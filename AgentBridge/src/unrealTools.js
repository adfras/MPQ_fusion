import fs from "node:fs";
import path from "node:path";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { config, nowStamp } from "./config.js";

const execFileAsync = promisify(execFile);

function makePythonString(value) {
  return JSON.stringify(String(value));
}

async function executePython(backends, code, timeout = 60000) {
  return backends.chir24.call("system_control", { action: "execute_python", code }, timeout);
}

function parseOutputJson(result) {
  const output = result?.payload?.output;
  if (typeof output !== "string" || !output.trim()) {
    return null;
  }
  try {
    return JSON.parse(output);
  } catch {
    return null;
  }
}

async function analyzeScreenshot(imagePath, compareTo = "") {
  const script = `
Add-Type -AssemblyName System.Drawing
function Get-Stats([string]$Path) {
  $bmp = [System.Drawing.Bitmap]::new($Path)
  try {
    $w = $bmp.Width; $h = $bmp.Height
    $step = [Math]::Max(1, [int][Math]::Floor([Math]::Sqrt(($w * $h) / 12000)))
    $count = 0; $r = 0.0; $g = 0.0; $b = 0.0; $bright = 0; $green = 0; $amber = 0; $sky = 0; $centerBright = 0; $centerCount = 0
    $edgeSamples = 0; $edgeSum = 0.0
    $centerLeft = [int]($w * 0.35); $centerRight = [int]($w * 0.65); $centerTop = [int]($h * 0.35); $centerBottom = [int]($h * 0.65)
    for ($y = 0; $y -lt $h; $y += $step) {
      for ($x = 0; $x -lt $w; $x += $step) {
        $c = $bmp.GetPixel($x, $y)
        $count++; $r += $c.R; $g += $c.G; $b += $c.B
        if (($c.R + $c.G + $c.B) -gt 45) { $bright++ }
        if ($c.G -gt 80 -and $c.G -gt ($c.R * 1.25) -and $c.G -gt ($c.B * 1.25)) { $green++ }
        if ($c.R -gt 130 -and $c.G -gt 75 -and $c.B -lt 120) { $amber++ }
        if ($c.B -gt 105 -and $c.B -gt ($c.R * 1.08) -and $c.G -gt 80 -and $c.R -gt 55) { $sky++ }
        if ($x -ge $centerLeft -and $x -le $centerRight -and $y -ge $centerTop -and $y -le $centerBottom) {
          $centerCount++
          if (($c.R + $c.G + $c.B) -gt 45) { $centerBright++ }
        }
        if ($x + $step -lt $w) {
          $n = $bmp.GetPixel($x + $step, $y)
          $edgeSum += ([Math]::Abs($c.R - $n.R) + [Math]::Abs($c.G - $n.G) + [Math]::Abs($c.B - $n.B)) / 3.0
          $edgeSamples++
        }
      }
    }
    [pscustomobject]@{
      path = $Path; width = $w; height = $h; samples = $count;
      avgR = [Math]::Round($r / $count, 1); avgG = [Math]::Round($g / $count, 1); avgB = [Math]::Round($b / $count, 1);
      nonDarkRatio = [Math]::Round($bright / $count, 4); centerNonDarkRatio = [Math]::Round($centerBright / [Math]::Max(1, $centerCount), 4);
      greenRatio = [Math]::Round($green / $count, 4); amberRatio = [Math]::Round($amber / $count, 4);
      skyLikeRatio = [Math]::Round($sky / $count, 4); edgeMean = [Math]::Round($edgeSum / [Math]::Max(1, $edgeSamples), 2)
    }
  } finally { $bmp.Dispose() }
}
function Compare-Images([string]$APath, [string]$BPath) {
  if (-not $BPath -or -not (Test-Path -LiteralPath $BPath)) { return $null }
  $a = [System.Drawing.Bitmap]::new($APath); $b = [System.Drawing.Bitmap]::new($BPath)
  try {
    $w = [Math]::Min($a.Width, $b.Width); $h = [Math]::Min($a.Height, $b.Height)
    $step = [Math]::Max(1, [int][Math]::Floor([Math]::Sqrt(($w * $h) / 12000)))
    $count = 0; $changed = 0; $sum = 0.0
    for ($y = 0; $y -lt $h; $y += $step) {
      for ($x = 0; $x -lt $w; $x += $step) {
        $ca = $a.GetPixel($x, $y); $cb = $b.GetPixel($x, $y)
        $d = ([Math]::Abs($ca.R - $cb.R) + [Math]::Abs($ca.G - $cb.G) + [Math]::Abs($ca.B - $cb.B)) / 3.0
        $count++; $sum += $d
        if ($d -gt 18) { $changed++ }
      }
    }
    [pscustomobject]@{ compareTo = $BPath; samples = $count; meanAbsDiff = [Math]::Round($sum / $count, 2); changedRatio = [Math]::Round($changed / $count, 4) }
  } finally { $a.Dispose(); $b.Dispose() }
}
$image = ${makePythonString(imagePath)}
$compare = ${makePythonString(compareTo)}
$result = [ordered]@{ success = $true; image = Get-Stats $image }
$comparison = Compare-Images $image $compare
if ($comparison) { $result["comparison"] = $comparison }
$result | ConvertTo-Json -Depth 8
`;

  const { stdout } = await execFileAsync("powershell.exe", ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script], { timeout: 60000 });
  return JSON.parse(stdout);
}

function screenshotQualityWarnings(analysis, focus = null) {
  const warnings = [];
  const image = analysis?.image;
  if (image?.skyLikeRatio >= 0.65 && image?.edgeMean <= 7.5) {
    warnings.push("Screenshot appears sky-heavy/low-detail; retake with focusActor, actorLabel, focusLocation, or cameraLocation.");
  }
  if (image?.edgeMean <= 2.0 && image?.centerNonDarkRatio <= 0.4) {
    warnings.push("Screenshot appears empty or badly framed; retake with focusActor, actorLabel, focusLocation, or cameraLocation.");
  }
  if (focus?.requested && !focus?.found) {
    warnings.push("Requested screenshot focus was not found; capture may not show the intended target.");
  }
  return warnings;
}

async function runMovePawn(backends, location) {
  const [x, y, z] = location;
  return executePython(backends, `import unreal
world=unreal.EditorLevelLibrary.get_game_world()
pc=unreal.GameplayStatics.get_player_controller(world,0)
pawn=pc.get_controlled_pawn()
pawn.set_actor_location(unreal.Vector(${Number(x)},${Number(y)},${Number(z)}), False, True)
print("pawn", pawn.get_name(), pawn.get_actor_location())`);
}

