// What waits for the player, surfaced without being asked for:
//   - every tool's result gets an [attention] note while event windows wait (an event pauses the
//     game, and an agent busy with other tools would otherwise only notice much later);
//   - stellaris_run_until lets time pass and stops at the first thing that needs the player.
// Both read the bridge's get_attention (paused, speed, date, pause_reason, pending_events).
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { PipeClient } from "./pipe_client.js";
import { sleep } from "./game_wait.js";

// tools whose own result already says it
const NO_NOTE = new Set(["stellaris_get_status", "stellaris_get_active_events", "stellaris_run_until"]);

async function attention(client: PipeClient): Promise<any | null> {
  try {
    const s = await client.request("get_screen");
    if (s?.screen !== "in_game") return null;
    return await client.request("get_attention");
  } catch {
    return null;
  }
}

function note(a: any): string | null {
  const events: any[] = a?.pending_events ?? [];
  if (!events.length) return null;
  const list = events.map((e) => ({ window_id: e.window_id, kind: e.kind, event_key: e.event_key, title: e.title }));
  const paused = a.paused ? `game paused (${a.pause_reason})` : "game running";
  return `[attention] ${events.length} event window(s) waiting, ${paused}: ${JSON.stringify(list)} -> stellaris_get_active_events, stellaris_resolve_event`;
}

// wrap server.tool so that every handler registered after this adds the note
export function withAttentionNotes(server: McpServer, client: PipeClient) {
  const original = server.tool.bind(server) as (...args: any[]) => any;
  (server as any).tool = (...args: any[]) => {
    const name = args[0];
    const handler = args[args.length - 1];
    if (typeof name === "string" && typeof handler === "function" && !NO_NOTE.has(name)) {
      args[args.length - 1] = async (...callArgs: any[]) => {
        const result = await handler(...callArgs);
        const text = note(await attention(client));
        if (text && result && Array.isArray(result.content)) {
          result.content = [...result.content, { type: "text", text }];
        }
        return result;
      };
    }
    return original(...args);
  };
}

// "2200.01.01" -> days since 0.01.01 (the game's years are 12 months of 30 days)
function dayIndex(date?: string): number | null {
  const m = /^(\d+)\.(\d+)\.(\d+)$/.exec(date ?? "");
  return m ? Number(m[1]) * 360 + (Number(m[2]) - 1) * 30 + (Number(m[3]) - 1) : null;
}

export function registerAttentionTools(server: McpServer, client: PipeClient) {
  const ok = (v: unknown) => ({ content: [{ type: "text" as const, text: JSON.stringify(v, null, 2) }] });
  const fail = (msg: string) => ({ isError: true, content: [{ type: "text" as const, text: `Error: ${msg}` }] });

  server.tool(
    "stellaris_run_until",
    "Let game time pass until something needs the player, then return with the game paused. Unpauses (at speed, when given) and stops at the first of: an event window opening (stopped_because \"event\"), the game pausing itself or being paused (\"paused\": pause_reason popup / player), the date after days or until_date (\"date\"), or max_seconds of real time (\"time_limit\"). Does not start while event windows wait (\"pending_events\": resolve them first). Returns the dates, days passed, why it stopped and the event windows waiting (details: stellaris_get_active_events).",
    {
      days: z.number().int().min(1).max(36000).optional().describe("Game days to run (30 = a month, 360 = a year)"),
      until_date: z.string().optional().describe("Run until this game date, YYYY.MM.DD"),
      speed: z.number().int().min(0).max(4).optional().describe("Game speed while running, 0 (slowest) .. 4 (fastest); default: as it is"),
      max_seconds: z.number().int().min(1).max(600).optional().describe("Real-time limit (default 60)"),
    },
    async ({ days, until_date, speed, max_seconds }) => {
      try {
        let a = await attention(client);
        if (!a) return fail("No game is in (stellaris_load_game / stellaris_new_game)");
        const start = a.date as string | undefined;
        if (a.pending_events?.length) {
          return ok({ stopped_because: "pending_events", date: start, paused: a.paused, pause_reason: a.pause_reason,
                      pending_events: a.pending_events, hint: "Resolve the waiting events first (stellaris_get_active_events, stellaris_resolve_event)" });
        }
        const from = dayIndex(start);
        let target = until_date !== undefined ? dayIndex(until_date) : days !== undefined && from !== null ? from + days : null;
        if (until_date !== undefined && target === null) return fail("until_date is YYYY.MM.DD");
        if (target !== null && from !== null && target <= from) return ok({ stopped_because: "date", date: start, days_passed: 0, paused: a.paused });

        if (speed !== undefined) await client.request("set_speed", { speed });
        const r = await client.request("set_paused", { paused: false });
        if (r?.success === false) return fail(r.error ?? "Could not unpause");

        const limit = Date.now() + (max_seconds ?? 60) * 1000;
        let reason = "time_limit";
        await sleep(300);
        while (Date.now() < limit) {
          const now = await attention(client);
          if (!now) {
            reason = "left_game";
            break;
          }
          a = now;
          if (a.pending_events?.length) {
            reason = "event";
            break;
          }
          if (a.paused) {
            reason = "paused";
            break;
          }
          const d = dayIndex(a.date);
          if (target !== null && d !== null && d >= target) {
            reason = "date";
            break;
          }
          await sleep(250);
        }
        // stop time where it stopped (an event may not pause by the player's message settings)
        const pausedItself = !!a.paused;
        const reasonBefore = a.pause_reason;
        if (reason !== "left_game" && !a.paused) {
          await client.request("set_paused", { paused: true });
          a = (await attention(client)) ?? a;
        }
        const end = dayIndex(a.date);
        return ok({
          stopped_because: reason,
          date_start: start,
          date: a.date,
          days_passed: from !== null && end !== null ? end - from : undefined,
          paused: a.paused,
          // whether the game had stopped on its own (an event or popup pausing it) or this tool paused it
          game_paused_itself: pausedItself,
          pause_reason: pausedItself ? reasonBefore : "run_until",
          pending_events: a.pending_events ?? [],
          ...(a.pending_events?.length ? { hint: "stellaris_get_active_events for the texts and options, stellaris_resolve_event to answer" } : {}),
        });
      } catch (err: any) {
        return fail(err.message);
      }
    }
  );
}
