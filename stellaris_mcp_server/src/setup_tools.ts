// New-game setup tools: the empire designs the main menu offers (and, next, the galaxy settings
// and starting a game).
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import fs from "node:fs";
import path from "node:path";
import { PipeClient } from "./pipe_client.js";
import { DesignChanges, applyChanges, editUserFile, findDesignText, parse } from "./design_text.js";

// the file the game reads one design from (a VFS name: the user folder)
const SCRATCH = "mcp_empire_design.txt";

interface Screen {
  screen: string;
  user_dir: string;
  game_dir: string;
  user_design_file: string;
}

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
  const fail = (message: string) => ({ isError: true, content: [{ type: "text" as const, text: `Error: ${message}` }] });
  const ok = (body: unknown) => ({ content: [{ type: "text" as const, text: JSON.stringify(body, null, 2) }] });

  const where = async () => {
    const s: Screen = await client.request("get_screen");
    if (!s.user_dir || !s.user_design_file) throw new Error("The game's user folder or user design file is unknown");
    return { ...s, user_file: path.join(s.user_dir, s.user_design_file) };
  };
  // a design's script: from the user design file, or the game's prescripted_countries
  const designText = async (key: string) => {
    const s = await where();
    const d = await client.request("get_empire_design", { key });
    if (d?.error) throw new Error(d.error);
    const found = findDesignText(s.user_file, s.game_dir, key, d.type);
    if (!found) {
      throw new Error(`The script of "${key}" is not in ${d.type === "user" ? s.user_file : "the game's prescripted_countries"} (a mod's empire?); pass text`);
    }
    return { ...found, type: d.type as "user" | "prescripted", disallow_editing: !!d.disallow_editing };
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

  server.tool(
    "stellaris_get_empire_design_text",
    "One empire design as game script (Layer 2): the block the game reads it from, out of the user design file or the game's prescripted_countries. Edit it and pass it as text to stellaris_save_empire_design for what the structured changes do not cover (secondary species, ruler, flag, playable conditions ...).",
    { key: z.string().describe("Design key from stellaris_list_empire_designs") },
    async ({ key }) => {
      try {
        const t = await designText(key);
        return ok({ key, type: t.type, file: t.file, text: t.text });
      } catch (err: any) {
        return fail(err.message);
      }
    }
  );

  const changesSchema = z.object({
    name: z.string().optional().describe("Empire name (literal text)"),
    adjective: z.string().optional().describe("Empire adjective (literal text)"),
    ethics: z.array(z.string()).optional().describe("Ethic keys, e.g. ethic_fanatic_egalitarian, ethic_xenophile"),
    civics: z.array(z.string()).optional().describe("Civic keys"),
    authority: z.string().optional().describe("Authority key, e.g. auth_democratic"),
    government: z.string().optional().describe("Government key"),
    origin: z.string().optional().describe("Origin key, e.g. origin_default"),
    species_class: z.string().optional().describe("Species class key, e.g. HUM, MAM, ROBOT"),
    portrait: z.string().optional().describe("Portrait key of that class, e.g. human"),
    species_name: z.string().optional().describe("Species name (literal text)"),
    species_plural: z.string().optional().describe("Species plural (literal text)"),
    species_adjective: z.string().optional().describe("Species adjective (literal text)"),
    name_list: z.string().optional().describe("Name list key, e.g. HUMAN1"),
    gender: z.string().optional().describe("Species gender: male, female, indeterminable or not_set"),
    traits: z.array(z.string()).optional().describe("All the species' trait keys: replaces the list, so keep its organic / robotic trait (e.g. trait_organic)"),
    planet_class: z.string().optional().describe("Home planet class key, e.g. pc_continental"),
    planet_name: z.string().optional().describe("Home planet name (literal text)"),
    system_name: z.string().optional().describe("Home system name (literal text)"),
    initializer: z.string().optional().describe("Home system initializer key (empty: a random system)"),
    ship_prefix: z.string().optional().describe("Ship name prefix (literal text)"),
    ship_set: z.string().optional().describe("Ship set (graphical culture) key, e.g. humanoid_01"),
    city_set: z.string().optional().describe("City set key"),
    room: z.string().optional().describe("Ruler's room key"),
    advisor_voice: z.string().optional().describe("Advisor voice key"),
    spawn: z.enum(["yes", "no", "always"]).optional().describe("Spawn this empire as an AI in new games: yes, no, always"),
  });

  server.tool(
    "stellaris_save_empire_design",
    "Create or edit a user empire design (Layer 2 write, main menu). Starts from base (any design, prescripted or user; by default key itself when that is a user design, edited in place) with changes applied, or from the full script in text (stellaris_get_empire_design_text). The game reads the result the way it reads its user design file, runs its fix-ups and its validation (IsEmpireDesignValid, the empire editor's check); only a valid design is kept, and it is written into the user design file without rewriting the other designs. validate_only: check, do not save. Returns the design's summary, valid, invalid_reason (the game's text), saved, and the script that was checked.",
    {
      key: z.string().describe("Key of the design to save (a new key creates one; a prescripted empire's key is refused)"),
      base: z.string().optional().describe("Design to start from (default: key, when it is a user design)"),
      changes: changesSchema.optional().describe("Fields to set; a list replaces the whole list"),
      text: z.string().optional().describe("The design's full script, one block `key = { ... }`, instead of base (changes still apply)"),
      validate_only: z.boolean().optional().describe("Check with the game's validation and do not save"),
      overwrite: z.boolean().optional().describe("Replace an existing user design with this key (implied when editing it in place)"),
    },
    async ({ key, base, changes, text, validate_only, overwrite }) => {
      let s: Awaited<ReturnType<typeof where>>;
      let block: string;
      let replace = !!overwrite;
      try {
        s = await where();
        if (s.screen !== "main_menu") return fail(`Empire designs are edited on the main menu (screen: ${s.screen}); stellaris_exit_to_main_menu`);
        let source = text;
        if (source === undefined) {
          const existing = await client.request("get_empire_design", { key });
          const from = base ?? (existing && !existing.error && existing.type === "user" ? key : undefined);
          if (!from) return fail(`"${key}" is not a user design: pass base (a design to start from) or text`);
          const t = await designText(from);
          if (t.disallow_editing) return fail(`"${from}" does not allow editing (disallow_editing)`);
          if (from === key && t.type === "user") replace = true;
          source = t.text;
        } else if (parse(source).filter((e) => e.key !== null && typeof e.value !== "string").length !== 1) {
          return fail("text must be one design block: key = { ... }");
        }
        block = applyChanges(source, key, (changes ?? {}) as DesignChanges);
      } catch (err: any) {
        return fail(err.message);
      }
      const scratch = path.join(s.user_dir, SCRATCH);
      let r: any;
      try {
        fs.writeFileSync(scratch, block + "\n", "utf-8");
        r = await client.request("save_empire_design", { file: SCRATCH, validate_only: !!validate_only, overwrite: replace });
      } catch (err: any) {
        return fail(err.message);
      } finally {
        try {
          fs.unlinkSync(scratch);
        } catch {
          // already gone
        }
      }
      if (r?.error) return fail(r.error);
      if (r.saved) {
        try {
          editUserFile(s.user_file, key, block);
          r.file = s.user_file;
        } catch (err: any) {
          return fail(`The game keeps "${key}" for this session, but writing ${s.user_file} failed: ${err.message}`);
        }
      }
      r.text = block;
      return ok(r);
    }
  );

  server.tool(
    "stellaris_delete_empire_design",
    "Delete a user empire design (main menu): the game drops it, and its block is removed from the user design file (the file before the change is kept next to it as .mcp_backup). Prescripted empires cannot be deleted.",
    { key: z.string().describe("User design key from stellaris_list_empire_designs") },
    async ({ key }) => {
      try {
        const s = await where();
        if (s.screen !== "main_menu") return fail(`Empire designs are edited on the main menu (screen: ${s.screen})`);
        const r = await client.request("delete_empire_design", { key });
        if (r?.error) return fail(r.error);
        const removed = editUserFile(s.user_file, key, null);
        return ok({ ...r, file: s.user_file, removed_from_file: removed });
      } catch (err: any) {
        return fail(err.message);
      }
    }
  );
}