async function runCheckpoint(backends, checkpoint) {
  if (checkpoint.python) {
    return executePython(backends, checkpoint.python);
  }
  return { success: true, payload: { checkpoint } };
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function normalizePinConnection(connection = {}) {
  const normalized = {
    ...connection,
    action: "connect_pins",
    blueprintPath: connection.blueprintPath || connection.path,
    graphName: connection.graphName || "EventGraph",
    fromPinName: connection.fromPinName || connection.fromPin || connection.sourcePin || connection.sourcePinName,
    toPinName: connection.toPinName || connection.toPin || connection.targetPin || connection.targetPinName
  };
  delete normalized.path;
  delete normalized.fromPin;
  delete normalized.toPin;
  delete normalized.sourcePin;
  delete normalized.targetPin;
  delete normalized.sourcePinName;
  delete normalized.targetPinName;
  return normalized;
}

function normalizeUnrealAssetPath(value, defaultFolder = "/Game/CodexAgentTests") {
  let text = String(value || "").trim().replace(/^['"`]+|['"`]+$/g, "");
  if (!text) {
    return "";
  }
  text = text.replace(/\\/g, "/");
  const contentIndex = text.toLowerCase().indexOf("/content/");
  if (contentIndex >= 0) {
    text = `/Game/${text.slice(contentIndex + "/content/".length)}`;
  }
  text = text.replace(/\.(uasset|umap)$/i, "");
  const leaf = text.split("/").pop();
  if (text.includes(".") && leaf) {
    text = text.replace(new RegExp(`\\.${leaf}$`), "");
  }
  if (!text.startsWith("/")) {
    text = `${defaultFolder}/${text}`;
  }
  return text.replace(/\/+/g, "/");
}

function inferMcpTool(args = {}) {
  if (args.mcpTool || args.tool || args.toolName || args.mcpToolName || args.name || args.mcp_tool) {
    return args.mcpTool || args.tool || args.toolName || args.mcpToolName || args.name || args.mcp_tool;
  }
  if (args.action && (args.blueprintPath || args.graphName || args.nodeType || args.componentName || args.variableName)) {
    return "manage_blueprint";
  }
  if (args.action && (args.assetPath || args.materialPath || args.directory || args.path)) {
    return "manage_asset";
  }
  return "";
}

function normalizeMcpArguments(args = {}) {
  if (args.arguments && typeof args.arguments === "object") {
    return args.arguments;
  }
  if (args.args && typeof args.args === "object") {
    return args.args;
  }
  const ignored = new Set(["backend", "mcp", "mcpTool", "tool", "toolName", "mcpToolName", "name", "mcp_tool", "arguments", "args", "timeout"]);
  const payload = {};
  for (const [key, value] of Object.entries(args)) {
    if (!ignored.has(key)) {
      payload[key] = value;
    }
  }
  return payload;
}

export function createToolRegistry(backends, events) {
  const tools = {
    call_chir24_mcp: {
      permission: "run",
      description: "Call a raw ChiR24 MCP tool.",
      run: async (args = {}) => {
        const tool = inferMcpTool(args);
        if (!tool) {
          return { success: false, error: "Missing MCP tool name. Use mcpTool/tool/toolName, or pass a Blueprint action so manage_blueprint can be inferred." };
        }
        return backends.chir24.call(tool, normalizeMcpArguments(args), args.timeout || 60000);
      }
    },
    call_flopperam_mcp: {
      permission: "run",
      description: "Call a raw Flopperam MCP tool.",
      run: async (args = {}) => {
        const tool = inferMcpTool(args);
        if (!tool) {
          return { success: false, error: "Missing MCP tool name. Use mcpTool/tool/toolName." };
        }
        return backends.flopperam.call(tool, normalizeMcpArguments(args), args.timeout || 60000);
      }
    },
    list_mcp_tools: {
      permission: "read-only",
      description: "List raw MCP tools exposed by ChiR24 or Flopperam, including schemas when available.",
      run: async (args = {}) => {
        const backendName = String(args.backend || args.mcp || "chir24").toLowerCase();
        const backend = backendName.includes("flopperam") ? backends.flopperam : backends.chir24;
        return backend.listTools({ refresh: Boolean(args.refresh) });
      }
    },
    run_unreal_python: {
      permission: "run",
      description: "Run Unreal Python through ChiR24 system_control.",
      run: async (args) => executePython(backends, args.code || "", args.timeout || 60000)
    },
    execute_python: {
      permission: "run",
      description: "Alias for run_unreal_python.",
      run: async (args) => executePython(backends, args.code || "", args.timeout || 60000)
    },
    capture_viewport_screenshot: {
      permission: "read-only",
      description: "Capture the current level view to Saved/CodexAgent/Screenshots. Pass focusActor, actorLabel, targetActor, or focusLocation to frame the shot before capture.",
      run: async (args = {}) => {
        const width = Number(args.width || 1280);
        const height = Number(args.height || 720);
        const filename = args.filename || `viewport_${nowStamp()}.png`;
        const outputPath = path.join(config.screenshotsDir, filename);
        fs.mkdirSync(path.dirname(outputPath), { recursive: true });
        const outputDir = path.dirname(outputPath);
        const outputFile = path.basename(outputPath);
        const focusActor = args.focusActor || args.targetActor || args.actorLabel || args.actorName || args.actorClassContains || args.blueprintName || "";
        const screenshotOptions = {
          focusActor,
          focusLocation: args.focusLocation || args.targetLocation || null,
          cameraLocation: args.cameraLocation || null,
          cameraRotation: args.cameraRotation || null,
          cameraOffset: args.cameraOffset || null,
          distance: args.distance || args.cameraDistance || null,
          heightOffset: args.heightOffset || args.cameraHeightOffset || null,
          fov: args.fov || args.fovAngle || 70,
          requireFocus: Boolean(args.requireFocus || args.focusRequired || args.requireActorVisible),
          setEditorViewport: args.setEditorViewport !== false
        };
        const code = `import unreal, os, json, traceback
out_dir = ${makePythonString(outputDir)}
file_name = ${makePythonString(outputFile)}
out = os.path.join(out_dir, file_name)
options = json.loads(${makePythonString(JSON.stringify(screenshotOptions))})
os.makedirs(out_dir, exist_ok=True)
actor = None
focus = {"requested": False, "found": False}

def get_active_world():
    game_world = None
    try:
        game_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    except Exception:
        pass
    if game_world is None:
        try:
            game_world = unreal.EditorLevelLibrary.get_game_world()
        except Exception:
            pass
    if game_world is not None:
        return game_world, "PIE"
    return unreal.EditorLevelLibrary.get_editor_world(), "Editor"

def vector_from(value, default=None):
    if value is None:
        return default
    if isinstance(value, dict):
        return unreal.Vector(float(value.get("x", 0.0)), float(value.get("y", 0.0)), float(value.get("z", 0.0)))
    return unreal.Vector(float(value[0]), float(value[1]), float(value[2]))

def rotator_from(value, default=None):
    if value is None:
        return default
    if isinstance(value, dict):
        return unreal.Rotator(float(value.get("pitch", 0.0)), float(value.get("yaw", 0.0)), float(value.get("roll", 0.0)))
    return unreal.Rotator(float(value[0]), float(value[1]), float(value[2]))

def vec_list(value):
    return [round(value.x, 3), round(value.y, 3), round(value.z, 3)]

def rot_list(value):
    return [round(value.roll, 3), round(value.pitch, 3), round(value.yaw, 3)]

def actor_text(actor_obj):
    parts = []
    for getter in (
        lambda: actor_obj.get_actor_label(),
        lambda: actor_obj.get_name(),
        lambda: actor_obj.get_class().get_name(),
        lambda: actor_obj.get_path_name(),
    ):
        try:
            parts.append(str(getter()))
        except Exception:
            pass
    return " ".join(parts)

def find_actor(world, world_kind, query):
    query = str(query or "").strip().lower()
    if not query:
        return None
    if world_kind == "Editor":
        candidates = unreal.EditorLevelLibrary.get_all_level_actors()
    else:
        candidates = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    exact = None
    contains = None
    for candidate in candidates:
        text = actor_text(candidate)
        text_lower = text.lower()
        if not text_lower:
            continue
        if query == text_lower:
            exact = candidate
            break
        if contains is None and query in text_lower:
            contains = candidate
    return exact or contains

def bounds_for(actor_obj):
    try:
        origin, extent = actor_obj.get_actor_bounds(False)
        return origin, extent
    except Exception:
        return actor_obj.get_actor_location(), unreal.Vector(160.0, 160.0, 160.0)

def look_at(start, target):
    try:
        return unreal.MathLibrary.find_look_at_rotation(start, target)
    except Exception:
        direction = target - start
        return direction.rotator()

def resolve_camera(world, world_kind):
    camera_location = vector_from(options.get("cameraLocation"))
    camera_rotation = rotator_from(options.get("cameraRotation"))
    if camera_location is not None and camera_rotation is not None:
        focus.update({
            "requested": True,
            "found": True,
            "mode": "explicitCamera",
            "cameraLocation": vec_list(camera_location),
            "cameraRotation": rot_list(camera_rotation)
        })
        return camera_location, camera_rotation, True

    focus_location = vector_from(options.get("focusLocation"))
    focus_actor = options.get("focusActor")
    focus_requested = focus_location is not None or bool(str(focus_actor or "").strip())
    focus["requested"] = focus_requested
    if not focus_requested:
        return None, None, False

    target_actor = None
    if focus_location is None:
        target_actor = find_actor(world, world_kind, focus_actor)
        if target_actor is None:
            focus.update({"found": False, "query": focus_actor, "worldKind": world_kind})
            return None, None, False
        focus_location, extent = bounds_for(target_actor)
        radius = max(abs(extent.x), abs(extent.y), abs(extent.z), 140.0)
        try:
            label = target_actor.get_actor_label()
        except Exception:
            label = target_actor.get_name()
        focus.update({"found": True, "query": focus_actor, "actor": label, "class": target_actor.get_class().get_name(), "worldKind": world_kind})
    else:
        radius = 180.0
        focus.update({"found": True, "query": "focusLocation", "worldKind": world_kind})

    distance = float(options.get("distance") or max(720.0, radius * 3.8))
    height_offset = float(options.get("heightOffset") or max(260.0, radius * 1.35))
    offset = vector_from(options.get("cameraOffset"), unreal.Vector(-distance, -distance, height_offset))
    camera_location = focus_location + offset
    camera_rotation = look_at(camera_location, focus_location)
    focus.update({
        "targetLocation": vec_list(focus_location),
        "cameraLocation": vec_list(camera_location),
        "cameraRotation": rot_list(camera_rotation),
        "distance": round(distance, 3),
        "heightOffset": round(height_offset, 3)
    })
    return camera_location, camera_rotation, True

def capture_with_python_scene_capture(world, location, rotation):
    global actor
    rt = unreal.RenderingLibrary.create_render_target2d(world, ${width}, ${height}, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.SceneCapture2D, location, rotation)
    comp = actor.get_component_by_class(unreal.SceneCaptureComponent2D)
    comp.set_editor_property("texture_target", rt)
    comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    comp.set_editor_property("fov_angle", float(options.get("fov") or 70.0))
    comp.capture_scene()
    unreal.RenderingLibrary.export_render_target(world, rt, out_dir, file_name)

try:
    world, world_kind = get_active_world()
    if world is None:
        raise RuntimeError("No editor or PIE world is available for screenshot capture")
    loc, rot, framed = resolve_camera(world, world_kind)
    if framed and world_kind == "Editor":
        if options.get("setEditorViewport", True):
            try:
                unreal.EditorLevelLibrary.set_level_viewport_camera_info(loc, rot)
            except Exception:
                pass
        capture_with_python_scene_capture(world, loc, rot)
        print(json.dumps({"screenshot": out, "exists": os.path.exists(out), "ok": os.path.exists(out), "method": "PythonSceneCapture2DFramed", "worldKind": world_kind, "focus": focus}))
    elif focus.get("requested") and options.get("requireFocus") and not focus.get("found"):
        print(json.dumps({"screenshot": out, "exists": os.path.exists(out), "ok": False, "method": "NoCapture", "worldKind": world_kind, "focus": focus, "error": "Requested screenshot focus actor/location was not found"}))
    elif hasattr(unreal, "CodexAgentScreenshotLibrary"):
        ok = unreal.CodexAgentScreenshotLibrary.capture_level_screenshot(out, ${width}, ${height})
        if framed:
            focus["warning"] = "Focused camera resolved, but PIE screenshots currently use the active player camera"
        print(json.dumps({"screenshot": out, "exists": os.path.exists(out), "ok": bool(ok), "method": "CodexAgentScreenshotLibrary", "worldKind": world_kind, "focus": focus}))
    else:
        try:
            loc, rot = unreal.EditorLevelLibrary.get_level_viewport_camera_info()
        except Exception:
            loc = unreal.Vector(0.0, -700.0, 350.0)
            rot = unreal.Rotator(-25.0, 0.0, 0.0)
        capture_with_python_scene_capture(world, loc, rot)
        print(json.dumps({"screenshot": out, "exists": os.path.exists(out), "method": "PythonSceneCapture2D", "worldKind": world_kind, "focus": focus}))
except Exception as exc:
    print(json.dumps({"screenshot": out, "exists": os.path.exists(out), "error": str(exc), "trace": traceback.format_exc()}))
finally:
    if actor:
        try:
            unreal.EditorLevelLibrary.destroy_actor(actor)
        except Exception:
            try:
                actor.destroy_actor()
            except Exception:
                pass
`;
        const result = await executePython(backends, code);
        const payload = parseOutputJson(result);
        const exists = fs.existsSync(outputPath);
        return {
          ...result,
          success: result.success !== false && payload?.ok !== false && !payload?.error && exists,
          screenshotPath: outputPath,
          exists,
          capture: payload,
          method: payload?.method,
          focus: payload?.focus
        };
      }
    },
    analyze_screenshot: {
      permission: "read-only",
      description: "Analyze a screenshot for basic visual stats and optionally compare it to a previous screenshot.",
      run: async (args = {}) => {
        const imagePath = args.path || args.screenshotPath || args.imagePath;
        if (!imagePath || !fs.existsSync(imagePath)) {
          return { success: false, error: `Screenshot not found: ${imagePath || ""}` };
        }
        const analysis = await analyzeScreenshot(imagePath, args.compareTo || args.beforePath || "");
        return { ...analysis, warnings: screenshotQualityWarnings(analysis) };
      }
    },
    capture_visual_checkpoint: {
      permission: "read-only",
      description: "Capture a screenshot and return deterministic visual stats; pass compareTo to quantify before/after visual change.",
      run: async (args = {}) => {
        const screenshot = await tools.capture_viewport_screenshot.run(args);
        if (screenshot?.success === false || !screenshot.exists) {
          return screenshot;
        }
        const analysis = await analyzeScreenshot(screenshot.screenshotPath, args.compareTo || args.beforePath || "");
        const warnings = screenshotQualityWarnings(analysis, screenshot.focus);
        return { ...screenshot, visualAnalysis: analysis, warnings };
      }
    },
    inspect_scene: {
      permission: "read-only",
      description: "One-call editor state, selected actors, actor summary, PlayerStart, camera info, and optional screenshot/visual analysis.",
      run: async (args = {}) => {
        const code = `import unreal, json
editor_world = unreal.EditorLevelLibrary.get_editor_world()
game_world = unreal.EditorLevelLibrary.get_game_world()
try:
    cam_loc, cam_rot = unreal.EditorLevelLibrary.get_level_viewport_camera_info()
    camera = {"location": [round(cam_loc.x,1), round(cam_loc.y,1), round(cam_loc.z,1)], "rotation": [round(cam_rot.roll,1), round(cam_rot.pitch,1), round(cam_rot.yaw,1)]}
except Exception:
    camera = None
actors = []
class_counts = {}
player_starts = []
for a in unreal.EditorLevelLibrary.get_all_level_actors():
    loc = a.get_actor_location()
    cls = a.get_class().get_name()
    label = a.get_actor_label()
    class_counts[cls] = class_counts.get(cls, 0) + 1
    entry = {"label": label, "class": cls, "location": [round(loc.x,1), round(loc.y,1), round(loc.z,1)]}
    actors.append(entry)
    if cls == "PlayerStart" or label.startswith("PlayerStart"):
        player_starts.append(entry)
selected = [a.get_actor_label() for a in unreal.EditorLevelLibrary.get_selected_level_actors()]
print(json.dumps({
    "editorMap": editor_world.get_path_name() if editor_world else None,
    "pieRunning": game_world is not None,
    "pieWorld": game_world.get_path_name() if game_world else None,
    "actorCount": len(actors),
    "classCounts": class_counts,
    "selected": selected,
    "playerStarts": player_starts,
    "camera": camera,
    "actors": actors if ${Boolean(args.includeActors) ? "True" : "False"} else actors[:20]
}))`;
        const stateResult = await executePython(backends, code, args.timeout || 60000);
        const scene = parseOutputJson(stateResult);
        const result = { ...stateResult, scene };
        if (args.screenshot !== false) {
          result.screenshot = await tools.capture_visual_checkpoint.run({
            filename: args.filename || `scene_checkpoint_${nowStamp()}.png`,
            width: args.width,
            height: args.height,
            compareTo: args.compareTo || args.beforePath || "",
            focusActor: args.focusActor || args.targetActor || args.actorLabel || args.actorName || args.actorClassContains || "",
            focusLocation: args.focusLocation || args.targetLocation || null,
            cameraLocation: args.cameraLocation || null,
            cameraRotation: args.cameraRotation || null,
            cameraOffset: args.cameraOffset || null,
            distance: args.distance || args.cameraDistance || null,
            heightOffset: args.heightOffset || args.cameraHeightOffset || null,
            fov: args.fov || args.fovAngle || null,
            requireFocus: Boolean(args.requireFocus || args.focusRequired || args.requireActorVisible)
          });
        }
        return result;
      }
    },
    create_color_material: {
      permission: "edit",
      description: "Create or update a simple constant-color material with correct Unreal path/name handling.",
      run: async (args = {}) => {
        const assetPath = String(args.assetPath || args.materialPath || "");
        const folder = String(args.folder || args.path || assetPath.split("/").slice(0, -1).join("/") || "/Game/CodexAgentTests");
        const name = String(args.name || assetPath.split("/").pop() || `M_Codex_Color_${nowStamp()}`);
        const color = Array.isArray(args.color) ? args.color : [0.0, 1.0, 0.0, 1.0];
        const [r, g, b, a = 1.0] = color.map((value) => Number(value));
        const code = `import unreal, json, traceback
folder = ${makePythonString(folder)}
name = ${makePythonString(name)}
asset_path = folder.rstrip('/') + '/' + name
try:
    unreal.EditorAssetLibrary.make_directory(folder)
    mat = unreal.EditorAssetLibrary.load_asset(asset_path)
    if mat is None:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError('create_asset returned None for ' + asset_path)
    unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
    color_expr = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -300, 0)
    color_expr.set_editor_property('constant', unreal.LinearColor(${r}, ${g}, ${b}, ${a}))
    unreal.MaterialEditingLibrary.connect_material_property(color_expr, '', unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    saved = unreal.EditorAssetLibrary.save_loaded_asset(mat)
    print(json.dumps({"success": True, "assetPath": mat.get_path_name(), "folder": folder, "name": name, "saved": bool(saved), "color": [${r}, ${g}, ${b}, ${a}]}))
except Exception as exc:
    print(json.dumps({"success": False, "assetPath": asset_path, "error": str(exc), "trace": traceback.format_exc()}))`;
        const result = await executePython(backends, code, args.timeout || 60000);
        const payload = parseOutputJson(result);
        return { ...result, success: payload?.success !== false && result.success !== false, material: payload };
      }
    },
    setup_blueprint_components: {
      permission: "edit",
      description: "Ensure Blueprint SCS components exist, then repair template transforms/properties in one deterministic Python pass.",
      run: async (args = {}) => {
        const blueprintPath = normalizeUnrealAssetPath(args.blueprintPath || args.assetPath || args.path || args.blueprint || args.blueprintName || args.name);
        const components = Array.isArray(args.components) ? args.components : [];
        if (!blueprintPath || components.length === 0) {
          return { success: false, error: "blueprintPath and components[] are required" };
        }

        const componentsJson = JSON.stringify(components);
        const repairCode = `import unreal, json, traceback
blueprint_path = ${makePythonString(blueprintPath)}
components = json.loads(${makePythonString(componentsJson)})

def vec(value, default):
    if value is None:
        return default
    if isinstance(value, dict):
        return unreal.Vector(float(value.get("x", default.x)), float(value.get("y", default.y)), float(value.get("z", default.z)))
    return unreal.Vector(float(value[0]), float(value[1]), float(value[2]))

def rot(value):
    if value is None:
        return None
    if isinstance(value, dict):
        return unreal.Rotator(float(value.get("pitch", 0.0)), float(value.get("yaw", 0.0)), float(value.get("roll", 0.0)))
    return unreal.Rotator(float(value[0]), float(value[1]), float(value[2]))

def apply_known_property(comp, key, value):
    if value is None:
        return False
    lower = key.lower()
    if lower in ("collision", "collisionprofile", "collisionprofilename", "collision_profile_name"):
        comp.set_collision_profile_name(str(value))
        return True
    if lower in ("collisionprofilename", "collision_profile_name"):
        comp.set_collision_profile_name(str(value))
        return True
    if lower in ("collisionenabled", "collision_enabled"):
        raw = str(value)
        normalized = raw.replace(" ", "").replace("-", "").replace("_", "").lower()
        mapping = {
            "nocollision": unreal.CollisionEnabled.NO_COLLISION,
            "queryonly": unreal.CollisionEnabled.QUERY_ONLY,
            "physicsonly": unreal.CollisionEnabled.PHYSICS_ONLY,
            "queryandphysics": unreal.CollisionEnabled.QUERY_AND_PHYSICS,
            "probeonly": unreal.CollisionEnabled.PROBE_ONLY,
            "queryandprobe": unreal.CollisionEnabled.QUERY_AND_PROBE,
        }
        if normalized in mapping and hasattr(comp, "set_collision_enabled"):
            comp.set_collision_enabled(mapping[normalized])
            return True
    if lower in ("collisionresponse", "collision_response"):
        raw = str(value).lower()
        mapping = {
            "ignore": unreal.CollisionResponseType.ECR_IGNORE,
            "ignoreall": unreal.CollisionResponseType.ECR_IGNORE,
            "overlap": unreal.CollisionResponseType.ECR_OVERLAP,
            "overlapall": unreal.CollisionResponseType.ECR_OVERLAP,
            "block": unreal.CollisionResponseType.ECR_BLOCK,
            "blockall": unreal.CollisionResponseType.ECR_BLOCK,
        }
        if raw in mapping and hasattr(comp, "set_collision_response_to_all_channels"):
            comp.set_collision_response_to_all_channels(mapping[raw])
            return True
    if lower in ("hiddeningame", "hidden_in_game", "bhiddeningame"):
        if hasattr(comp, "set_hidden_in_game"):
            comp.set_hidden_in_game(bool(value))
            return True
        comp.set_editor_property("hidden_in_game", bool(value))
        return True
    if lower in ("generatesoverlapevents", "bgenerateoverlapevents", "generateoverlapevents"):
        try:
            comp.set_generate_overlap_events(bool(value))
        except Exception:
            comp.set_editor_property("generate_overlap_events", bool(value))
        return True
    if lower in ("boxextent", "box_extent"):
        if hasattr(comp, "set_box_extent"):
            comp.set_box_extent(vec(value, unreal.Vector(32, 32, 32)), False)
            return True
    if lower == "intensity" and hasattr(comp, "set_intensity"):
        comp.set_intensity(float(value))
        return True
    if lower in ("attenuationradius", "attenuation_radius") and hasattr(comp, "set_attenuation_radius"):
        comp.set_attenuation_radius(float(value))
        return True
    if lower in ("lightcolor", "light_color") and hasattr(comp, "set_light_color"):
        color = value
        if isinstance(color, dict):
            c = unreal.LinearColor(float(color.get("r", 1)), float(color.get("g", 1)), float(color.get("b", 1)), float(color.get("a", 1)))
        else:
            scale = 255.0 if max(color[:3]) > 1.0 else 1.0
            c = unreal.LinearColor(float(color[0]) / scale, float(color[1]) / scale, float(color[2]) / scale, float(color[3] if len(color) > 3 else scale) / scale)
        comp.set_light_color(c)
        return True
    return False

def set_property_best_effort(comp, key, value):
    try:
        if apply_known_property(comp, key, value):
            return True, None
        if isinstance(value, list) and len(value) == 3:
            comp.set_editor_property(key, vec(value, unreal.Vector(0, 0, 0)))
        elif isinstance(value, list) and len(value) == 4 and "color" in key.lower():
            scale = 255.0 if max(value[:3]) > 1.0 else 1.0
            comp.set_editor_property(key, unreal.LinearColor(float(value[0]) / scale, float(value[1]) / scale, float(value[2]) / scale, float(value[3]) / scale))
        else:
            comp.set_editor_property(key, value)
        return True, None
    except Exception as exc:
        return False, str(exc)

def component_class_from_name(name):
    value = str(name or "SceneComponent")
    value = value.replace("U", "", 1) if value.startswith("U") else value
    cls = getattr(unreal, value, None)
    if cls:
        return cls
    if not value.endswith("Component"):
        cls = getattr(unreal, value + "Component", None)
        if cls:
            return cls
    raise RuntimeError("Unknown component class: " + value)

def gather_components(bp, subsys, lib):
    handles = subsys.k2_gather_subobject_data_for_blueprint(bp)
    by_base = {}
    root_handle = handles[0] if handles else None
    root_base = None
    seen = set()
    existing = []
    for handle in handles:
        data = subsys.k2_find_subobject_data_from_handle(handle)
        if not lib.is_component(data):
            continue
        obj = lib.get_object_for_blueprint(data, bp)
        if not obj:
            continue
        key = obj.get_path_name()
        if key in seen:
            continue
        seen.add(key)
        base = obj.get_name().replace("_GEN_VARIABLE", "")
        by_base[base] = obj
        existing.append({"name": obj.get_name(), "class": obj.get_class().get_name()})
        if lib.is_root_component(data):
            root_handle = handle
            root_base = base
    return handles, by_base, root_handle, root_base, existing

out = {"success": False, "blueprintPath": blueprint_path, "components": [], "errors": []}
try:
    bp = unreal.EditorAssetLibrary.load_asset(blueprint_path)
    if bp is None:
        raise RuntimeError("Blueprint not found: " + blueprint_path)
    subsys = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    handles, by_base, root_handle, root_base, existing_before = gather_components(bp, subsys, lib)
    out["existingBefore"] = existing_before
    created = []
    for spec in components:
        name = spec.get("name") or spec.get("componentName")
        component_class_name = spec.get("componentClass") or spec.get("class") or spec.get("type") or spec.get("componentType")
        if not name or name in by_base:
            continue
        if component_class_name in ("SceneRoot", "Root") and root_handle is not None:
            try:
                subsys.rename_subobject_member_variable(bp, root_handle, name)
                created.append({"name": name, "reusedRoot": root_base})
            except Exception as exc:
                out["errors"].append("Could not rename root component to " + name + ": " + str(exc))
            handles, by_base, root_handle, root_base, existing_after_rename = gather_components(bp, subsys, lib)
            continue
        params = unreal.AddNewSubobjectParams()
        params.blueprint_context = bp
        params.parent_handle = root_handle if root_handle is not None else handles[0]
        params.new_class = component_class_from_name(component_class_name)
        params.conform_transform_to_parent = False
        handle, fail_reason = subsys.add_new_subobject(params)
        if str(fail_reason):
            out["errors"].append("Add component " + name + ": " + str(fail_reason))
        try:
            subsys.rename_subobject_member_variable(bp, handle, name)
        except Exception as exc:
            out["errors"].append("Rename component " + name + ": " + str(exc))
        created.append({"name": name, "class": component_class_name})
        handles, by_base, root_handle, root_base, existing_after_create = gather_components(bp, subsys, lib)
    out["created"] = created

    for spec in components:
        name = spec.get("name") or spec.get("componentName")
        comp = by_base.get(name)
        row = {"name": name, "exists": comp is not None}
        if comp is None:
            out["components"].append(row)
            out["errors"].append("Missing component " + str(name))
            continue

        transform = spec.get("transform") or {}
        location = spec.get("location", transform.get("location"))
        rotation = spec.get("rotation", transform.get("rotation"))
        scale = spec.get("scale", transform.get("scale"))
        if location is not None:
            comp.set_editor_property("relative_location", vec(location, unreal.Vector(0, 0, 0)))
        if rotation is not None:
            comp.set_editor_property("relative_rotation", rot(rotation))
        if scale is not None:
            comp.set_editor_property("relative_scale3d", vec(scale, unreal.Vector(1, 1, 1)))

        mesh_path = spec.get("meshPath")
        if mesh_path and hasattr(comp, "set_static_mesh"):
            mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
            if mesh:
                comp.set_static_mesh(mesh)
                row["mesh"] = mesh.get_path_name()
            else:
                out["errors"].append("Mesh not found for " + name + ": " + mesh_path)

        material_path = spec.get("materialPath")
        if material_path and hasattr(comp, "set_material"):
            mat = unreal.EditorAssetLibrary.load_asset(material_path)
            if mat:
                comp.set_material(int(spec.get("materialIndex", 0)), mat)
                row["material"] = mat.get_path_name()
            else:
                out["errors"].append("Material not found for " + name + ": " + material_path)

        properties = {}
        properties.update(spec.get("properties") or {})
        for shortcut in ["collision", "Collision", "collisionProfileName", "CollisionProfileName", "collisionEnabled", "CollisionEnabled", "collisionResponse", "CollisionResponse", "hiddenInGame", "HiddenInGame", "boxExtent", "BoxExtent", "generateOverlapEvents", "bGenerateOverlapEvents", "intensity", "Intensity", "attenuationRadius", "AttenuationRadius", "lightColor", "LightColor"]:
            if shortcut in spec:
                properties[shortcut] = spec[shortcut]
        row["properties"] = {}
        for key, value in properties.items():
            ok, error = set_property_best_effort(comp, key, value)
            row["properties"][key] = ok
            if error:
                out["errors"].append(name + "." + key + ": " + error)

        if isinstance(comp, unreal.SceneComponent):
            loc = comp.get_editor_property("relative_location")
            scl = comp.get_editor_property("relative_scale3d")
            row["relativeLocation"] = [round(loc.x, 3), round(loc.y, 3), round(loc.z, 3)]
            row["relativeScale"] = [round(scl.x, 3), round(scl.y, 3), round(scl.z, 3)]
        out["components"].append(row)

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    saved = unreal.EditorAssetLibrary.save_loaded_asset(bp) if ${Boolean(args.save ?? true) ? "True" : "False"} else False
    out["success"] = len(out["errors"]) == 0
    out["saved"] = bool(saved)
except Exception as exc:
    out["error"] = str(exc)
    out["trace"] = traceback.format_exc()
print(json.dumps(out))`;
        const repairResult = await executePython(backends, repairCode, args.timeout || 60000);
        const payload = parseOutputJson(repairResult);
        return {
          ...repairResult,
          success: payload?.success !== false && repairResult.success !== false,
          blueprintPath,
          addedMissingComponents: payload?.created?.length || 0,
          setup: payload,
          existingBefore: payload?.existingBefore || []
        };
      }
    },
    take_editor_screenshot: {
      permission: "read-only",
      description: "Alias for capture_viewport_screenshot.",
      run: async (args) => tools.capture_viewport_screenshot.run(args)
    },
    take_pie_screenshot: {
      permission: "read-only",
      description: "Alias for capture_viewport_screenshot while PIE is running.",
      run: async (args) => tools.capture_viewport_screenshot.run(args)
    },
    start_pie: {
      permission: "run",
      description: "Start Play in Editor.",
      run: async () => backends.chir24.call("control_editor", { action: "play" }, 60000)
    },
    stop_pie: {
      permission: "run",
      description: "Stop Play in Editor.",
      run: async () => backends.chir24.call("control_editor", { action: "stop" }, 60000)
    },
    load_level: {
      permission: "run",
      description: "Load an Unreal level.",
      run: async (args) => backends.chir24.call("manage_level", { action: "load", levelPath: args.levelPath || args.path }, 60000)
    },
    get_level_actors: {
      permission: "read-only",
      description: "Return labels, classes, and locations for current editor level actors.",
      run: async () => executePython(backends, `import unreal, json
actors=[]
for a in unreal.EditorLevelLibrary.get_all_level_actors():
    loc=a.get_actor_location()
    actors.append({"label": a.get_actor_label(), "class": a.get_class().get_name(), "location": [round(loc.x,1), round(loc.y,1), round(loc.z,1)]})
print(json.dumps({"actors": actors}))`)
    },
    get_selected_actor: {
      permission: "read-only",
      description: "Return selected actor labels.",
      run: async () => executePython(backends, `import unreal, json
selected=unreal.EditorLevelLibrary.get_selected_level_actors()
print(json.dumps({"selected": [a.get_actor_label() for a in selected]}))`)
    },
    get_editor_state: {
      permission: "read-only",
      description: "Return current editor map and PIE status.",
      run: async () => executePython(backends, `import unreal, json
editor_world = unreal.EditorLevelLibrary.get_editor_world()
game_world = unreal.EditorLevelLibrary.get_game_world()
print(json.dumps({
    "editorMap": editor_world.get_path_name() if editor_world else None,
    "pieRunning": game_world is not None,
    "pieWorld": game_world.get_path_name() if game_world else None
}))`)
    },
    inspect_blueprint: {
      permission: "read-only",
      description: "Inspect a Blueprint EventGraph.",
      run: async (args) => {
        const blueprintPath = normalizeUnrealAssetPath(args.blueprintPath || args.assetPath || args.path || args.blueprint || args.blueprintName || args.name);
        if (!blueprintPath) {
          return { success: false, error: "blueprintPath is required" };
        }
        const graph = await backends.chir24.call("manage_blueprint", {
          action: "get_graph_details",
          blueprintPath,
          graphName: args.graphName || "EventGraph"
        }, 60000);
        const nodes = graph.payload?.nodes || graph.payload?.result?.nodes || [];
        return { ...graph, nodeCount: nodes.length };
      }
    },
    connect_blueprint_pins: {
      permission: "edit",
      description: "Connect Blueprint graph pins using the ChiR24 argument names that work in UE 5.7.",
      run: async (args = {}) => {
        const payload = normalizePinConnection(args);
        return backends.chir24.call("manage_blueprint", payload, args.timeout || 60000);
      }
    },
    connect_blueprint_pins_batch: {
      permission: "edit",
      description: "Connect multiple Blueprint pin links in one bridge call using the known-good ChiR24 fromPinName/toPinName shape.",
      run: async (args = {}) => {
        const blueprintPath = args.blueprintPath || args.path;
        const graphName = args.graphName || "EventGraph";
        const connections = Array.isArray(args.connections) ? args.connections : [];
        if (!blueprintPath || connections.length === 0) {
          return { success: false, error: "blueprintPath and connections[] are required" };
        }
        const results = [];
        for (const connection of connections) {
          const payload = normalizePinConnection({
            ...connection,
            blueprintPath: connection.blueprintPath || blueprintPath,
            graphName: connection.graphName || graphName
          });
          const result = await backends.chir24.call("manage_blueprint", payload, args.timeout || 60000);
          results.push({
            success: result.success !== false,
            fromNodeId: payload.fromNodeId,
            fromPinName: payload.fromPinName,
            toNodeId: payload.toNodeId,
            toPinName: payload.toPinName,
            result
          });
          if (result.success === false && args.stopOnFailure !== false) {
            break;
          }
        }
        const failed = results.filter((result) => !result.success);
        return { success: failed.length === 0, blueprintPath, graphName, requested: connections.length, connected: results.length - failed.length, failed: failed.length, results };
      }
    },
    compile_blueprint: {
      permission: "edit",
      description: "Compile a Blueprint.",
      run: async (args) => backends.chir24.call("manage_blueprint", {
        action: "compile",
        blueprintPath: args.blueprintPath || args.path,
        saveAfterCompile: Boolean(args.saveAfterCompile ?? true)
      }, 60000)
    },
    save_asset: {
      permission: "edit",
      description: "Save a single Unreal asset.",
      run: async (args) => executePython(backends, `import unreal
asset_path = ${makePythonString(args.assetPath || args.path)}
ok = unreal.EditorAssetLibrary.save_asset(asset_path, False)
print({"saved": ok, "asset": asset_path})`)
    },
    save_all: {
      permission: "edit",
      description: "Save all dirty assets through the editor bridge.",
      run: async () => backends.chir24.call("control_editor", { action: "save_all" }, 60000)
    },
    run_pie_overlap_test: {
      permission: "run",
      description: "Start PIE, move the player pawn into a trigger target, let the game tick outside Python, inspect actor component state, capture a visual checkpoint, and optionally stop PIE.",
      run: async (args = {}) => {
        const targetLocation = Array.isArray(args.targetLocation || args.location)
          ? (args.targetLocation || args.location).map((value) => Number(value))
          : [300, 0, 92];
        const actorMatch = args.actorLabel || args.actorName || args.actorClassContains || args.blueprintName || "";
        const componentNames = Array.isArray(args.componentNames) ? args.componentNames : [];
        const startWaitMs = Number(args.startWaitMs ?? 700);
        const tickWaitMs = Number(args.tickWaitMs ?? 1500);
        const stopPie = args.stopPie !== false;
        const started = args.assumePieRunning ? { success: true, assumed: true } : await tools.start_pie.run({});
        if (started.success === false) {
          return { success: false, stage: "start_pie", startPie: started };
        }
        await sleep(startWaitMs);

        const moveCode = `import unreal, json, traceback
out = {"success": False}
try:
    world = None
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    except Exception:
        pass
    if world is None:
        world = unreal.EditorLevelLibrary.get_game_world()
    if world is None:
        raise RuntimeError("PIE game world not found")
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        pawns = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn)
        pawn = pawns[0] if pawns else None
    if pawn is None:
        raise RuntimeError("Player pawn not found")
    before = pawn.get_actor_location()
    target = unreal.Vector(${targetLocation[0]}, ${targetLocation[1]}, ${targetLocation[2]})
    moved = pawn.set_actor_location(target, ${args.sweep === false ? "False" : "True"}, False)
    after = pawn.get_actor_location()
    out = {
        "success": True,
        "world": world.get_path_name(),
        "pawn": pawn.get_name(),
        "pawnClass": pawn.get_class().get_name(),
        "moved": bool(moved),
        "before": [round(before.x, 3), round(before.y, 3), round(before.z, 3)],
        "after": [round(after.x, 3), round(after.y, 3), round(after.z, 3)]
    }
except Exception as exc:
    out = {"success": False, "error": str(exc), "trace": traceback.format_exc()}
print(json.dumps(out))`;
        const moveResult = await executePython(backends, moveCode, args.timeout || 60000);
        const move = parseOutputJson(moveResult);
        if (move?.success === false) {
          if (stopPie) {
            await tools.stop_pie.run({});
          }
          return { ...moveResult, success: false, stage: "move_pawn", startPie: started, move };
        }

        await sleep(tickWaitMs);

        const inspectCode = `import unreal, json, traceback
actor_match = ${makePythonString(actorMatch)}
component_names = set(json.loads(${makePythonString(JSON.stringify(componentNames))}))
out = {"success": False, "actors": []}
try:
    world = None
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    except Exception:
        pass
    if world is None:
        world = unreal.EditorLevelLibrary.get_game_world()
    if world is None:
        raise RuntimeError("PIE game world not found")
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    for actor in actors:
        label = actor.get_actor_label()
        name = actor.get_name()
        cls = actor.get_class().get_name()
        haystack = " ".join([label, name, cls])
        if actor_match and actor_match not in haystack:
            continue
        loc = actor.get_actor_location()
        row = {"label": label, "name": name, "class": cls, "location": [round(loc.x, 3), round(loc.y, 3), round(loc.z, 3)], "components": []}
        for comp in actor.get_components_by_class(unreal.ActorComponent):
            comp_name = comp.get_name()
            base_name = comp_name.replace("_GEN_VARIABLE", "")
            if component_names and base_name not in component_names and comp_name not in component_names:
                continue
            c = {"name": comp_name, "baseName": base_name, "class": comp.get_class().get_name()}
            if isinstance(comp, unreal.SceneComponent):
                rel = comp.get_editor_property("relative_location")
                c["relativeLocation"] = [round(rel.x, 3), round(rel.y, 3), round(rel.z, 3)]
            if hasattr(comp, "get_material"):
                try:
                    mat = comp.get_material(0)
                    c["material"] = mat.get_path_name() if mat else None
                except Exception:
                    pass
            if hasattr(comp, "get_intensity"):
                try:
                    c["intensity"] = comp.get_intensity()
                except Exception:
                    pass
            row["components"].append(c)
        out["actors"].append(row)
    out["success"] = True
    out["world"] = world.get_path_name()
    out["actorCount"] = len(out["actors"])
except Exception as exc:
    out = {"success": False, "error": str(exc), "trace": traceback.format_exc()}
print(json.dumps(out))`;
        const inspectionResult = await executePython(backends, inspectCode, args.timeout || 60000);
        const inspection = parseOutputJson(inspectionResult);

        let screenshot = null;
        if (args.screenshot !== false) {
          screenshot = await tools.capture_visual_checkpoint.run({
            filename: args.filename || `pie_overlap_${nowStamp()}.png`,
            width: args.width,
            height: args.height,
            compareTo: args.compareTo || args.beforePath || "",
            focusActor: args.focusActor || (args.frameActor ? actorMatch : ""),
            focusLocation: args.focusLocation || null,
            cameraLocation: args.cameraLocation || null,
            cameraRotation: args.cameraRotation || null,
            cameraOffset: args.cameraOffset || null,
            distance: args.distance || args.cameraDistance || null,
            heightOffset: args.heightOffset || args.cameraHeightOffset || null,
            fov: args.fov || args.fovAngle || null,
            requireFocus: Boolean(args.requireFocus || args.focusRequired || args.requireActorVisible)
          });
        }

        const stopped = stopPie ? await tools.stop_pie.run({}) : null;
        const success = move?.success !== false && inspection?.success !== false && (!screenshot || screenshot.success !== false);
        return { success, targetLocation, startPie: started, move, inspection, screenshot, stopPie: stopped };
      }
    },
    run_pie_test: {
      permission: "run",
      description: "Run a JSON playtest manifest from the Tests directory.",
      run: async (args = {}) => {
        const testName = args.test || args.name;
        const testPath = path.isAbsolute(testName)
          ? testName
          : path.join(config.testsDir, testName.endsWith(".json") ? testName : `${testName}.json`);
        const manifest = JSON.parse(fs.readFileSync(testPath, "utf8"));
        const results = [];
        if (manifest.map) {
          results.push({ action: "load_level", result: await tools.load_level.run({ levelPath: manifest.map }) });
        }
        for (const step of manifest.steps || []) {
          if (step.action === "startPie") {
            results.push({ action: step.action, result: await tools.start_pie.run({}) });
          } else if (step.action === "stopPie") {
            results.push({ action: step.action, result: await tools.stop_pie.run({}) });
          } else if (step.action === "wait") {
            await new Promise((resolve) => setTimeout(resolve, Number(step.ms || 500)));
            results.push({ action: step.action, waitedMs: Number(step.ms || 500) });
          } else if (step.action === "movePawn") {
            results.push({ action: step.action, result: await runMovePawn(backends, step.location || [0, 0, 120]) });
          } else if (step.action === "checkpoint") {
            results.push({ action: step.action, label: step.label, result: await runCheckpoint(backends, step) });
          } else if (step.action === "screenshot") {
            results.push({ action: step.action, result: await tools.capture_viewport_screenshot.run(step) });
          }
        }
        events.emit("playtest.completed", { test: manifest.name || testName, resultCount: results.length });
        return { success: true, test: manifest.name || testName, results };
      }
    }
  };
  return tools;
}
