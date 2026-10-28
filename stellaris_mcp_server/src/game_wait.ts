// Waiting on the game across screens: loads and new galaxies take seconds to minutes, longer than
// one pipe request, so the tools poll the bridge's get_screen (answered even while the game loads).
import { PipeClient } from "./pipe_client.js";

export const START_SCREEN_EVENT_ID = 0xffff0001;
export const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

export interface Screen {
  screen: "main_menu" | "loading" | "in_game" | "unknown";
  quick_start_pending: boolean;
  user_dir: string;
  game_dir: string;
  user_design_file: string;
}

export function gameWait(client: PipeClient) {
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

  // once in game: close the start screen (the opening event window of a new game) as its button
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

  return { screen, waitFor, closeStartScreen };
}
