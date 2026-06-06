import http from "node:http";
import { URL } from "node:url";
import { config, ensureBridgeDirs } from "./config.js";
import { EventHub } from "./events.js";
import { CodexAgent } from "./codexAgent.js";
import { createMcpBackends } from "./mcpClient.js";
import { createToolRegistry } from "./unrealTools.js";

ensureBridgeDirs();

const events = new EventHub();
const codex = new CodexAgent(events);
const backends = createMcpBackends(events);
const tools = createToolRegistry(backends, events);
const pendingApprovals = new Map();
let lastTurn = null;
let nextTurnNumber = 1;

function sendJson(response, status, body) {
  response.writeHead(status, {
    "Content-Type": "application/json",
    "Access-Control-Allow-Origin": "http://127.0.0.1",
    "Access-Control-Allow-Headers": "content-type",
    "Access-Control-Allow-Methods": "GET,POST,OPTIONS"
  });
  response.end(JSON.stringify(body, null, 2));
}

function compact(value, maxLength = 220) {
  const text = String(value ?? "").replace(/\s+/g, " ").trim();
  return text.length > maxLength ? `${text.slice(0, maxLength - 3)}...` : text;
}

function commandName(command = "") {
  const text = String(command || "");
  const toolMatch = text.match(/\\"tool\\"\s*:\s*\\"([^"]+)\\"/) || text.match(/"tool"\s*:\s*"([^"]+)"/);
  if (toolMatch) {
    return `POST /tool ${toolMatch[1]}`;
  }
  const knownTools = [
    "inspect_scene",
    "capture_visual_checkpoint",
    "analyze_screenshot",
    "create_color_material",
    "setup_blueprint_components",
    "connect_blueprint_pins",
    "connect_blueprint_pins_batch",
    "run_pie_overlap_test",
    "list_mcp_tools",
    "call_chir24_mcp",
    "call_flopperam_mcp",
    "run_unreal_python",
    "capture_viewport_screenshot",
    "get_editor_state",
    "get_level_actors",
    "inspect_blueprint",
    "compile_blueprint",
    "save_asset",
    "start_pie",
    "stop_pie"
  ];
  const knownTool = knownTools.find((tool) => text.includes(tool));
  if (knownTool) {
    return `POST /tool ${knownTool}`;
  }
  if (text.includes("/status")) {
    return "GET /status";
  }
  if (text.includes("/chat/latest")) {
    return "GET /chat/latest";
  }
  return compact(text, 160);
}

function summarizeTraceEvent(event) {
  switch (event.type) {
    case "chat.turn.started":
      return `Started request: ${compact(event.message, 180)}`;
    case "chat.turn.completed":
      return event.status === "completed"
        ? "Codex turn completed"
        : `Codex turn ${event.status}${event.error ? `: ${compact(event.error, 160)}` : ""}`;
    case "codex.item.completed": {
      const item = event.event?.item;
      if (item?.type === "agent_message") {
        return `Plan: ${compact(item.text, 260)}`;
      }
      if (item?.type === "command_execution") {
        return `Command finished: ${commandName(item.command)} (${item.status || "done"})`;
      }
      return null;
    }
    case "codex.item.started": {
      const item = event.event?.item;
      if (item?.type === "command_execution") {
        return `Command started: ${commandName(item.command)}`;
      }
      return null;
    }
    case "tool.started":
      return `Tool started: ${event.tool}`;
    case "tool.completed": {
      const ok = event.result?.success !== false;
      return `Tool ${ok ? "completed" : "failed"}: ${event.tool}${ok ? "" : ` - ${compact(event.result?.error || event.result?.text, 140)}`}`;
    }
    case "mcp.connected":
      return `MCP connected: ${event.backend}`;
    case "mcp.failed":
      return `MCP failed: ${event.backend} - ${compact(event.error, 160)}`;
    case "mcp.tool.started":
      return `MCP tool started: ${event.backend}.${event.tool}`;
    case "mcp.tool.completed": {
      const ok = event.payload?.success !== false;
      return `MCP tool ${ok ? "completed" : "failed"}: ${event.backend}.${event.tool}`;
    }
    case "mcp.tool.failed":
      return `MCP tool failed: ${event.backend}.${event.tool} - ${compact(event.error, 160)}`;
    case "mcp.tools.list.started":
      return `MCP tool discovery started: ${event.backend}`;
    case "mcp.tools.list.completed":
      return `MCP tool discovery ${event.cached ? "served from cache" : "completed"}: ${event.backend} (${event.count} tools)`;
    case "mcp.tools.list.failed":
      return `MCP tool discovery failed: ${event.backend} - ${compact(event.error, 160)}`;
    case "codex.turn.failed":
      return `Codex failed: ${compact(event.error, 180)}`;
    case "codex.turn.cancelled":
      return "Codex turn cancelled";
    default:
      return null;
  }
}

