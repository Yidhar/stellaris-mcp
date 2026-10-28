// New-game setup tools: the empire designs the main menu offers (and, next, the galaxy settings
// and starting a game).
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { PipeClient } from "./pipe_client.js";

export function registerSetupTools(server: McpServer, client: PipeClient) {
  const call = async (method: string, params: Record<string, unknown> = {}) => {
    try {
      const result = await client.request(method, params);
      if (result && typeof result === "object" && "error" in result && Object.keys(result).length <= 2) {
        return { isError: true, content: [{ type: "text" as const, text: `Error: ${result.error}` }] };
      }
      return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
    } catch (err: any) {
      return { isError: true, content: [{ type: "text" as const, text: `Error: ${err.message}` }] };
    }
  };

  server.tool(
    "stellaris_list_empire_designs",
    "The empires a new game can be played as (Layer 1): the prescripted empires and the user's saved designs, each with its key, name, type (prescripted / user), species name, ethics, origin and playable (the design's own playable condition), and on the main menu valid (the game's validation for playing it; the game's empire list hides prescripted designs that fail either). Works on the main menu and in game. Details and why a design is invalid: stellaris_get_empire_design.",
    {},
    async () => call("list_empire_designs")
  );

  server.tool(
    "stellaris_get_empire_design",
    "One empire design in full (Layer 2): names, species (class, portrait, names, name list, gender, traits) and a secondary species when it has one, ethics, authority, government, civics, origin, home world (planet class, names, system initializer), ship prefix, ship and city sets, room, advisor voice, and for a user design its spawn setting. On the main menu also valid and, when not, invalid_reason: the game's own validation for playing it (IsEmpireDesignValid, the check the empire list and the empire editor use, with their messages).",
    { key: z.string().describe("Design key from stellaris_list_empire_designs") },
    async ({ key }) => call("get_empire_design", { key })
  );
}
