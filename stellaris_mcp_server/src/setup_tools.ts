// New-game setup tools: the empire designs the main menu offers, the galaxy settings, and starting
// a game (in one call: stellaris_new_game).
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import fs from "node:fs";
import path from "node:path";
import { PipeClient } from "./pipe_client.js";
import { DesignChanges, Entry, applyChanges, editUserFile, findDesignText, parse, quote, topBlocks, unquote } from "./design_text.js";
import { gameWait, sleep } from "./game_wait.js";

// the file the game reads one design from (a VFS name: the user folder)
const SCRATCH = "mcp_empire_design.txt";

// the galaxy sizes (map\setup_scenarios: name and the empire counts the galaxy setup allows) and
// shapes (map\galaxy) as the game's files define them
interface GalaxySize {
  template: string;
  stars?: number;
  empires: { min?: number; max?: number; default?: number };
  fallen_empires: { max?: number; default?: number };
  marauder_empires: { max?: number; default?: number };
  nomad_empires: { max?: number; default?: number };
  advanced_empires_default?: number;
}

function galaxyFiles(gameDir: string): { sizes: GalaxySize[]; shapes: string[] } {
  const num = (block: Entry[], key: string) => {
    const e = block.find((x) => x.key !== null && unquote(x.key) === key && typeof x.value === "string");
    return e ? Number(e.value) : undefined;
  };
  const sub = (block: Entry[], key: string) => {
    const e = block.find((x) => x.key !== null && unquote(x.key) === key && typeof x.value !== "string");
    return (e?.value as Entry[]) ?? [];
  };
  const sizes: GalaxySize[] = [];
  const dir = path.join(gameDir, "map", "setup_scenarios");
  for (const f of fs.existsSync(dir) ? fs.readdirSync(dir).filter((x) => x.endsWith(".txt")) : []) {
    for (const top of parse(fs.readFileSync(path.join(dir, f), "utf-8"))) {
      if (top.key !== "setup_scenario" || typeof top.value === "string") continue;
      const s = top.value;
      const name = s.find((x) => x.key === "name" && typeof x.value === "string");
      if (!name) continue;
      sizes.push({
        template: unquote(name.value as string),
        stars: num(s, "num_stars"),
        empires: { min: num(sub(s, "num_empires"), "min"), max: num(sub(s, "num_empires"), "max"), default: num(s, "num_empire_default") },
        fallen_empires: { max: num(s, "fallen_empire_max"), default: num(s, "fallen_empire_default") },
        marauder_empires: { max: num(s, "marauder_empire_max"), default: num(s, "marauder_empire_default") },
        nomad_empires: { max: num(s, "nomad_empire_max"), default: num(s, "nomad_empire_default") },
        advanced_empires_default: num(s, "advanced_empire_default"),
      });
    }
  }
  sizes.sort((a, b) => (a.stars ?? 0) - (b.stars ?? 0));
  const shapes: string[] = [];
  const gdir = path.join(gameDir, "map", "galaxy");
  for (const f of fs.existsSync(gdir) ? fs.readdirSync(gdir).filter((x) => x.endsWith(".txt")) : []) {
    shapes.push(...topBlocks(fs.readFileSync(path.join(gdir, f), "utf-8")).map((b) => b.key));
  }
  return { sizes, shapes };
}