function traceBody(sinceSeq = 0) {
  if (String(sinceSeq).toLowerCase() === "now") {
    return { success: true, events: [], nextSeq: events.currentSeq() };
  }
  const traceEvents = events.recent(sinceSeq)
    .map((event) => ({
      seq: event.seq,
      time: event.time,
      type: event.type,
      line: summarizeTraceEvent(event)
    }))
    .filter((event) => event.line);
  return {
    success: true,
    events: traceEvents,
    nextSeq: traceEvents.length ? traceEvents[traceEvents.length - 1].seq : Number(sinceSeq) || 0
  };
}

function readJson(request) {
  return new Promise((resolve, reject) => {
    let data = "";
    request.on("data", (chunk) => {
      data += chunk;
      if (data.length > 8 * 1024 * 1024) {
        reject(new Error("Request body is too large"));
        request.destroy();
      }
    });
    request.on("end", () => {
      if (!data.trim()) {
        resolve({});
        return;
      }
      try {
        resolve(JSON.parse(data));
      } catch (error) {
        reject(error);
      }
    });
    request.on("error", reject);
  });
}

function statusBody() {
  return {
    success: true,
    bridge: {
      host: config.host,
      port: config.port,
      projectRoot: config.projectRoot,
      projectFile: config.projectFile,
      logPath: events.logPath
    },
    codex: {
      threadId: codex.threadId,
      busy: Boolean(codex.abortController),
      lastTurn: publicTurn(lastTurn)
    },
    mcp: {
      chiR24: backends.chir24.status,
      flopperam: backends.flopperam.status
    },
    tools: Object.fromEntries(Object.entries(tools).map(([name, tool]) => [name, {
      permission: tool.permission,
      description: tool.description
    }])),
    pendingApprovals: [...pendingApprovals.keys()]
  };
}

function publicTurn(turn) {
  if (!turn) {
    return null;
  }
  return {
    id: turn.id,
    status: turn.status,
    message: turn.message,
    threadId: turn.threadId || null,
    finalResponse: turn.finalResponse || "",
    error: turn.error || "",
    startedAt: turn.startedAt,
    completedAt: turn.completedAt || null
  };
}

function startChatTurn(message, options = {}) {
  if (codex.abortController) {
    return {
      success: false,
      busy: true,
      message: "Codex is already working.",
      turn: publicTurn(lastTurn)
    };
  }

  const turn = {
    id: `turn_${Date.now()}_${nextTurnNumber++}`,
    status: "running",
    message,
    startedAt: new Date().toISOString(),
    completedAt: null,
    finalResponse: "",
    error: "",
    threadId: codex.threadId
  };
  lastTurn = turn;
  events.emit("chat.turn.started", { turnId: turn.id, message });

  codex.run(message, options)
    .then((result) => {
      turn.threadId = result.threadId || codex.threadId || turn.threadId;
      turn.status = result.success ? "completed" : "failed";
      turn.finalResponse = result.finalResponse || "";
      turn.error = result.error || "";
      turn.completedAt = new Date().toISOString();
      events.emit("chat.turn.completed", { turnId: turn.id, status: turn.status, error: turn.error });
    })
    .catch((error) => {
      turn.status = "failed";
      turn.error = String(error?.message || error);
      turn.completedAt = new Date().toISOString();
      events.emit("chat.turn.completed", { turnId: turn.id, status: turn.status, error: turn.error });
    });

  return {
    success: true,
    accepted: true,
    message: "Codex is working.",
    turn: publicTurn(turn)
  };
}

