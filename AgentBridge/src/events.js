import fs from "node:fs";
import path from "node:path";
import { config, ensureBridgeDirs, nowStamp } from "./config.js";

export class EventHub {
  constructor() {
    ensureBridgeDirs();
    this.clients = new Set();
    this.ring = [];
    this.nextSeq = 1;
    this.logPath = path.join(config.logsDir, `agent_bridge_${nowStamp()}.jsonl`);
  }

  emit(type, payload = {}) {
    const event = {
      seq: this.nextSeq++,
      time: new Date().toISOString(),
      type,
      ...payload
    };
    this.ring.push(event);
    if (this.ring.length > 500) {
      this.ring.shift();
    }
    fs.appendFileSync(this.logPath, `${JSON.stringify(event)}\n`);
    const encoded = `event: ${type}\ndata: ${JSON.stringify(event)}\n\n`;
    for (const client of this.clients) {
      client.write(encoded);
    }
    return event;
  }

  recent(sinceSeq = 0) {
    const minSeq = Number(sinceSeq) || 0;
    return this.ring.filter((event) => Number(event.seq || 0) > minSeq);
  }

  currentSeq() {
    return this.nextSeq - 1;
  }

  attachSse(response) {
    response.writeHead(200, {
      "Content-Type": "text/event-stream",
      "Cache-Control": "no-cache",
      Connection: "keep-alive",
      "Access-Control-Allow-Origin": "http://127.0.0.1"
    });
    response.write("\n");
    this.clients.add(response);
    for (const event of this.ring.slice(-50)) {
      response.write(`event: replay\ndata: ${JSON.stringify(event)}\n\n`);
    }
    response.on("close", () => this.clients.delete(response));
  }
}
