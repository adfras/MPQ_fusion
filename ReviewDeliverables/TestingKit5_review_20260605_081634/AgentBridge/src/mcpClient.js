import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { config } from "./config.js";

function normalizeResult(result) {
  const text = result?.content?.map((entry) => entry.text ?? "").join("\n") ?? "";
  for (const candidate of [result?.structuredContent?.result, result?.structuredContent, text.split(/\n\n/).at(-1), text]) {
    if (!candidate) {
      continue;
    }
    if (typeof candidate === "object") {
      return candidate;
    }
    try {
      return JSON.parse(candidate);
    } catch {
      // Some MCP tools return summary strings.
    }
  }
  return { text };
}

export class McpBackend {
  constructor(definition, events) {
    this.definition = definition;
    this.events = events;
    this.client = null;
    this.transport = null;
    this.startedAt = null;
    this.lastError = null;
    this.toolsCache = null;
  }

  get status() {
    return {
      name: this.definition.name,
      connected: Boolean(this.client),
      startedAt: this.startedAt,
      lastError: this.lastError
    };
  }

  async connect() {
    if (this.client) {
      return;
    }
    this.transport = new StdioClientTransport({
      command: this.definition.command,
      args: this.definition.args,
      cwd: this.definition.toolRoot,
      env: {
        ...process.env,
        UE_PROJECT_PATH: config.projectFile,
        MCP_AUTOMATION_PORT: this.definition.port || process.env.MCP_AUTOMATION_PORT || "",
        MCP_AUTOMATION_REQUEST_TIMEOUT_MS: process.env.MCP_AUTOMATION_REQUEST_TIMEOUT_MS || "30000",
        LOG_LEVEL: process.env.LOG_LEVEL || "error"
      }
    });
    this.client = new Client({ name: `testingkit-${this.definition.name.toLowerCase()}-bridge`, version: "0.1.0" }, { capabilities: {} });
    try {
      await this.client.connect(this.transport);
      this.startedAt = new Date().toISOString();
      this.lastError = null;
      this.events.emit("mcp.connected", { backend: this.definition.name });
    } catch (error) {
      this.client = null;
      this.transport = null;
      this.lastError = String(error?.message || error);
      this.events.emit("mcp.failed", { backend: this.definition.name, error: this.lastError });
      throw error;
    }
  }

  async call(tool, args = {}, timeout = 60000) {
    await this.connect();
    this.events.emit("mcp.tool.started", { backend: this.definition.name, tool, args });
    try {
      const result = await this.client.callTool({ name: tool, arguments: args }, undefined, { timeout });
      const payload = normalizeResult(result);
      this.events.emit("mcp.tool.completed", { backend: this.definition.name, tool, payload });
      return {
        success: !result?.isError && payload?.success !== false,
        payload,
        text: result?.content?.map((entry) => entry.text ?? "").join("\n") ?? ""
      };
    } catch (error) {
      this.lastError = String(error?.message || error);
      this.events.emit("mcp.tool.failed", { backend: this.definition.name, tool, error: this.lastError });
      return { success: false, error: this.lastError };
    }
  }

  async listTools(options = {}) {
    await this.connect();
    if (this.toolsCache && !options.refresh) {
      this.events.emit("mcp.tools.list.completed", { backend: this.definition.name, count: this.toolsCache.tools.length, cached: true });
      return this.toolsCache;
    }

    this.events.emit("mcp.tools.list.started", { backend: this.definition.name });
    try {
      const result = await this.client.listTools();
      const toolList = result?.tools || [];
      this.events.emit("mcp.tools.list.completed", { backend: this.definition.name, count: toolList.length });
      this.toolsCache = {
        success: true,
        tools: toolList.map((tool) => ({
          name: tool.name,
          description: tool.description || "",
          inputSchema: tool.inputSchema || null
        }))
      };
      return this.toolsCache;
    } catch (error) {
      this.lastError = String(error?.message || error);
      this.events.emit("mcp.tools.list.failed", { backend: this.definition.name, error: this.lastError });
      return { success: false, error: this.lastError };
    }
  }

  async close() {
    if (!this.client) {
      return;
    }
    try {
      await this.client.close();
    } finally {
      this.client = null;
      this.transport = null;
      this.events.emit("mcp.closed", { backend: this.definition.name });
    }
  }
}

export function createMcpBackends(events) {
  return {
    chir24: new McpBackend(config.chiR24, events),
    flopperam: new McpBackend(config.flopperam, events)
  };
}
