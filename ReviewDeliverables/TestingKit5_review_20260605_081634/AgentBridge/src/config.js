import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const bridgeRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const projectRoot = path.resolve(bridgeRoot, "..");

export const config = {
  host: process.env.CODEX_AGENT_HOST || "127.0.0.1",
  port: Number(process.env.CODEX_AGENT_PORT || 8765),
  bridgeRoot,
  projectRoot,
  projectFile: process.env.UE_PROJECT_PATH || path.join(projectRoot, "TestingKit5.uproject"),
  savedRoot: path.join(projectRoot, "Saved", "CodexAgent"),
  screenshotsDir: path.join(projectRoot, "Saved", "CodexAgent", "Screenshots"),
  logsDir: path.join(projectRoot, "Saved", "CodexAgent", "Logs"),
  testsDir: path.join(projectRoot, "Tests"),
  agentInstructionsPath: path.join(projectRoot, "AGENTS.md"),
  chiR24: {
    name: "ChiR24",
    toolRoot: path.join(projectRoot, "_MCPBench", "Tools", "ChiR24-Unreal_mcp"),
    command: "node",
    args: ["dist/cli.js"],
    port: process.env.MCP_AUTOMATION_PORT || "8091"
  },
  flopperam: {
    name: "Flopperam",
    toolRoot: path.join(projectRoot, "_MCPBench", "Tools", "flopperam-unreal-engine-mcp", "Python"),
    command: "uv",
    args: ["run", "unreal_mcp_server_advanced.py"]
  }
};

export function ensureBridgeDirs() {
  for (const dir of [config.savedRoot, config.screenshotsDir, config.logsDir]) {
    fs.mkdirSync(dir, { recursive: true });
  }
}

export function nowStamp() {
  return new Date().toISOString().replace(/[-:]/g, "").replace(/\..+/, "").replace("T", "_");
}