// the galaxy setup's limits for these settings with that size: an error message, or null
function checkGalaxy(settings: Record<string, unknown>, current: Record<string, unknown>, files: { sizes: GalaxySize[]; shapes: string[] }) {
  if (settings.template !== undefined && settings.template !== null && !files.sizes.some((s) => s.template === settings.template)) {
    return `template is one of: ${files.sizes.map((s) => s.template).join(", ")}`;
  }
  if (settings.shape !== undefined && settings.shape !== null && !files.shapes.includes(settings.shape as string)) {
    return `shape is one of: ${files.shapes.join(", ")}`;
  }
  const template = (settings.template ?? current.template) as string | null;
  const size = files.sizes.find((s) => s.template === template);
  if (!size) return null;
  const within = (key: string, max?: number, min = 0) => {
    const v = settings[key];
    if (typeof v !== "number") return null;
    return v < min || (max !== undefined && v > max) ? `${key} for a ${size.template} galaxy is ${min}..${max ?? "?"}` : null;
  };
  const empires = settings.num_empires ?? current.num_empires ?? size.empires.default;
  return within("num_empires", size.empires.max, size.empires.min ?? 0)
    ?? within("num_fallen_empires", size.fallen_empires.max)
    ?? within("num_marauder_empires", size.marauder_empires.max)
    ?? within("num_nomad_empires", size.nomad_empires.max)
    ?? within("num_advanced_empires", typeof empires === "number" ? empires : undefined);
}

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

  // one design made by the game from script (base + changes, or text; no base and no text: from
  // the game's defaults), validated by it, and kept as a user design when valid. Throws on refusal
  interface SaveArgs {
    key: string;
    base?: string;
    changes?: DesignChanges;
    text?: string;
    validate_only?: boolean;
    overwrite?: boolean;
    from_defaults?: boolean;
  }
  async function saveDesign({ key, base, changes, text, validate_only, overwrite, from_defaults }: SaveArgs) {
    const s = await where();
    if (s.screen !== "main_menu") throw new Error(`Empire designs are edited on the main menu (screen: ${s.screen}); stellaris_exit_to_main_menu`);
    let replace = !!overwrite;
    let source = text;
    if (source === undefined) {
      const existing = await client.request("get_empire_design", { key });
      const from = base ?? (existing && !existing.error && existing.type === "user" ? key : undefined);
      if (from) {
        const t = await designText(from);
        if (t.disallow_editing) throw new Error(`"${from}" does not allow editing (disallow_editing)`);
        if (from === key && t.type === "user") replace = true;
        source = t.text;
      } else if (from_defaults) {
        source = `${quote(key)} = { }`;
      } else {
        throw new Error(`"${key}" is not a user design: pass base (a design to start from) or text`);
      }
    } else if (parse(source).filter((e) => e.key !== null && typeof e.value !== "string").length !== 1) {
      throw new Error("text must be one design block: key = { ... }");
    }
    const block = applyChanges(source, key, changes ?? {});
    const scratch = path.join(s.user_dir, SCRATCH);
    let r: any;
    try {
      fs.writeFileSync(scratch, block + "\n", "utf-8");
      r = await client.request("save_empire_design", { file: SCRATCH, validate_only: !!validate_only, overwrite: replace });
    } finally {
      try {
        fs.unlinkSync(scratch);
      } catch {
        // already gone
      }
    }
    if (r?.error) throw new Error(r.error);
    if (r.saved) {
      try {
        editUserFile(s.user_file, key, block);
        r.file = s.user_file;
      } catch (err: any) {
        throw new Error(`The game keeps "${key}" for this session, but writing ${s.user_file} failed: ${err.message}`);
      }
    }
    r.text = block;
    return r;
  }

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
    ruler_name: z.string().optional().describe("Ruler's name"),
    ruler_gender: z.string().optional().describe("Ruler's gender: male, female, indeterminable"),
    ruler_portrait: z.string().optional().describe("Ruler's portrait key, e.g. human_female_05 (default: one of the species' portrait)"),
    ruler_trait: z.string().optional().describe("Ruler's starting trait key, e.g. trait_ruler_eye_for_talent"),
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
    async (args) => {
      try {
        return ok(await saveDesign(args));
      } catch (err: any) {
        return fail(err.message);
      }
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

  const { screen, closeStartScreen } = gameWait(client);

  server.tool(
    "stellaris_get_new_game_options",
    "Everything a new game can be set to, in one call (Layer 1): galaxy sizes (template, stars, the empire counts the galaxy setup allows and their defaults) and shapes from the game's map files, the current single-player galaxy settings (null = the size's own value), and the choices of the named settings (difficulty, aggressiveness, crisis_type, scaling, technology_difficulty_scale) with their in-game names. Empires: stellaris_list_empire_designs, or give one inline to stellaris_new_game.",
    {},
    async () => {
      try {
        const s = await where();
        const g = await client.request("get_galaxy_settings");
        if (g?.error) return fail(g.error);
        return ok({ ...galaxyFiles(s.game_dir), ...g });
      } catch (err: any) {
        return fail(err.message);
      }
    }
  );

  const galaxySchema = z.record(z.string(), z.union([z.string(), z.number(), z.boolean(), z.null()])).describe(
    "Galaxy settings by the game's settings-file names (stellaris_get_new_game_options lists them all); null resets one to the galaxy size's value. " +
    "template (size: tiny, small, medium, large, huge), shape, num_empires (AI empires), num_advanced_empires, num_fallen_empires, num_marauder_empires, num_nomad_empires, " +
    "num_guaranteed_colonies, colonizable_planet_odds (habitable worlds, 1.0 = normal), primitive_odds, crises (crisis strength), technology / traditions (cost, 1.0 = normal), " +
    "resource_abundance, num_wormhole_pairs, num_gateways, num_hyperlanes, naval_capacity_mult, mid_game_start / end_game_start / victory_year (years), " +
    "difficulty (civilian .. grand_admiral), aggressiveness (normal, high, low), crisis_type (any, prethoryn, unbidden, contingency, synth_queen, all), scaling (off, mid, late), " +
    "technology_difficulty_scale (off, low, normal, high, extreme), advanced_starts_near_player, clustered, ironman, caravaneers_enabled, lgate_enabled ..."
  );

  async function setGalaxy(settings: Record<string, unknown>) {
    const s = await where();
    const g = await client.request("get_galaxy_settings");
    if (g?.error) throw new Error(g.error);
    const problem = checkGalaxy(settings, g.settings ?? {}, galaxyFiles(s.game_dir));
    if (problem) throw new Error(problem);
    const r = await client.request("set_galaxy_settings", { settings });
    if (r?.error) throw new Error(r.error);
    return r;
  }

  server.tool(
    "stellaris_set_galaxy_settings",
    "Set single-player galaxy settings for the next new game (main menu), as the galaxy setup's pickers do: written into the game's settings, which the new game reads. Checked against the galaxy size's limits first; nothing is written when one is wrong. Returns all settings after the change.",
    { settings: galaxySchema },
    async ({ settings }) => {
      try {
        return ok(await setGalaxy(settings));
      } catch (err: any) {
        return fail(err.message);
      }
    }
  );

  server.tool(
    "stellaris_new_game",
    "Start a new single-player game in one call (main menu): set the galaxy (galaxy, optional), pick or create the empire (empire), launch, and wait until the game is in (start screen closed, game paused). " +
    "empire: an existing design's key (stellaris_list_empire_designs), \"random\" (the game's random empire, the default), or a whole empire inline: { key?, base?, text?, ...the fields of stellaris_save_empire_design's changes }. " +
    "Without base or text an inline empire starts from the game's defaults, so give every part (species_class, portrait, name_list, traits, ethics, authority, civics, origin, name, species names, ship_set ...). " +
    "It is saved as a user design (key default mcp_empire, replaced each time) once the game's validation accepts it; when the game refuses it nothing starts and the error carries the game's reason. Galaxy generation takes seconds (tiny) to minutes (huge).",
    {
      empire: z.union([
        z.string(),
        z.object({
          key: z.string().optional().describe("Design key to save it under (default mcp_empire)"),
          base: z.string().optional().describe("Design to start from instead of the game's defaults"),
          text: z.string().optional().describe("Full design script instead of base"),
        }).catchall(z.unknown()),
      ]).optional().describe("Design key, \"random\", or an inline empire"),
      galaxy: galaxySchema.optional(),
      wait: z.boolean().optional().describe("Wait until the game is in (default true)"),
    },
    async ({ empire, galaxy, wait }) => {
      try {
        let s = await screen();
        if (s.screen !== "main_menu") return fail(`A new game starts from the main menu (screen: ${s.screen}); stellaris_exit_to_main_menu`);
        let key = "random";
        let design: any = null;
        if (empire && typeof empire === "object") {
          const { key: k, base, text, ...changes } = empire as Record<string, any>;
          key = k ?? "mcp_empire";
          design = await saveDesign({ key, base, text, changes: changes as DesignChanges, overwrite: true, from_defaults: true });
          if (!design.saved) {
            return fail(`The game refuses the empire: ${design.invalid_reason ?? design.note ?? "invalid"}\n(script checked:\n${design.text})`);
          }
        } else if (typeof empire === "string" && empire) {
          key = empire;
        }
        const galaxyAfter = galaxy && Object.keys(galaxy).length ? (await setGalaxy(galaxy)).settings : undefined;
        const r = await client.request("start_new_game", { empire: key });
        if (!r?.success) return fail(r?.error ?? "The new game did not start");
        if (wait === false) return ok({ ...r, waiting: false });

        // the main menu launches on its next frame, then generates the galaxy (screen loading)
        const started = Date.now();
        let sawLoading = false;
        while (Date.now() - started < 900_000) {
          await sleep(1000);
          try {
            s = await screen();
          } catch {
            continue;
          }
          if (s.screen === "loading") sawLoading = true;
          if (s.screen === "in_game") {
            try {
              await client.request("finish_load");
            } catch {
              // cleared again when the game next leaves for the main menu
            }
            const startScreen = await closeStartScreen();
            let status: any = null, gov: any = null;
            try {
              status = await client.request("get_status");
              gov = await client.request("get_government");
            } catch {
              // the game is in; these can follow
            }
            // the empire the game made, to check against what was asked for
            const playing = gov && !gov.error ? {
              species: status?.species?.founder_species_name,
              ethics: (gov.ethics ?? []).map((e: any) => e.key),
              civics: (gov.civics ?? []).map((c: any) => c.key),
              authority: gov.authority, government: gov.government_type, origin: gov.origin,
              ruler: gov.ruler?.name,
            } : undefined;
            return ok({ started: true, empire: key, design_file: design?.file, screen: "in_game", start_screen: startScreen,
                        date: status?.date?.formatted, paused: status?.is_paused, playing, galaxy: galaxyAfter,
                        seconds: Math.round((Date.now() - started) / 1000) });
          }
          if (s.screen === "main_menu" && !s.quick_start_pending && !sawLoading && Date.now() - started > 15000) {
            return fail("The game did not start the new game and stayed on the main menu");
          }
        }
        return fail("Timed out waiting for the new game");
      } catch (err: any) {
        return fail(err.message);
      }
    }
  );
}
