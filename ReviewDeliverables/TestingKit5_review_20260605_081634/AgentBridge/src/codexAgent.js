import fs from "node:fs";
import { Codex } from "@openai/codex-sdk";
import { config } from "./config.js";

export class CodexAgent {
  constructor(events) {
    this.events = events;
    this.codex = new Codex({
      config: {
        sandbox_workspace_write: { network_access: true }
      }
    });
    this.thread = null;
    this.threadId = null;
    this.abortController = null;
  }

  ensureThread() {
    if (!this.thread) {
      this.thread = this.codex.startThread({
        workingDirectory: config.projectRoot,
        skipGitRepoCheck: true,
        sandboxMode: "workspace-write",
        networkAccessEnabled: true,
        approvalPolicy: "on-request",
        additionalDirectories: [config.projectRoot]
      });
    }
    return this.thread;
  }

  buildInput(message, options = {}) {
    const instructions = fs.existsSync(config.agentInstructionsPath)
      ? fs.readFileSync(config.agentInstructionsPath, "utf8")
      : "";
    const text = [
      "Use the following project instructions when helping from the Unreal Editor bridge.",
      instructions,
      "The local bridge exposes HTTP routes on http://127.0.0.1:8765.",
      "Use GET /status for bridge state. Use POST /tool for Unreal actions instead of searching the repository for bridge documentation.",
      "POST /tool body format: {\"tool\":\"inspect_scene\",\"args\":{\"filename\":\"before.png\",\"includeActors\":false}}.",
      "Use efficient wrapper tools first: inspect_scene for map state + PlayerStart + actor summary + screenshot; capture_visual_checkpoint for screenshots with visual stats and before/after comparison; analyze_screenshot for existing images; create_color_material for simple visible materials; setup_blueprint_components for deterministic SCS component template repair; connect_blueprint_pins_batch or connect_blueprint_pins for ChiR24 pin wiring with the correct UE 5.7 argument names; run_pie_overlap_test for moving the pawn, waiting for ticks, inspecting component state, screenshotting, and stopping PIE.",
      "Useful lower-level tools include get_editor_state, get_level_actors, get_selected_actor, inspect_blueprint, compile_blueprint, start_pie, stop_pie, run_pie_test, list_mcp_tools, call_chir24_mcp, and call_flopperam_mcp.",
      "Visual workflow: capture a before screenshot with inspect_scene or capture_visual_checkpoint before edits; after placement/material changes capture another screenshot with compareTo set to the before path; after PIE/runtime interaction capture a final screenshot and compare. For placement or actor-state proof, pass focusActor, actorLabel, or focusLocation so the screenshot camera frames the target. If visualAnalysis.image.skyLikeRatio is high or warnings mention a sky-heavy shot, retake with an explicit focus target or camera.",
      "For Blueprint asset/component/Event Graph authoring, do not conclude the bridge cannot do it until you have tried MCP tool discovery and the raw MCP tools. Start with list_mcp_tools for backend chir24 only once unless refresh is needed. For raw ChiR24 calls, use {tool:\"call_chir24_mcp\",args:{mcpTool:\"manage_blueprint\",arguments:{action:\"...\"}}}; the bridge also accepts tool/toolName plus root-level action fields, but nested arguments are clearest. ChiR24 manage_blueprint supports create_blueprint, add_component/add_scs_component, get_graph_details, list_node_types, create_node, connect_pins, set_pin_default_value, compile, and related graph actions. Flopperam can also be tried through call_flopperam_mcp for create_blueprint, add_component_to_blueprint, blueprint graph node creation, pin connection, compile, and spawn actor flows.",
      "Known efficient argument shapes from prior traces: use connect_blueprint_pins_batch/connect_blueprint_pins or manage_blueprint connect_pins with fromPinName/toPinName, not fromPin/toPin; create materials with create_color_material or with separate name/path fields, not assetPath alone; variable get nodes need variable-specific fields such as variableName; for SetMaterial use memberClass /Script/Engine.PrimitiveComponent; for SetIntensity use memberClass /Script/Engine.LightComponent; for MoveComponentTo and PrintString use /Script/Engine.KismetSystemLibrary.",
      "Known bad routes from recent traces: do not attempt component-bound overlap nodes that inspect as None (None) unless explicitly required; for multi-component SCS actors, use setup_blueprint_components directly for component creation/repair instead of repeated add_scs_component/set_scs_transform retries; inspect_blueprint accepts blueprintPath/assetPath/name, so do not fall back to raw MCP solely because you used the wrong path alias once; do not call TestingKitPlayerController.get_pawn() from Unreal Python, use GameplayStatics.get_player_pawn(world, 0); do not sleep inside Unreal Python while waiting for latent Blueprint movement because it blocks game ticking.",
      "For overlap tests, place floor-level interactables at floor Z near 0 in the Third Person template, keep PlayerStart outside the trigger initially, set purely visual meshes to NoCollision, move the pawn to capsule-center Z around 92 during PIE, and verify material/color/movement changes with run_pie_overlap_test or screenshot comparison.",
      "Avoid no-op smoke tests like `print('python bridge ok')` when another real operation can validate the same route.",
      "When a tool fails, report the tool/action/error briefly, then try the next reasonable route before giving up. Do not save a known-broken asset unless the user explicitly asks for partial output.",
      "Avoid scanning AgentBridge/node_modules, Intermediate, Binaries, DerivedDataCache, and Saved unless the user explicitly asks for those files.",
      "User request:",
      message
    ].join("\n\n");
    const input = [{ type: "text", text }];
    if (options.screenshotPath && fs.existsSync(options.screenshotPath)) {
      input.push({ type: "local_image", path: options.screenshotPath });
    }
    return input;
  }

  async run(message, options = {}) {
    const thread = this.ensureThread();
    this.abortController = new AbortController();
    const items = [];
    let finalResponse = "";
    this.events.emit("codex.turn.started", { message });
    try {
      const { events } = await thread.runStreamed(this.buildInput(message, options), {
        signal: this.abortController.signal
      });
      for await (const event of events) {
        if (event.type === "thread.started") {
          this.threadId = event.thread_id;
        }
        if (event.type === "item.completed" || event.type === "item.updated" || event.type === "item.started") {
          items.push(event.item);
        }
        if (event.type === "item.completed" && event.item?.type === "agent_message") {
          finalResponse = event.item.text;
        }
        this.events.emit(`codex.${event.type}`, { event });
      }
      this.events.emit("codex.turn.completed", { threadId: this.threadId, finalResponse });
      return { success: true, threadId: this.threadId, finalResponse, items };
    } catch (error) {
      const messageText = String(error?.message || error);
      this.events.emit("codex.turn.failed", { error: messageText });
      return { success: false, error: messageText, threadId: this.threadId, finalResponse, items };
    } finally {
      this.abortController = null;
    }
  }

  cancel() {
    if (this.abortController) {
      this.abortController.abort();
      this.events.emit("codex.turn.cancelled");
      return true;
    }
    return false;
  }
}