async function runTool(name, args = {}, approval = {}) {
  const tool = tools[name];
  if (!tool) {
    return { success: false, error: `Unknown tool: ${name}` };
  }
  const needsApproval = tool.permission === "destructive" || args.requiresApproval === true;
  if (needsApproval && !approval.approved) {
    const id = `approval_${Date.now()}_${Math.random().toString(16).slice(2)}`;
    pendingApprovals.set(id, { name, args });
    events.emit("approval.required", { id, tool: name, args });
    return { success: false, pendingApproval: id, message: `Approval required for ${name}` };
  }
  events.emit("tool.started", { tool: name, args });
  const result = await tool.run(args);
  events.emit("tool.completed", { tool: name, result });
  return result;
}

async function handleRequest(request, response) {
  const url = new URL(request.url, `http://${config.host}:${config.port}`);
  if (request.method === "OPTIONS") {
    sendJson(response, 200, { success: true });
    return;
  }
  if (request.method === "GET" && (url.pathname === "/" || url.pathname === "/status")) {
    sendJson(response, 200, statusBody());
    return;
  }
  if (request.method === "GET" && url.pathname === "/events") {
    events.attachSse(response);
    return;
  }
  if (request.method === "GET" && url.pathname === "/trace/latest") {
    sendJson(response, 200, traceBody(url.searchParams.get("since") || 0));
    return;
  }
  if (request.method === "GET" && url.pathname === "/chat/latest") {
    sendJson(response, 200, { success: true, turn: publicTurn(lastTurn) });
    return;
  }
  if (request.method === "POST" && url.pathname === "/chat") {
    const body = await readJson(request);
    const result = startChatTurn(String(body.message || ""), {
      screenshotPath: body.screenshotPath
    });
    sendJson(response, result.success ? 202 : 409, result);
    return;
  }
  if (request.method === "POST" && url.pathname === "/cancel") {
    const success = codex.cancel();
    if (success && lastTurn?.status === "running") {
      lastTurn.status = "cancelled";
      lastTurn.completedAt = new Date().toISOString();
    }
    sendJson(response, 200, { success, turn: publicTurn(lastTurn) });
    return;
  }
  if (request.method === "POST" && url.pathname === "/tool") {
    const body = await readJson(request);
    const result = await runTool(String(body.tool || ""), body.args || {}, body.approval || {});
    sendJson(response, result?.success === false ? 400 : 200, result);
    return;
  }
  if (request.method === "POST" && url.pathname === "/approve") {
    const body = await readJson(request);
    const pending = pendingApprovals.get(body.id);
    if (!pending) {
      sendJson(response, 404, { success: false, error: "Unknown approval id" });
      return;
    }
    pendingApprovals.delete(body.id);
    if (!body.approved) {
      events.emit("approval.denied", { id: body.id, tool: pending.name });
      sendJson(response, 200, { success: false, denied: true });
      return;
    }
    events.emit("approval.granted", { id: body.id, tool: pending.name });
    const result = await runTool(pending.name, pending.args, { approved: true });
    sendJson(response, 200, result);
    return;
  }
  sendJson(response, 404, { success: false, error: "Not found" });
}

const server = http.createServer((request, response) => {
  handleRequest(request, response).catch((error) => {
    events.emit("server.error", { error: String(error?.stack || error) });
    sendJson(response, 500, { success: false, error: String(error?.message || error) });
  });
});

server.listen(config.port, config.host, () => {
  events.emit("server.started", { host: config.host, port: config.port });
  console.log(`Codex Agent Bridge listening on http://${config.host}:${config.port}`);
});

process.on("SIGINT", async () => {
  events.emit("server.stopping");
  await Promise.allSettled([backends.chir24.close(), backends.flopperam.close()]);
  server.close(() => process.exit(0));
});
