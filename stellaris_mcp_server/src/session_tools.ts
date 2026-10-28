// Session tools: which screen the game is on, the save games, loading, saving and the main menu.
// Loading takes seconds to minutes, longer than one pipe request: these tools poll the bridge's
// get_screen (answered even while the game loads) until the game is where it should be.
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import fs from "node:fs";
import path from "node:path";
import { PipeClient } from "./pipe_client.js";
import { continueTarget, listCampaignSaves, listCampaigns, savesRoot } from "./saves.js";

const START_SCREEN_EVENT_ID = 0xffff0001;
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

interface Screen {
  screen: "main_menu" | "loading" | "in_game" | "unknown";
  quick_start_pending: boolean;
  user_dir: string;
}

export function registerSessionTools(server: McpServer, client: PipeClient) {
  const text = (v: unknown) => ({ content: [{ type: "text" as const, text: JSON.stringify(v, null, 2) }] });
  const fail = (msg: string) => ({ isError: true, content: [{ type: "text" as const, text: `Error: ${msg}` }] });
  const screen = async (): Promise<Screen> => client.request("get_screen");

  // wait until the screen is one of `want`; null on timeout
  async function waitFor(want: string[], timeoutMs: number): Promise<Screen | null> {
    const end = Date.now() + timeoutMs;
    while (Date.now() < end) {
      try {
        const s = await screen();
        if (want.includes(s.screen)) return s;
      } catch {
        // the pipe can be briefly busy while the game swaps screens
      }
      await sleep(1000);
    }
    return null;
  }

  // after a load: close the start screen (the opening event window of a new game) as its button
  // does; a save past the opening has none
  async function closeStartScreen(): Promise<"closed" | "not_shown" | "failed"> {
    for (let i = 0; i < 3; i++) {
      try {
        const ev = await client.request("get_active_events");
        const list: any[] = Array.isArray(ev) ? ev : ev?.events ?? [];
        if (list.some((e) => e?.window_id === START_SCREEN_EVENT_ID)) {
          const r = await client.request("resolve_event", { window_id: START_SCREEN_EVENT_ID, option_index: 0 });
          return r?.success ? "closed" : "failed";
        }
      } catch {
        // not ready yet
      }
      await sleep(1000);
    }
    return "not_shown";
  }

  server.tool(
    "stellaris_list_saves",
    "Save games on disk (Layer 1, read from the save files' headers; the game is not touched). Without campaign: every campaign folder, newest first, with its number of saves and its newest save's empire name, game date and ironman flag, plus continue (the save the main menu's Continue opens). With campaign: that campaign's saves, newest first, each with game version, empire name, game date and ironman. Load one with stellaris_load_game.",
    { campaign: z.string().optional().describe("A campaign folder from the list without campaign") },
    async ({ campaign }) => {
      try {
        const s = await screen();
        if (!s.user_dir) return fail("The game's user folder is unknown");
        if (!campaign) {
          return text({ saves_folder: savesRoot(s.user_dir), continue: continueTarget(s.user_dir), campaigns: listCampaigns(s.user_dir) });
        }
        const saves = listCampaignSaves(s.user_dir, campaign);
        if (!saves.length) return fail(`No saves in campaign ${campaign}`);
        return text({ campaign, saves: saves.map(({ file, modified, meta }) => ({ file, modified, ...meta })) });
      } catch (err: any) {
        return fail(err.message);
      }
    }
  );

  server.tool(
    "stellaris_load_game",
    "Loads a save, from the main menu or from inside a game (which is left for the main menu first, as the game does). Without arguments it continues: the save the main menu's Continue button opens (else the newest save). With campaign only: that campaign's newest save. With campaign and file: that save (stellaris_list_saves). The result's source says which rule picked it (continue; newest when Continue's save no longer exists). By default it waits until the game is loaded (screen in_game) and closes the start screen, so the next call can play; wait=false only starts the load (follow it with stellaris_get_status: screen loading -> in_game). The game refuses a save made for another version or other DLCs: the main menu stays and the tool says so.",
    {
      campaign: z.string().optional().describe("Campaign folder (stellaris_list_saves)"),
      file: z.string().optional().describe("Save file in that campaign, with or without .sav"),
      wait: z.boolean().optional().default(true).describe("Wait until the game is loaded (default true)"),
    },
    async ({ campaign, file, wait }) => {
      try {
        let s = await screen();
        if (!s.user_dir) return fail("The game's user folder is unknown");
        let target: { campaign: string; file: string } | null = null;
        // which rule picked the save: given, campaign_newest, continue (the main menu's Continue),
        // newest (Continue's save is gone, e.g. an autosave the game rotated out)
        let source = "given";
        if (campaign && file) {
          target = { campaign, file: file.toLowerCase().endsWith(".sav") ? file : `${file}.sav` };
        } else if (campaign) {
          const saves = listCampaignSaves(s.user_dir, campaign, false);
          if (!saves.length) return fail(`No saves in campaign ${campaign}`);
          target = { campaign, file: saves[0].file };
          source = "campaign_newest";
        } else {
          target = continueTarget(s.user_dir);
          source = "continue";
          if (!target) {
            const c = listCampaigns(s.user_dir)[0];
            if (!c) return fail("There are no saves");
            target = { campaign: c.campaign, file: c.latest_file };
            source = "newest";
          }
        }
        if (!fs.existsSync(path.join(savesRoot(s.user_dir), target.campaign, target.file))) {
          return fail(`No such save: ${target.campaign}/${target.file}`);
        }
        if (s.screen === "loading") s = (await waitFor(["main_menu", "in_game"], 600_000)) ?? s;
        if (s.screen === "in_game") {
          const r = await client.request("exit_to_main_menu");
          if (!r?.success) return fail(r?.error ?? "Could not leave the game");
          if (!(await waitFor(["main_menu"], 180_000))) return fail("The game did not reach the main menu");
        }
        const r = await client.request("load_save", { campaign: target.campaign, file: target.file });
        if (!r?.success) return fail(r?.error ?? "The load did not start");
        if (!wait) return text({ ...r, waiting: false });

        // the main menu takes the save on its next frame; then it loads (screen loading) and
        // switches to the game. If it never starts, the game refused the save
        const started = Date.now();
        let sawLoading = false;
        while (Date.now() - started < 900_000) {
          await sleep(1000);
          let now: Screen;
          try {
            now = await screen();
          } catch {
            continue;
          }
          if (now.screen === "loading") sawLoading = true;
          if (now.screen === "in_game") {
            try {
              await client.request("finish_load");
            } catch {
              // the request is cleared again when the game next leaves for the main menu
            }
            const startScreen = await closeStartScreen();
            let status: any = null;
            try {
              status = await client.request("get_status");
            } catch {
              // the game is in; the status can follow
            }
            return text({ loaded: `${target.campaign}/${target.file}`, source, screen: "in_game", start_screen: startScreen,
                          date: status?.date?.formatted, paused: status?.is_paused });
          }
          if (now.screen === "main_menu" && !now.quick_start_pending && !sawLoading && Date.now() - started > 8000) {
            return fail(`The game did not load ${target.campaign}/${target.file}: it refused the save (another game version or other DLCs?) and stayed on the main menu`);
          }
        }
        return fail("Timed out waiting for the save to load");
      } catch (err: any) {
        return fail(err.message);
      }
    }
  );

  server.tool(
    "stellaris_save_game",
    "Saves the current game into its campaign folder (as the game's save menu; a local save). The file is written over the next moments; the result says when it is on disk. A name that exists is refused unless overwrite is set.",
    {
      name: z.string().optional().describe("File name without .sav (default: the game date, e.g. 2354.07.01)"),
      overwrite: z.boolean().optional().default(false).describe("Replace a save of the same name"),
    },
    async ({ name, overwrite }) => {
      try {
        const s = await screen();
        if (s.screen !== "in_game") return fail(`Saving needs a game in progress (screen: ${s.screen})`);
        let fileName = name;
        if (!fileName) {
          const status = await client.request("get_status");
          fileName = status?.date?.formatted ?? "save";
        }
        const before = Date.now();
        const r = await client.request("save_game", { name: fileName, overwrite: overwrite ?? false });
        if (!r?.success) return fail(r?.error ?? "The game refused the save");
        // the campaign folder is the one the game writes into: wait for the file to appear there
        const root = savesRoot(s.user_dir);
        for (let i = 0; i < 60; i++) {
          await sleep(500);
          for (const c of fs.existsSync(root) ? fs.readdirSync(root) : []) {
            const f = path.join(root, c, r.name);
            if (fs.existsSync(f) && fs.statSync(f).mtimeMs >= before - 2000) {
              return text({ saved: `${c}/${r.name}`, campaign: c, file: r.name });
            }
          }
        }
        return text({ ...r, note: "The save was started but the file has not appeared yet" });
      } catch (err: any) {
        return fail(err.message);
      }
    }
  );

  server.tool(
    "stellaris_exit_to_main_menu",
    "Leaves the current game for the main menu (as the in-game menu's resign/exit; an ironman game is saved first, any other is not: save with stellaris_save_game before). Waits until the main menu is shown unless wait=false.",
    { wait: z.boolean().optional().default(true).describe("Wait until the main menu is shown (default true)") },
    async ({ wait }) => {
      try {
        const r = await client.request("exit_to_main_menu");
        if (!r?.success) return fail(r?.error ?? "Could not leave the game");
        if (!wait) return text(r);
        const s = await waitFor(["main_menu"], 180_000);
        return s ? text({ screen: "main_menu" }) : fail("The game did not reach the main menu");
      } catch (err: any) {
        return fail(err.message);
      }
    }
  );
}
