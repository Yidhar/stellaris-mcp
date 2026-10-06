import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { PipeClient } from "./pipe_client.js";
import { registerTools } from "./tools.js";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

// <plugin folder>\logs\mcp_server.log beside the bridge's log (this file is mcp-server\dist\index.js);
// in the repository that is <repo>\logs\
const LOG_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "logs");

function log(msg: string) {
  try {
    fs.mkdirSync(LOG_DIR, { recursive: true });
    fs.appendFileSync(path.join(LOG_DIR, "mcp_server.log"), `[${new Date().toISOString()}] ${msg}\n`);
  } catch {}
}

async function main() {
  log("Starting Stellaris MCP server...");
  const server = new McpServer({
    name: "stellaris-mcp-bridge",
    version: "1.0.0",
  });

  const client = new PipeClient();

  // Register all Stellaris tools
  registerTools(server, client);
  log("Tools registered.");

  // Connect over standard stdio transport for MCP client
  const transport = new StdioServerTransport();
  await server.connect(transport);

  console.error("[Stellaris MCP Server] Running on stdio.");
  log("Connected to stdio transport.");

  // Graceful shutdown
  process.on("SIGINT", () => {
    log("SIGINT received.");
    client.close();
    process.exit(0);
  });
  process.on("SIGTERM", () => {
    log("SIGTERM received.");
    client.close();
    process.exit(0);
  });
}

main().catch((err) => {
  console.error("[Stellaris MCP Server] Fatal error:", err);
  log(`Fatal error: ${err?.stack || err}`);
  process.exit(1);
});
