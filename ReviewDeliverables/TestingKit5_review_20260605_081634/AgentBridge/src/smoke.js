import fs from "node:fs";
import { config, ensureBridgeDirs } from "./config.js";
import { EventHub } from "./events.js";
import { createMcpBackends } from "./mcpClient.js";
import { createToolRegistry } from "./unrealTools.js";

ensureBridgeDirs();
const events = new EventHub();
const backends = createMcpBackends(events);
const tools = createToolRegistry(backends, events);

console.log(JSON.stringify({
  success: true,
  projectRoot: config.projectRoot,
  projectFile: config.projectFile,
  projectFileExists: fs.existsSync(config.projectFile),
  bridgeRoot: config.bridgeRoot,
  toolCount: Object.keys(tools).length,
  tools: Object.keys(tools)
}, null, 2));
