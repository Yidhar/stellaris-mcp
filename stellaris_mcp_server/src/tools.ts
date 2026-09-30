import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { PipeClient } from "./pipe_client.js";

export function registerTools(server: McpServer, client: PipeClient) {
  // Tool 1: stellaris_get_status
  server.tool(
    "stellaris_get_status",
    "Retrieves current Stellaris game status (Layer 1 Global Perception in progressive disclosure architecture). Includes session active state, pause state, speed (0-4), date, empire statistics (empire_size, colonies, starbases, naval_capacity), empire resources (stockpile/income/expense/net for all 26 resources), situation_log indicators, council indicators, society indicators, leaders indicators, market indicators (is_galactic_market, market_fee_percent), discoveries indicators (held_relics_count, can_activate_relic), and contacts indicators (known_empires_count, pending_first_contacts_count).",
    {},
    async () => {
      try {
        const result = await client.request("get_status");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting status: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 2: stellaris_set_paused
  server.tool(
    "stellaris_set_paused",
    "Pauses or unpauses the Stellaris game deterministically via native CPauseCommand.",
    {
      paused: z.boolean().describe("True to pause the game, false to resume simulation."),
    },
    async ({ paused }) => {
      try {
        const result = await client.request("set_paused", { paused });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error setting pause state: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 3: stellaris_set_speed
  server.tool(
    "stellaris_set_speed",
    "Adjusts the game simulation speed using native game speed commands.",
    {
      speed: z
        .number()
        .int()
        .min(0)
        .max(4)
        .describe("Game speed level: 0 (Slowest/1x), 1 (Slow/2x), 2 (Normal/3x), 3 (Fast/4x), 4 (Very Fast/5x)."),
    },
    async ({ speed }) => {
      try {
        const result = await client.request("set_speed", { speed });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error setting speed: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 4: stellaris_get_active_events
  server.tool(
    "stellaris_get_active_events",
    "Retrieves all currently active pending event windows: window_id, event_key (script id), title, description, and the shown options. Each option carries `effects`, the effect tooltip the game shows on hover (icons rendered as [energy] etc.); options without immediate effects have none. Hidden options are not listed.",
    {},
    async () => {
      try {
        const result = await client.request("get_active_events");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting active events: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 5: stellaris_resolve_event
  server.tool(
    "stellaris_resolve_event",
    "Resolves an active event choice natively on the main game thread by window ID and option index, applying the decision and closing the event dialog.",
    {
      window_id: z.number().int().describe("The unique window ID of the event window (from stellaris_get_active_events)."),
      option_index: z.number().int().describe("The `index` of one of the options listed for this window by stellaris_get_active_events (indexes are not contiguous when some options are hidden; others are refused)."),
    },
    async ({ window_id, option_index }) => {
      try {
        const result = await client.request("resolve_event", { window_id, option_index });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error resolving event: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 6: stellaris_get_notifications
  server.tool(
    "stellaris_get_notifications",
    "Retrieves the active notification queue (top bar message badges), including queue index, notification type name, title key, parameter, and whether it can be clicked.",
    {},
    async () => {
      try {
        const result = await client.request("get_notifications");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting notifications: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 7: stellaris_open_notification
  server.tool(
    "stellaris_open_notification",
    "Triggers a left-click on an active top-bar notification by its queue index (0-based) on the main game thread. This opens/expands the notification into an active modal event or anomaly window and removes it from the top-bar queue.",
    {
      index: z.number().int().min(0).describe("The queue index of the notification to click and open (from stellaris_get_notifications)."),
    },
    async ({ index }) => {
      try {
        const result = await client.request("open_notification", { index });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error opening notification: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 8: stellaris_get_alerts
  server.tool(
    "stellaris_get_alerts",
    "Retrieves all currently active top-bar alerts (CAlertIconsWindow banners in the center top bar), including alert_id (0..65), internal type name, title, description, and hover tooltip.",
    {},
    async () => {
      try {
        const result = await client.request("get_alerts");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting alerts: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 9: stellaris_open_alert
  server.tool(
    "stellaris_open_alert",
    "Triggers a native left-click on an active top-bar alert banner by its alert_id on the main game thread. For First Contact alerts (alert_id: 38), this opens the First Contact narrative and stage view.",
    {
      alert_id: z.number().int().min(0).max(65).describe("The alert ID (0..65) to click and open (from stellaris_get_alerts)."),
    },
    async ({ alert_id }) => {
      try {
        const result = await client.request("open_alert", { alert_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error opening alert: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 10: stellaris_get_research_state
  server.tool(
    "stellaris_get_research_state",
    "Retrieves the complete research state across all three technological departments (physics, society, engineering), including currently active research (key, name, tier, cost) and candidate card choices (pool of drawn technologies).",
    {},
    async () => {
      try {
        const result = await client.request("get_research_state");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting research state: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 11: stellaris_select_research
  server.tool(
    "stellaris_select_research",
    "Selects a technology to research for the specified department area from the candidate pool using native CSelectTechCommand. If a technology is already currently being researched in that area, it is automatically and safely cancelled first (preserving accumulated points into stored research) before switching to the new technology.",
    {
      area: z.union([
        z.number().int().min(0).max(2).describe("0: physics, 1: society, 2: engineering"),
        z.enum(["physics", "society", "engineering"]).describe("Name of the research department"),
      ]).describe("The research area (physics, society, or engineering)."),
      tech_key: z.string().describe("The unique technology key to select and research (e.g. 'tech_fusion_power')."),
    },
    async ({ area, tech_key }) => {
      try {
        const result = await client.request("select_research", { area, tech_key });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error selecting research: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 12: stellaris_cancel_research
  server.tool(
    "stellaris_cancel_research",
    "Cancels the currently active technology research in the specified department area using native CCancelTechCommand, preserving accumulated research points into stored research.",
    {
      area: z.union([
        z.number().int().min(0).max(2).describe("0: physics, 1: society, 2: engineering"),
        z.enum(["physics", "society", "engineering"]).describe("Name of the research department"),
      ]).describe("The research area (physics, society, or engineering)."),
    },
    async ({ area }) => {
      try {
        const result = await client.request("cancel_research", { area });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error cancelling research: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Galaxy map. Everything follows the player's knowledge: all systems and hyperlanes are
  // visible (as on the in-game map); owners, planets, deposits and foreign fleets depend on the
  // engine's intel level for the system (0 none .. 4 full) and on what the player has surveyed.
  const galaxyCall = async (method: string, params: Record<string, unknown>, compact = false) => {
    try {
      const result = await client.request(method, params);
      return { content: [{ type: "text" as const, text: compact ? JSON.stringify(result) : JSON.stringify(result, null, 2) }] };
    } catch (err: any) {
      return { isError: true, content: [{ type: "text" as const, text: `Error: ${err.message}` }] };
    }
  };

  server.tool(
    "stellaris_get_galaxy_overview",
    "Galaxy overview (Layer 1): system and hyperlane totals, a topology_version (a cached stellaris_get_galaxy_map topology stays valid while it is unchanged), the player's capital system and owned systems, how many systems the player has each intel level on, surveyed / unsurveyed systems, unclaimed systems bordering the player's space, known empires with the number of their systems the player can see, and unresearched anomalies. Only what the player knows.",
    {},
    async () => galaxyCall("get_galaxy_overview", {})
  );

  server.tool(
    "stellaris_get_galaxy_map",
    "Galaxy map around a system (Layer 1.5): every system within `jumps` hyperlane jumps of center_system_id (default: the player's capital system) as table rows [id, name, x, y, owner_id, intel, jumps, flags] plus the hyperlanes between them as [a, b, length] and the names of the owners shown. flags: S surveyed, C colonized, B starbase, F own fleet present, A unresearched anomaly. owner_id is null when the system is unowned or its owner is unknown to the player. Use stellaris_get_system for one system's planets, starbase and fleets.",
    {
      center_system_id: z.number().int().optional().describe("System to center on (default: the player's capital system)"),
      jumps: z.number().int().min(0).max(12).optional().default(3).describe("Hyperlane jumps around the center (0-12, default 3)"),
    },
    async ({ center_system_id, jumps }) =>
      galaxyCall("get_galaxy_map", { ...(center_system_id !== undefined ? { center_system_id } : {}), jumps: jumps ?? 3 }, true)
  );

  server.tool(
    "stellaris_get_system",
    "One star system (Layer 2) as the player knows it: position, intel level, owner, whether it is fully surveyed, hyperlanes (neighbor id, name, length), the starbase (level) when the owner is known, planets (class, size, owner, colony, surveyed; deposits only on surveyed planets; an unresearched anomaly if the player has discovered one) when intel is medium or better or the planets are surveyed, and fleets (own fleets always, other fleets only with high intel). unknown_planets counts planets the player cannot see yet.",
    {
      system_id: z.number().int().describe("System id (from stellaris_get_galaxy_map)"),
    },
    async ({ system_id }) => galaxyCall("get_system", { system_id })
  );

  server.tool(
    "stellaris_move_fleet",
    "Orders one of the player's fleets to fly to a star system (native CSendFleetToLocationCommand, a move to the system's centre; the engine plans the route). queue=true appends the move after the fleet's current orders instead of replacing them. Rejected with the engine's reason for stations or immobile fleets. The fleet's new orders show in stellaris_get_fleets on a later call.",
    {
      fleet_id: z.number().int().describe("One of the player's fleet ids"),
      system_id: z.number().int().describe("Target system id"),
      queue: z.boolean().optional().default(false).describe("Append to the current orders instead of replacing them"),
    },
    async ({ fleet_id, system_id, queue }) => galaxyCall("move_fleet", { fleet_id, system_id, queue: queue ?? false })
  );

  server.tool(
    "stellaris_find_systems",
    "Finds systems for a purpose, nearest first by hyperlane jumps from from_system_id (default: the capital), using only what the player knows. purpose: 'unsurveyed' (systems not fully surveyed), 'outpost' (unowned systems; with a construction ship, can_build and the game's reason come from the build order's own check), 'deposit' (surveyed planets whose deposit key contains `resource`, e.g. 'minerals', 'energy', 'alloys'), 'colonizable' (the expansion planner's list: unowned surveyed planets in systems with medium+ intel, with the game's habitability for species_id (default: the founder species) and the player's modifiers, best first).",
    {
      purpose: z.enum(["unsurveyed", "outpost", "deposit", "colonizable"]).describe("What to look for"),
      species_id: z.number().int().optional().describe("colonizable: species to rate planets for (default: the founder species)"),
      from_system_id: z.number().int().optional().describe("Start system (default: the capital system)"),
      limit: z.number().int().min(1).max(50).optional().default(10).describe("Maximum systems to return"),
      fleet_id: z.number().int().optional().describe("outpost: the construction ship to check with (default: the first one)"),
      resource: z.string().optional().describe("deposit: substring of the deposit key, e.g. 'minerals'"),
    },
    async ({ purpose, from_system_id, limit, fleet_id, resource, species_id }) =>
      galaxyCall("find_systems", {
        purpose,
        ...(species_id !== undefined ? { species_id } : {}),
        ...(from_system_id !== undefined ? { from_system_id } : {}),
        ...(fleet_id !== undefined ? { fleet_id } : {}),
        limit: limit ?? 10,
        resource: resource ?? "",
      })
  );

  server.tool(
    "stellaris_find_path",
    "Shortest hyperlane route between two systems by hyperlane length (the systems on the way, jumps, length). Closed borders, gateways, wormholes and jump drives are not considered; the engine plans the actual route when stellaris_move_fleet is used.",
    {
      from_system_id: z.number().int().optional().describe("Start system (default: the capital system)"),
      to_system_id: z.number().int().describe("Destination system"),
    },
    async ({ from_system_id, to_system_id }) =>
      galaxyCall("find_path", { ...(from_system_id !== undefined ? { from_system_id } : {}), to_system_id })
  );

  server.tool(
    "stellaris_build_outpost",
    "Orders a player construction ship to build an outpost (a starbase) in a star system (native CFleetBuildOrbitalStationCommand). Rejected with the game's reason (for example the system is owned, not surveyed, not bordering your space, or influence is short). Candidates: stellaris_find_systems purpose=outpost.",
    {
      fleet_id: z.number().int().describe("The construction ship's fleet id"),
      system_id: z.number().int().describe("Target system id"),
      queue: z.boolean().optional().default(false).describe("Append to the current orders instead of replacing them"),
    },
    async ({ fleet_id, system_id, queue }) => galaxyCall("build_outpost", { fleet_id, system_id, queue: queue ?? false })
  );

  server.tool(
    "stellaris_colonize",
    "Orders a player colony ship to colonize a planet (native CFleetColonizePlanetCommand). Rejected with the game's reason (for example uninhabitable, not surveyed, outside your borders).",
    {
      fleet_id: z.number().int().describe("The colony ship's fleet id"),
      planet_id: z.number().int().describe("Target planet id"),
      queue: z.boolean().optional().default(false).describe("Append to the current orders instead of replacing them"),
    },
    async ({ fleet_id, planet_id, queue }) => galaxyCall("colonize", { fleet_id, planet_id, queue: queue ?? false })
  );

  server.tool(
    "stellaris_survey",
    "Orders a player science ship fleet to survey (native CFleetSurveyDepositHolderCommand): one planet when planet_id is given, otherwise every planet of system_id. The ship travels there first. Rejected with the engine's reason (for example no scientist, already surveyed, no access).",
    {
      fleet_id: z.number().int().describe("The science ship's fleet id"),
      system_id: z.number().int().optional().describe("System to survey completely (when planet_id is not given)"),
      planet_id: z.number().int().optional().describe("A single planet to survey"),
      queue: z.boolean().optional().default(false).describe("Append to the current orders instead of replacing them"),
    },
    async ({ fleet_id, system_id, planet_id, queue }) =>
      galaxyCall("survey", {
        fleet_id,
        ...(system_id !== undefined ? { system_id } : {}),
        ...(planet_id !== undefined ? { planet_id } : {}),
        queue: queue ?? false,
      })
  );

  // Tool 13: stellaris_get_situation_log
  server.tool(
    "stellaris_get_situation_log",
    "Retrieves the detailed situation log state, including active situations (stage progress, monthly change rate, current approach, owner country), the player's special projects (kind, days left, species for species modification / uplift) and the anomalies it has discovered but not researched (planet and anomaly category). Implements progressive disclosure (Layer 2) for F1 Situation Log.",
    {
      player_only: z
        .boolean()
        .optional()
        .default(true)
        .describe("Whether to only return situations owned by the player empire (default true). If false, returns all active situations in the galaxy."),
    },
    async ({ player_only }) => {
      try {
        const result = await client.request("get_situation_log", { player_only: player_only ?? true });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting situation log: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 14: stellaris_set_situation_approach
  server.tool(
    "stellaris_set_situation_approach",
    "Selects and adopts an approach for a situation using native CSetSituationApproachCommand (0x417D). Implements progressive disclosure (Layer 3 Action Command).",
    {
      situation_id: z
        .number()
        .int()
        .optional()
        .default(0)
        .describe("The unique ID of the situation (can be 0 or omitted to default to player country's matching situation)."),
      approach_key: z
        .string()
        .describe("The approach key string to select/adopt (e.g. 'approach_revolt_do_nothing', 'approach_voidworm_plague_do_nothing')."),
    },
    async ({ situation_id, approach_key }) => {
      try {
        const result = await client.request("set_situation_approach", {
          situation_id: situation_id ?? 0,
          approach_key,
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error setting situation approach: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 15: stellaris_get_government
  server.tool(
    "stellaris_get_government",
    "Retrieves detailed Government and Council domain state (Layer 2 Detailed Domain Query). Includes Authority (auth_democratic), Government Type (gov_representative_democracy), Origin, Civics (with keys and localized names), Ethics (fanatic/regular with keys and localized names), Ruler leader details (ID, name, class, level, age, ethic), Active Council Agenda (key, localized name, progress, cost, ready flag), and Council Seats (all positions and assigned leader details).",
    {},
    async () => {
      try {
        const result = await client.request("get_government");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting government state: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 16: stellaris_launch_council_agenda
  server.tool(
    "stellaris_launch_council_agenda",
    "Launches and activates the currently prepared council agenda using native CFinishAgendaCommand (0x2B16/0x38CA). Validates that the active agenda is ready (progress >= cost) before execution. Implements progressive disclosure (Layer 3 Action Command).",
    {},
    async () => {
      try {
        const result = await client.request("launch_council_agenda");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error launching council agenda: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // stellaris_set_council_agenda
  server.tool(
    "stellaris_set_council_agenda",
    "Starts a new council agenda via native CSetCouncilAgendaCommand (Layer 3 Action Command). Choose agenda_key from stellaris_get_government available_agendas (only agendas the engine currently accepts are listed). Finish a ready agenda with stellaris_launch_council_agenda.",
    {
      agenda_key: z.string().describe("Agenda key from available_agendas, e.g. 'agenda_chart_the_unknown'."),
    },
    async ({ agenda_key }) => {
      try {
        const result = await client.request("set_council_agenda", { agenda_key });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error setting council agenda: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // stellaris_get_civics
  server.tool(
    "stellaris_get_civics",
    "Government civics (Layer 2): adopted civics with their civic point cost, civic points (total/used/free), and every civic the game would accept added now (each checked with the engine's own government validation). Also returns `reform`: can_reform (with the game's reason, e.g. the 20-year reform cooldown), reform_cost_unity and unity_stockpile. Pass civic_key to check one civic: `requirements` lists the civic's own conditions for this empire, one per line with [trigger_yes]/[trigger_no] (ethics, authority, conflicting civics, origin), and `blocked_by` lists every obstacle (civic points, requirements, reform).",
    {
      civic_key: z.string().optional().describe("Optional civic key, e.g. 'civic_technocracy', to check just that civic."),
    },
    async ({ civic_key }) => {
      try {
        const result = await client.request("get_civics", civic_key ? { civic_key } : {});
        return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
      } catch (err: any) {
        return { isError: true, content: [{ type: "text", text: `Error getting civics: ${err.message}` }] };
      }
    }
  );

  // stellaris_change_civics
  server.tool(
    "stellaris_change_civics",
    "Reforms the government's civics via native CChangeGovernmentCommand (Layer 3 Action Command), keeping the authority: adds the civics in `add` and removes those in `remove`. Refused when the civics would cost more civic points than the empire has, or when the game rejects the reform: the error carries the game's reason, `reform` (cooldown/unity cost) and `unmet_requirements` per added civic. Any change starts the game's government reform cooldown (20 years), so check stellaris_get_civics first.",
    {
      add: z.array(z.string()).optional().describe("Civic keys to adopt, from stellaris_get_civics available_civics."),
      remove: z.array(z.string()).optional().describe("Adopted civic keys to drop."),
    },
    async ({ add, remove }) => {
      try {
        const result = await client.request("change_civics", { add: add || [], remove: remove || [] });
        return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
      } catch (err: any) {
        return { isError: true, content: [{ type: "text", text: `Error changing civics: ${err.message}` }] };
      }
    }
  );

  // stellaris_select_leader_trait
  server.tool(
    "stellaris_select_leader_trait",
    "Picks a level-up trait for a leader via native CAddTraitFromPoolCommand (Layer 3 Action Command). Offered choices are listed per leader as trait_options / trait_upgrade_options with trait_selections_available (stellaris_get_leaders, and council seats in stellaris_get_government). Rejects traits that are not currently offered.",
    {
      leader_id: z.number().int().describe("Leader ID."),
      trait_key: z.string().describe("Trait key from the leader's trait_options or trait_upgrade_options."),
    },
    async ({ leader_id, trait_key }) => {
      try {
        const result = await client.request("select_leader_trait", { leader_id, trait_key });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error selecting leader trait: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 17: stellaris_get_traditions
  server.tool(
    "stellaris_get_traditions",
    "Retrieves detailed Traditions and Ascension Perks state (Layer 2 Detailed Domain Query). Includes summary (adopted_trees_count, max_trees_count: 7, unlocked_traditions_count, next_tradition_cost, can_unlock_tradition, ascension_perk_slots_available), adopted_trees (keys, localized names, unlocked_count, is_finished), unlocked_traditions (keys, localized names, tree_key, tree_name), available_trees (keys, localized names, adopt_tradition_key, adopt_tradition_name), and ascension_perks (adopted perks, total_slots: 8, available_slots).",
    {},
    async () => {
      try {
        const result = await client.request("get_traditions");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting traditions state: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 18: stellaris_adopt_tradition
  server.tool(
    "stellaris_adopt_tradition",
    "Adopts a new tradition tree or unlocks a tradition perk within an adopted tree via native CActivateTraditionCommand (0x37C2). Implements progressive disclosure (Layer 3 Action Command).",
    {
      tradition_key: z
        .string()
        .describe("Tradition key to adopt (e.g. 'tr_expansion_adopt' to adopt the Expansion tree, or 'tr_expansion_reach_for_the_stars' for an individual tradition perk)."),
    },
    async ({ tradition_key }) => {
      try {
        const result = await client.request("adopt_tradition", { tradition_key });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error adopting tradition: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 19: stellaris_get_edicts
  server.tool(
    "stellaris_get_edicts",
    "Retrieves detailed Edicts domain state (Layer 2 Detailed Domain Query). Includes summary (active_edicts_count, edict_fund), active_edicts (keys, localized names), and available_edicts catalog (keys, localized names, is_active).",
    {},
    async () => {
      try {
        const result = await client.request("get_edicts");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting edicts state: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 20: stellaris_toggle_edict
  server.tool(
    "stellaris_toggle_edict",
    "Enables or disables an edict via native CAddEdictCommand (0x2F36) or CRemoveEdictCommand (0x3628). Implements progressive disclosure (Layer 3 Action Command).",
    {
      edict_key: z
        .string()
        .describe("The edict key to toggle (e.g. 'map_the_stars', 'subsidize_mining')."),
      enabled: z
        .boolean()
        .describe("True to enable/activate the edict, false to cancel/deactivate it."),
    },
    async ({ edict_key, enabled }) => {
      try {
        const result = await client.request("toggle_edict", { edict_key, enabled });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error toggling edict: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 21: stellaris_get_leaders
  server.tool(
    "stellaris_get_leaders",
    "Retrieves full leader and recruitment pool details (Layer 2 Domain Deep-Dive in progressive disclosure architecture). Returns summary counts, list of all hired leaders (id, localized name, class, background_job (job held before recruitment), level, age, ethic, assignment_type, target_id, hire_date), and candidate recruitment pool (id, name, class, level, age, hire_cost, ethic).",
    {},
    async () => {
      try {
        const result = await client.request("get_leaders");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting leaders: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 22: stellaris_hire_leader
  server.tool(
    "stellaris_hire_leader",
    "Recruits a leader candidate from the candidate pool via native engine CHireLeaderCommand (0x4073). Implements progressive disclosure (Layer 3 Action Command).",
    {
      candidate_id: z
        .number()
        .int()
        .positive()
        .describe("The unique candidate leader ID to recruit (from pool_candidates in stellaris_get_leaders)."),
    },
    async ({ candidate_id }) => {
      try {
        const result = await client.request("hire_leader", { candidate_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error hiring leader: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 23: stellaris_dismiss_leader
  server.tool(
    "stellaris_dismiss_leader",
    "Dismisses/fires an existing hired leader via native engine CFireLeaderCommand (0x2EB3). Implements progressive disclosure (Layer 3 Action Command). Cannot dismiss the empire ruler.",
    {
      leader_id: z
        .number()
        .int()
        .positive()
        .describe("The unique leader ID to dismiss/fire (from hired_leaders in stellaris_get_leaders)."),
    },
    async ({ leader_id }) => {
      try {
        const result = await client.request("dismiss_leader", { leader_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error dismissing leader: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 24: stellaris_assign_leader
  server.tool(
    "stellaris_assign_leader",
    "Assigns a leader to a planetary governorship, fleet/science ship, army, or council seat via native engine CAssignLeaderCommand (0x4076). Implements progressive disclosure (Layer 3 Action Command).",
    {
      leader_id: z
        .number()
        .int()
        .positive()
        .describe("The unique leader ID to assign."),
      assignment_type: z
        .number()
        .int()
        .min(1)
        .max(6)
        .describe("Assignment type: 1 = Planet Governor, 2 = Fleet / Science Ship Commander, 3 = Army General, 6 = Council Seat."),
      target_id: z
        .number()
        .int()
        .describe("Target ID: Planet ID (for governor), Fleet ID (for fleet commander), Army ID (for general), or Council Seat ID (for councilor)."),
    },
    async ({ leader_id, assignment_type, target_id }) => {
      try {
        const result = await client.request("assign_leader", {
          leader_id,
          assignment_type,
          target_id,
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error assigning leader: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 25: stellaris_get_species
  server.tool(
    "stellaris_get_species",
    "Queries species details in the player's empire or across the galaxy (Layer 2 Domain Query in progressive disclosure architecture). Returns founder species, species names, classes/portraits, traits, detailed 9-category rights configurations (citizenship, living standards, military service, slavery, purge, population controls, colonization controls, migration controls, subspecies integration), remaining cooldown days, empire pop counts, and the complete available rights catalog.",
    {
      mode: z
        .enum(["empire", "galaxy"])
        .optional()
        .describe("Query mode: 'empire' (default) returns species present in or governed by the player's empire; 'galaxy' returns all species known in the galaxy."),
      species_id: z
        .number()
        .int()
        .positive()
        .optional()
        .describe("Optional specific species ID to query. If provided, returns only this species."),
    },
    async ({ mode, species_id }) => {
      try {
        const result = await client.request("get_species", { mode, species_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting species: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 26: stellaris_set_species_rights
  server.tool(
    "stellaris_set_species_rights",
    "Modifies a species' rights configuration via native engine CSetSpeciesRightCommand (0x3832). Implements progressive disclosure (Layer 3 Action Command). Supports categories: 'citizenship', 'living_standards', 'military_service', 'slavery_type', 'purge_type', 'population_controls', 'colonization_controls', 'migration_controls', 'subspecies_integration'.",
    {
      species_id: z
        .number()
        .int()
        .positive()
        .describe("The unique species ID to modify (e.g. founder species ID from stellaris_get_status or stellaris_get_species)."),
      category: z
        .string()
        .describe("The rights category to change: 'citizenship', 'living_standards', 'military_service', 'slavery_type', 'purge_type', 'population_controls', 'colonization_controls', 'migration_controls', or 'subspecies_integration'."),
      right_value: z
        .string()
        .describe("The new right key (e.g. 'citizenship_full', 'living_standard_stratified', 'living_standard_academic', 'military_service_exempt', 'population_control_yes', etc. See available_rights_catalog in stellaris_get_species)."),
    },
    async ({ species_id, category, right_value }) => {
      try {
        const result = await client.request("set_species_rights", {
          species_id,
          category,
          right_value,
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error setting species rights: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool: stellaris_get_species_modification_info
  server.tool(
    "stellaris_get_species_modification_info",
    "Queries species genetic modification capabilities, points, picks, current traits, and available traits catalog (Layer 2 Detailed Query in progressive disclosure architecture).",
    {
      species_id: z
        .number()
        .int()
        .positive()
        .describe("Species ID to query modification traits, points, and picks for (from stellaris_get_species)."),
    },
    async ({ species_id }) => {
      try {
        const result = await client.request("get_species_modification_info", { species_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting species modification info: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool: stellaris_create_species_template
  server.tool(
    "stellaris_create_species_template",
    "Creates a new subspecies modification template with custom traits via native engine CCountryCreateSpeciesModTemplate (0x33B1). Implements progressive disclosure (Layer 3 Action Command).",
    {
      base_species_id: z
        .number()
        .int()
        .positive()
        .describe("The base species ID to derive the new subspecies template from."),
      name: z
        .string()
        .optional()
        .describe("Optional name of the new subspecies template (e.g. 'Homo Sapiens Superior'). If omitted, automatically inherits the base species name."),
      traits: z
        .array(z.string())
        .describe("List of trait keys to assign to this template (e.g. ['trait_intelligent', 'trait_rapid_breeders', 'trait_unruly'])."),
    },
    async ({ base_species_id, name, traits }) => {
      try {
        const result = await client.request("create_species_template", {
          base_species_id,
          name: name || "",
          traits,
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error creating species template: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool: stellaris_modify_species_template
  server.tool(
    "stellaris_modify_species_template",
    "Modifies an existing subspecies modification template with updated name and traits via native engine CCountryUpdateSpeciesModTemplate (0x33B0). Implements progressive disclosure (Layer 3 Action Command).",
    {
      template_species_id: z
        .number()
        .int()
        .positive()
        .describe("The existing subspecies template ID to modify."),
      name: z
        .string()
        .optional()
        .describe("Optional updated name for the template. If omitted, retains previous name."),
      traits: z
        .array(z.string())
        .describe("Complete updated list of trait keys for this template."),
    },
    async ({ template_species_id, name, traits }) => {
      try {
        const result = await client.request("modify_species_template", {
          template_species_id,
          name: name || "",
          traits,
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error modifying species template: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool: stellaris_delete_species_template
  server.tool(
    "stellaris_delete_species_template",
    "Deletes an existing owned subspecies modification template via native engine CCountryDeleteSpeciesModTemplate (0x33B2). Implements progressive disclosure (Layer 3 Action Command).",
    {
      species_id: z
        .number()
        .int()
        .positive()
        .describe("The subspecies template ID to delete."),
    },
    async ({ species_id }) => {
      try {
        const result = await client.request("delete_species_template", { species_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error deleting species template: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool: stellaris_apply_species_template
  server.tool(
    "stellaris_apply_species_template",
    "Applies a subspecies modification template to pops on owned colonies, initiating a genetic modification special project via native engine CCreateSpeciesModSpecialProjectCommand (0x4645). Implements progressive disclosure (Layer 3 Action Command).",
    {
      template_species_id: z
        .number()
        .int()
        .positive()
        .describe("The subspecies template ID to apply to population."),
      colony_ids: z
        .array(z.number().int().nonnegative())
        .optional()
        .describe("Optional list of specific colony IDs to apply this template to. If omitted, applies to all owned colonies with matching base species pops."),
    },
    async ({ template_species_id, colony_ids }) => {
      try {
        const result = await client.request("apply_species_template", {
          template_species_id,
          colony_ids: colony_ids || [],
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error applying species template: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 27: stellaris_get_fleets
  server.tool(
    "stellaris_get_fleets",
    "Queries detailed fleet and fleet template information (Layer 2 Domain Query in progressive disclosure architecture). Returns fleet ID, template ID, fleet name, military power estimate, total actual ships, total quota, reinforceable status, and breakdown of ship design items (design ID, design name like 'Corvette'/'护卫舰', actual count, target quota, deficit, and whether it can be reinforced).",
    {
      fleet_id: z
        .number()
        .int()
        .optional()
        .describe("Optional specific fleet ID to query. If omitted, returns all fleets in the empire."),
      include_civilian: z
        .boolean()
        .optional()
        .describe("Whether to include civilian fleets (science ships, construction ships, transports). Defaults to false (military fleets only)."),
    },
    async ({ fleet_id, include_civilian }) => {
      try {
        const result = await client.request("get_fleets", { fleet_id, include_civilian });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting fleets: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 28: stellaris_reinforce_fleet
  server.tool(
    "stellaris_reinforce_fleet",
    "Reinforces a fleet according to its template deficit via native engine CReinforceFleetCommand (0x3B3C). Dispatches shipbuilding orders to available shipyards, deducting alloy/strategic resources natively and incrementing pending queues.",
    {
      fleet_id: z
        .number()
        .int()
        .describe("The unique ID of the fleet to reinforce (from stellaris_get_fleets)."),
    },
    async ({ fleet_id }) => {
      try {
        const result = await client.request("reinforce_fleet", { fleet_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error reinforcing fleet: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 29: stellaris_set_fleet_template_quota
  server.tool(
    "stellaris_set_fleet_template_quota",
    "Sets the target ship quota for a specific ship design in a fleet template via native CFleetTemplate modification (base + 0xE14840 / CFleetTemplate::AddDesign). Allows dynamic expansion or reduction of fleet composition.",
    {
      fleet_id: z
        .number()
        .int()
        .describe("The unique ID of the fleet whose template should be modified."),
      design_id: z
        .number()
        .int()
        .describe("The ship design ID to modify the quota for (from stellaris_get_fleets designs list)."),
      target_quota: z
        .number()
        .int()
        .min(0)
        .describe("The new target quota for this ship design in the fleet template."),
    },
    async ({ fleet_id, design_id, target_quota }) => {
      try {
        const result = await client.request("set_fleet_template_quota", {
          fleet_id,
          design_id,
          target_quota,
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error setting fleet template quota: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 30: stellaris_get_ship_designs
  server.tool(
    "stellaris_get_ship_designs",
    "Queries ship designs owned by the player empire (country + 0x1AD0 and CShipDesignManager at base + 0x3112980). Returns full section hierarchy, weapon/defense/aux slot definitions, equipped component templates, and core component loadout (reactor, FTL drive, thrusters, sensors, combat computer).",
    {
      design_id: z
        .number()
        .int()
        .optional()
        .describe("Optional specific ship design ID to query detailed information for. If omitted, returns all empire designs."),
    },
    async ({ design_id }) => {
      try {
        const result = await client.request("get_ship_designs", { design_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting ship designs: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 31: stellaris_get_ship_design_catalog
  server.tool(
    "stellaris_get_ship_design_catalog",
    "Queries the global component database (CComponentDatabase at base + 0x3156198) and available ship size hulls. Returns component sets, localized names, icons, size variants (small/medium/large/aux/core), and standard hull sizes.",
    {
      category: z
        .string()
        .optional()
        .describe("Optional filter category: 'weapon', 'defense', 'utility', 'auxiliary', 'core'."),
      unlocked_only: z
        .boolean()
        .optional()
        .describe("Whether to only return components currently unlocked by the player empire (default true)."),
    },
    async ({ category, unlocked_only }) => {
      try {
        const result = await client.request("get_ship_design_catalog", { category, unlocked_only });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting ship design catalog: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 32: stellaris_get_component_details
  server.tool(
    "stellaris_get_component_details",
    "Queries full native in-memory live attributes for a ship component or component set (from Clausewitz CComponentDatabase / CComponentSetTemplate / CComponentTemplate). Matches by component_key (e.g. 'PERDITION_BEAM_ION', 'SMALL_PLASMA_3', 'LARGE_ARMOR_5') or set_key (e.g. 'PERDITION_BEAM', 'PLASMA_3', 'DARK_MATTER_DEFLECTOR'). Returns all size variants in the series (small, medium, large, titanic, aux, etc.) with exact native attributes: min/max damage, range, cooldown, accuracy, tracking, shield/armor/hull multipliers, penetration, windup intervals, power, shield/armor/hull points, regen, evasion/speed modifiers, sensor ranges, ship behavior, and empire tech unlock status.",
    {
      key: z
        .string()
        .describe("The component_key (e.g. 'PERDITION_BEAM_ION', 'SMALL_PLASMA_3', 'LARGE_ARMOR_5') or set_key (e.g. 'PERDITION_BEAM', 'PLASMA', 'DARK_MATTER_DEFLECTOR')."),
    },
    async ({ key }) => {
      try {
        const result = await client.request("get_component_details", { key });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting component details: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 33: stellaris_create_ship_design
  server.tool(
    "stellaris_create_ship_design",
    "Creates and registers a brand-new independent ship design in the empire's roster via native engine registration (RegisterDesign at base + 0x266670). Allocates a new unique design_id, adds it to the player country, and optionally initializes customized weapon/defense slots and core components.",
    {
      ship_size: z
        .string()
        .describe("The customizable hull size for the new ship design (e.g. 'corvette', 'military_station_small', 'destroyer', 'cruiser')."),
      name: z
        .string()
        .optional()
        .describe("Optional custom name for the new ship design (e.g. 'HUMAN1_SHIP_Cobra')."),
      slots: z
        .array(
          z.object({
            section_name: z.string().optional().describe("Section name (e.g. 'bow', 'mid', 'stern') to disambiguate slot across sections."),
            section_index: z.number().int().optional().describe("0-based section index."),
            slot_index: z.number().int().optional().describe("0-based slot index within the ship's section."),
            slot_name: z.string().optional().describe("Name of the slot definition (e.g. 'LARGE_GUN_01', 'LARGE_UTILITY_1')."),
            component_key: z.string().describe("Component template key from catalog (e.g. 'KINETIC_ARTILLERY_2', 'LARGE_ARMOR_5')."),
          })
        )
        .optional()
        .describe("Optional initial list of slot modifications to equip on the newly created design."),
      core_components: z
        .object({
          reactor: z.string().optional().describe("Component key for reactor (e.g. 'CORVETTE_FISSION_REACTOR')."),
          ftl: z.string().optional().describe("Component key for FTL drive (e.g. 'HYPER_DRIVE_1')."),
          thruster: z.string().optional().describe("Component key for thrusters (e.g. 'SHIP_THRUSTER_1')."),
          sensor: z.string().optional().describe("Component key for sensors (e.g. 'SENSOR_1')."),
          combat_computer: z.string().optional().describe("Component key for combat computer (e.g. 'COMBAT_COMPUTER_DEFAULT')."),
          aura: z.string().optional().describe("Component key for titan/station aura (e.g. 'SHIP_AURA_QUANTUM_DESTABILIZER')."),
        })
        .optional()
        .describe("Optional initial core system component replacements."),
    },
    async ({ ship_size, name, slots, core_components }) => {
      try {
        const result = await client.request("create_ship_design", {
          ship_size,
          name,
          slots,
          core_components,
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error creating ship design: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 33: stellaris_update_ship_design
  server.tool(
    "stellaris_update_ship_design",
    "Customizes an existing ship design in the empire's roster by updating weapon, defense, and auxiliary slots, modifying core components (reactor/FTL/thrusters/sensors/combat computer/aura), or renaming the design.",
    {
      design_id: z
        .number()
        .int()
        .describe("The unique ID of the ship design to customize (from stellaris_get_ship_designs)."),
      name: z
        .string()
        .optional()
        .describe("Optional new custom name for this ship design."),
      slots: z
        .array(
          z.object({
            section_name: z.string().optional().describe("Section name (e.g. 'bow', 'mid', 'stern') to disambiguate slot across sections."),
            section_index: z.number().int().optional().describe("0-based section index."),
            slot_index: z.number().int().optional().describe("0-based slot index within the ship's section."),
            slot_name: z.string().optional().describe("Name of the slot definition (e.g. 'LARGE_GUN_01', 'LARGE_UTILITY_1')."),
            component_key: z.string().describe("Component template key from catalog (e.g. 'KINETIC_ARTILLERY_2', 'LARGE_ARMOR_5')."),
          })
        )
        .optional()
        .describe("List of slot modifications to apply."),
      core_components: z
        .object({
          reactor: z.string().optional().describe("Component key for reactor (e.g. 'CORVETTE_FISSION_REACTOR')."),
          ftl: z.string().optional().describe("Component key for FTL drive (e.g. 'HYPER_DRIVE_1')."),
          thruster: z.string().optional().describe("Component key for thrusters (e.g. 'SHIP_THRUSTER_1')."),
          sensor: z.string().optional().describe("Component key for sensors (e.g. 'SENSOR_1')."),
          combat_computer: z.string().optional().describe("Component key for combat computer (e.g. 'COMBAT_COMPUTER_DEFAULT')."),
          aura: z.string().optional().describe("Component key for titan/station aura (e.g. 'SHIP_AURA_QUANTUM_DESTABILIZER')."),
        })
        .optional()
        .describe("Core system component replacements."),
    },
    async ({ design_id, name, slots, core_components }) => {
      try {
        const result = await client.request("update_ship_design", {
          design_id,
          name,
          slots,
          core_components,
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error updating ship design: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 33: stellaris_upgrade_fleet
  server.tool(
    "stellaris_upgrade_fleet",
    "Orders a military fleet to refit and upgrade to the latest ship designs via native engine command CFleetUpgradeDesignCommand (Opcode 0x2F93). The fleet automatically moves to the nearest available shipyard starbase to carry out refit orders.",
    {
      fleet_id: z
        .number()
        .int()
        .describe("The unique ID of the fleet to upgrade (from stellaris_get_fleets)."),
      starbase_id: z
        .number()
        .int()
        .optional()
        .describe("Optional specific starbase ID to refit at. Default is 0xFFFFFFFF (nearest available shipyard starbase)."),
      target_design_id: z
        .number()
        .int()
        .optional()
        .describe("Optional specific target design ID to upgrade to. Default is 0xFFFFFFFF (upgrade all ship designs in the fleet)."),
    },
    async ({ fleet_id, starbase_id, target_design_id }) => {
      try {
        const result = await client.request("upgrade_fleet", {
          fleet_id,
          starbase_id,
          target_design_id,
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error upgrading fleet: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 34: stellaris_delete_ship_design
  server.tool(
    "stellaris_delete_ship_design",
    "Deletes an obsolete or unused ship design from the empire via native engine command CRemoveShipDesignCommand (Opcode 0x31B2).",
    {
      design_id: z
        .number()
        .int()
        .describe("The unique ID of the ship design to delete."),
    },
    async ({ design_id }) => {
      try {
        const result = await client.request("delete_ship_design", { design_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error deleting ship design: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 35: stellaris_get_market
  server.tool(
    "stellaris_get_market",
    "Retrieves current galactic/internal market state (Layer 2 Domain Perception) settled in trade value. Returns galactic market status, current market fee percentage, settlement currency ('trade'), all unlocked tradable commodities (including energy, minerals, food, consumer goods, alloys, and unlocked strategic resources like volatile motes; with stockpiles, batch sizes, unit base price, buy price with fee, and sell price with fee), and active monthly trades.",
    {},
    async () => {
      try {
        const result = await client.request("get_market");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting market info: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 36: stellaris_market_trade
  server.tool(
    "stellaris_market_trade",
    "Executes an instant spot trade (buy or sell) on the market via native engine command CMarketBuyResourceCommand / CMarketSellResourceCommand.",
    {
      resource: z.string().describe("Resource key to trade (e.g., 'minerals', 'food', 'energy', 'consumer_goods', 'alloys', 'volatile_motes', etc.)."),
      action: z.enum(["buy", "sell"]).describe("Action to perform: 'buy' or 'sell'."),
      units: z.number().int().optional().describe("Amount of resource units to trade (rounded to resource batch units). Default: 1 batch."),
    },
    async ({ resource, action, units }) => {
      try {
        const result = await client.request("market_trade", { resource, action, units });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error executing market trade: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 37: stellaris_set_monthly_trade
  server.tool(
    "stellaris_set_monthly_trade",
    "Creates or cancels a recurring monthly market trade order (Layer 3 Directive Intervention) via native engine command CAddMonthlyTradeCommand / CRemoveMonthlyTradeCommand. Existing orders and their order_id are listed by stellaris_get_market (monthly_trades).",
    {
      resource: z.string().optional().describe("Resource key (e.g., 'alloys', 'minerals', 'energy'). Required when creating an order."),
      action: z.enum(["buy", "sell"]).optional().describe("Order type: 'buy' or 'sell'. Default: 'buy'."),
      amount: z.number().optional().describe("Monthly quantity in units (e.g., 10, 50, 100). Required when creating an order."),
      price_limit: z.number().optional().describe("Maximum unit price in trade value (e.g. 100). Optional, 0 for no limit."),
      cancel: z.boolean().optional().describe("Set to true to cancel an existing order identified by order_id. Default: false."),
      order_id: z.number().int().optional().describe("Required when cancel is true: order_id from stellaris_get_market monthly_trades."),
    },
    async ({ resource, action, amount, price_limit, cancel, order_id }) => {
      try {
        const result = await client.request("set_monthly_trade", {
          resource: resource ?? "",
          action: action ?? "buy",
          amount: amount ?? 0,
          price_limit: price_limit ?? 0.0,
          cancel: cancel ?? false,
          order_id: order_id ?? -1,
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error setting monthly trade: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 38: stellaris_get_discoveries
  server.tool(
    "stellaris_get_discoveries",
    "Retrieves relics, astral actions, and minor artifact actions (Layer 2 Domain Perception). Includes player held relics with passive/triumph descriptions and activation status, all galaxy relics, astral actions, and artifact actions.",
    {
      tab: z.enum(["all", "relics", "astral_actions", "artifact_actions"]).optional().describe("Sub-panel to query: 'all', 'relics', 'astral_actions', or 'artifact_actions'. Default: 'all'."),
    },
    async ({ tab }) => {
      try {
        const result = await client.request("get_discoveries", { tab: tab ?? "all" });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting discoveries: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 39: stellaris_activate_relic
  server.tool(
    "stellaris_activate_relic",
    "Activates the triumph effect of an owned relic (Layer 3 Directive Intervention) via native engine command CActivateRelicCommand (Opcode 0x3CEA).",
    {
      relic_key: z.string().describe("Relic key identifier (e.g. 'r_severed_head', 'r_the_surveyor', 'r_galatron')."),
    },
    async ({ relic_key }) => {
      try {
        const result = await client.request("activate_relic", { relic_key });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error activating relic: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 40: stellaris_get_contacts
  server.tool(
    "stellaris_get_contacts",
    "Retrieves galactic contacts and diplomatic communications (Layer 2 Domain Perception). Lists all known empires, contact flags, diplomatic treaties (commercial pact, research agreement, migration pact), and pending first contact logs. NOTE: Claims (宣称) are excluded.",
    {
      mode: z.enum(["all", "empires", "first_contacts"]).optional().describe("Filter mode: 'all', 'empires', or 'first_contacts'. Default: 'all'."),
    },
    async ({ mode }) => {
      try {
        const result = await client.request("get_contacts", { mode: mode ?? "all" });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting contacts: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 41: stellaris_get_outliner
  server.tool(
    "stellaris_get_outliner",
    "Retrieves the macro summary of the empire's Outliner (Layer 1 Progressive Disclosure). Summarizes counts and KPI indicators across the 4 key categories: Sectors/Colonies (total sectors, total colonies, total pops, capital system, and sector catalog with sector_id/name/KPI), Military Fleets (combat fleets count, total military power), Civilian Fleets (civilian fleets count), and Armies (garrison armies count, transport armies count). NOTE: Uncolonized / colonizing planets without definitive sector assignment are excluded.",
    {},
    async () => {
      try {
        const result = await client.request("get_outliner");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting outliner summary: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 42: stellaris_get_sectors
  server.tool(
    "stellaris_get_sectors",
    "Expands a specific sector or all sectors to inspect member colonies (Layer 2 Progressive Disclosure). Provide 'sector_id' (from stellaris_get_outliner; colonies in no sector are grouped with sector_id null). If omitted, expands all sectors. Returns colony_id (the colony) and planet_id/id (use it for planet tools), name, system name, population, size, capital status, colonizing progress, current construction (building/district and progress), and the planet status alerts the game's outliner shows: crisis states (blockaded = under orbital bombardment, with the blockading empire and its bombardment stance; occupied) and notices (construction slot, capital upgrade, unemployment, excess civilians, overcrowding, low stability, clearable blocker).",
    {
      sector_id: z.number().int().optional().describe("Sector id to expand (from stellaris_get_outliner). If omitted, expands all sectors."),
    },
    async ({ sector_id }) => {
      try {
        const result = await client.request("get_sectors", { sector_id: sector_id !== undefined ? sector_id : -1 });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting sectors: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 43: stellaris_get_military_fleets
  server.tool(
    "stellaris_get_military_fleets",
    "Retrieves detailed list of all military combat fleets (Layer 2 Progressive Disclosure). Includes fleet ID, fleet name, military power rating, current ship count, template quota, reinforcement capability, and current operational status.",
    {},
    async () => {
      try {
        const result = await client.request("get_military_fleets");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting military fleets: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 44: stellaris_get_civilian_fleets
  server.tool(
    "stellaris_get_civilian_fleets",
    "Retrieves list of all civilian ships and non-combat fleets (Layer 2 Progressive Disclosure). Categorizes science ships, construction ships, and colony ships with fleet ID, ship name, and operational status.",
    {},
    async () => {
      try {
        const result = await client.request("get_civilian_fleets");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting civilian fleets: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 45: stellaris_get_armies
  server.tool(
    "stellaris_get_armies",
    "Retrieves list of ground armies and transport fleets (Layer 2 Progressive Disclosure). Distinguishes planetary garrison armies from embarked transport fleets, including army ID, type/name, combat strength power, health percentage, and stationed planet ID/name.",
    {},
    async () => {
      try {
        const result = await client.request("get_armies");
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting armies: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 46: stellaris_get_planet_details
  server.tool(
    "stellaris_get_planet_details",
    "Retrieves comprehensive planetary details for a specific colony in Stellaris 4.5.0 Cygnus (Layer 3 Progressive Disclosure Entity Deep-Dive). Fully aligned with Stellaris 4.5.0 Districts & Zones mechanics: 1) Planet overview (planet type, habitability %, colony date, planet size); 2) Top KPI bar (stability %, pop groups scale e.g. 5.9K, pop capacity, crime %, housing, amenities, unemployed, pop growth); 3) 4 Primary Districts (City, Generator, Mining, Agriculture) with their respective Zone Specializations, zone slots, unlock requirements, and buildings; 4) Monthly resources from the colony's own economy tables: produced, upkeep and net per resource (non-zero only); 5) Active construction queue; 6) Planetary features (all natural deposits and blockers with clear time, clear costs, modifiers, swap unlock types, and queued clearance status); 7) Current population breakdown (species list with pop counts, display '5.9K', share %, net change, portrait); 8) Monthly population summary (net change, growth, migration, assembly, categories, demographic pie chart); 9) Colony ascension (tier 0-10, designation multiplier +25%/tier, can_ascend, status text); 10) Colony status alerts, identical to the outliner: blockaded (orbital bombardment; name carries the blockader, desc the bombardment stance), occupied, construction_available, upgrade_available, unemployment, excess_civilians, overcrowding, low_stability, blocker_available. Species ids are the full ids used by the species tools.",
    {
      planet_id: z.number().int().describe("Planet / Colony ID to inspect (obtainable from stellaris_get_sectors or stellaris_get_outliner)."),
    },
    async ({ planet_id }) => {
      try {
        const result = await client.request("get_planet_details", { planet_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting planet details: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 47: stellaris_get_available_district_zones
  server.tool(
    "stellaris_set_district_zone",
    "Queues a zone into a district's zone slot (native construction queue, CBuildableZone), replacing the zone there. Rejected with the game's reason when the zone is not allowed in that slot. Get district ids, slots and allowed zones from stellaris_get_available_district_zones.",
    {
      planet_id: z.number().int().describe("Planet id"),
      district_id: z.number().int().describe("District id (from stellaris_get_available_district_zones)"),
      slot: z.number().int().describe("Zone slot index of that district"),
      zone_key: z.string().describe("Zone type key, e.g. zone_industrial"),
    },
    async ({ planet_id, district_id, slot, zone_key }) => {
      try {
        const result = await client.request("set_district_zone", { planet_id, district_id, slot, zone_key });
        return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
      } catch (err: any) {
        return { isError: true, content: [{ type: "text", text: `Error: ${err.message}` }] };
      }
    }
  );

  server.tool(
    "stellaris_get_available_district_zones",
    "Zone slots of a colony's districts (Layer 2): per district (id, type) and slot, the zone currently there and every zone type the game would queue into that slot (the construction queue's own validation of a CBuildableZone), with cost and build days. include_blocked=true also lists the refused zone types with the game's reason. Build one with stellaris_set_district_zone.",
    {
      planet_id: z.number().int().describe("Planet id (from stellaris_get_sectors or stellaris_get_outliner)."),
      district_type: z.string().optional().describe("Optional district type filter (e.g. 'district_city' or 'city'); empty for all."),
      include_blocked: z.boolean().optional().default(false).describe("Also list zone types the game refuses, with the reason"),
    },
    async ({ planet_id, district_type, include_blocked }) => {
      try {
        const result = await client.request("get_available_district_zones", { planet_id, district_type: district_type || "", include_blocked: include_blocked ?? false });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting available district zones: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 48: stellaris_get_buildable_buildings
  server.tool(
    "stellaris_get_buildable_buildings",
    "Buildings the game accepts in each building zone of a planet now (Layer 2), each checked with the engine's own construction validation. Per zone: zone_id, zone key/name, district, buildings/max_buildings, and buildable[] with cost (after the empire's cost modifiers) and build_days. Pass building_key to check one building in every zone (can_build, or the game's reason), and zone_id to limit to one zone.",
    {
      planet_id: z.number().int().describe("Planet ID (planet_id from the outliner or get_planet_details)."),
      building_key: z.string().optional().describe("Optional building key to check, e.g. 'building_foundry_1'."),
      zone_id: z.number().int().optional().describe("Optional zone id (districts[].zones[].slot_index in get_planet_details)."),
    },
    async ({ planet_id, building_key, zone_id }) => {
      try {
        const result = await client.request("get_buildable_buildings", {
          planet_id,
          building_key: building_key || "",
          zone_id: zone_id !== undefined ? zone_id : -1,
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting buildable buildings: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 49: stellaris_build_building
  server.tool(
    "stellaris_build_building",
    "Queues construction of a building (native CAddBuildableToQueueCommand with a CBuildableBuilding), validated by the engine; the game's reason is returned when refused. slot_index is the target zone id (see stellaris_get_buildable_buildings); without it the first zone that accepts the building is used.",
    {
      planet_id: z.number().int().describe("Planet ID to construct the building on."),
      building_key: z.string().describe("Building definition key (e.g. 'building_energy_grid', 'building_mineral_purification_plant', 'building_food_processing_facility', 'building_autochthon_monument', 'building_biolab_1', 'building_foundry_1')."),
      district_type: z.string().optional().describe("Optional district type ('district_generator', 'district_mining', 'district_farming', 'district_city')."),
      slot_index: z.number().int().optional().describe("Optional target zone id (zone_id from stellaris_get_buildable_buildings / districts[].zones[].slot_index). Zone ids differ per planet."),
    },
    async ({ planet_id, building_key, district_type, slot_index }) => {
      try {
        const result = await client.request("build_building", {
          planet_id,
          building_key,
          district_type: district_type || "",
          slot_index: slot_index !== undefined ? slot_index : -1,
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error building ${building_key}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 50: stellaris_upgrade_building
  server.tool(
    "stellaris_upgrade_building",
    "Upgrades an existing building on a planet in Stellaris 4.5.0 Cygnus via native CBuildableUpgradeBuilding entity and CAddBuildableToQueueCommand. Automatically resolves the building's current slot/zone and target upgrade definition if omitted.",
    {
      planet_id: z.number().int().describe("Planet / Colony ID (e.g. 11 for Earth)."),
      building_id: z.number().int().describe("Unique instance ID (bid) of the existing building to upgrade (e.g. 2, 3, 16777308, 1)."),
      upgrade_to_key: z.string().optional().describe("Optional target upgrade building key (e.g. 'building_hyper_entertainment_forum', 'building_commercial_megaplex', 'building_physics_lab_2', 'building_factory_2'). If omitted, will be inferred from the existing building type."),
    },
    async ({ planet_id, building_id, upgrade_to_key }) => {
      try {
        const result = await client.request("upgrade_building", {
          planet_id,
          building_id,
          upgrade_to_key: upgrade_to_key || "",
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error upgrading building ${building_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 51: stellaris_get_clearable_blockers
  server.tool(
    "stellaris_get_clearable_blockers",
    "Retrieves all planetary deposit blockers (e.g. Great Pacific Garbage Patch, Failing Infrastructure, Decrepit Dwellings, Deep Sinkhole) on a specified planet/colony. Evaluates real-time clearance status using native game engine validation (can_clear, is_queued, prerequisites, and resource affordability).",
    {
      planet_id: z.number().int().describe("Planet / Colony ID to inspect blockers for (e.g. 11 or 3 for Earth)."),
    },
    async ({ planet_id }) => {
      try {
        const result = await client.request("get_clearable_blockers", { planet_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting clearable blockers: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 52: stellaris_clear_blocker
  server.tool(
    "stellaris_clear_blocker",
    "Queues a deposit blocker clearance order on a planet via native CBuildableClearDepositBlocker entity and CAddBuildableToQueueCommand. Enforces strict engine validation of technology prerequisites, resource costs, and queue availability before dispatching to the main thread.",
    {
      planet_id: z.number().int().describe("Planet / Colony ID where the blocker is located (e.g. 11 or 3 for Earth)."),
      deposit_id: z.number().int().optional().describe("Unique instance ID of the deposit blocker to clear (obtainable from stellaris_get_clearable_blockers, e.g. 309, 310, 311)."),
      deposit_key: z.string().optional().describe("Blocker definition key (e.g. 'd_great_pacific_garbage_patch', 'd_failing_infrastructure_earth', 'd_decrepit_dwellings'). Can be used instead of deposit_id."),
    },
    async ({ planet_id, deposit_id, deposit_key }) => {
      try {
        const result = await client.request("clear_blocker", {
          planet_id,
          deposit_id: deposit_id !== undefined ? deposit_id : 0,
          deposit_key: deposit_key || "",
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error clearing blocker on planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 53: stellaris_get_planetary_decisions
  server.tool(
    "stellaris_get_planetary_decisions",
    "Queries planetary decisions for a given planet or colony. Evaluates native engine enactment preconditions (technologies, resources, cooldowns, planet features) via CEnactDecisionCommand::IsValid, returning the list of available decisions with localized names, durations, and can_enact status.",
    {
      planet_id: z.number().int().describe("Planet / Colony ID to query decisions for (e.g. 3 for Earth)."),
    },
    async ({ planet_id }) => {
      try {
        const result = await client.request("get_planetary_decisions", { planet_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting planetary decisions for planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 54: stellaris_enact_planetary_decision
  server.tool(
    "stellaris_enact_planetary_decision",
    "Enacts a planetary decision on a colony using native engine command CEnactDecisionCommand. Enforces strict engine validation of conditions, resources, and prerequisites before dispatching to the main thread.",
    {
      planet_id: z.number().int().describe("Planet / Colony ID to enact the decision on (e.g. 3 for Earth)."),
      decision_key: z.string().describe("Key or alias of the decision (e.g. 'decision_planet_luxuries_boost', 'decision_planet_food_boost', 'decision_discourage_growth', 'decision_mastery_of_nature', 'luxuries', 'food')."),
    },
    async ({ planet_id, decision_key }) => {
      try {
        const result = await client.request("enact_decision", {
          planet_id,
          decision_key,
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error enacting decision '${decision_key}' on planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 55: stellaris_get_terraforming_options
  server.tool(
    "stellaris_get_terraforming_options",
    "Queries the current terraforming state and available terraforming target options for a planet from the global CTerraformDatabase. Returns ongoing terraforming progress (progress days, total days, remaining days, percentage) if active, and all valid target planetary classes with duration and can_terraform readiness.",
    {
      planet_id: z.number().int().describe("Planet ID to query terraforming options for (e.g. 3 for Earth)."),
    },
    async ({ planet_id }) => {
      try {
        const result = await client.request("get_terraforming_options", { planet_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting terraforming options for planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 56: stellaris_start_terraforming
  server.tool(
    "stellaris_start_terraforming",
    "Initiates planetary terraformation project using native engine command CStartTerraformationCommand. Enforces full prerequisite validation (technologies such as Climate Restoration / Terrestrial Sculpting, energy credits stockpile) before dispatching to the game thread.",
    {
      planet_id: z.number().int().describe("Planet ID to terraform (e.g. 3 for Earth)."),
      target_class: z.string().optional().describe("Target planetary class key or alias (e.g. 'pc_ocean', 'pc_tropical', 'pc_gaia', 'pc_desert', 'ocean', 'gaia')."),
      link_index: z.number().int().optional().describe("Unique index of the terraforming link from CTerraformDatabase (obtainable from stellaris_get_terraforming_options). Can be provided instead of target_class."),
    },
    async ({ planet_id, target_class, link_index }) => {
      try {
        const result = await client.request("start_terraforming", {
          planet_id,
          target_class: target_class || "",
          link_index: link_index !== undefined ? link_index : -1,
        });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error starting terraforming on planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 57: stellaris_cancel_terraforming
  server.tool(
    "stellaris_cancel_terraforming",
    "Cancels an ongoing planetary terraforming project on a planet using native engine command CCancelTerraformationCommand. Restores energy credits and resets the planetary terraforming state.",
    {
      planet_id: z.number().int().describe("Planet ID where terraforming is to be cancelled (e.g. 3 for Earth)."),
    },
    async ({ planet_id }) => {
      try {
        const result = await client.request("cancel_terraforming", { planet_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error cancelling terraforming on planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 58: stellaris_get_planetary_features
  server.tool(
    "stellaris_get_planetary_features",
    "Retrieves all planetary features and blockers for a specific planet in Stellaris 4.5.0 Cygnus. Includes complete natural features, deposit blockers, queued clearance orders, clearance times (days), resource clearing costs (e.g. 300 energy), modifier effects (e.g. planet_max_districts_add, generator/mining/farming district capacity bonuses), and on-clear swap replacements (e.g. unlocking Delhi Sprawl).",
    {
      planet_id: z.number().int().describe("Planet / Colony ID to inspect (obtainable from stellaris_get_sectors or stellaris_get_outliner)."),
    },
    async ({ planet_id }) => {
      try {
        const result = await client.request("get_planetary_features", { planet_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting planetary features for planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 59: stellaris_ascend_colony
  server.tool(
    "stellaris_ascend_colony",
    "Ascends a colony to the next planetary ascension tier (1-10) using native engine command CIncreasePlanetaryAscensionTierCommand. Increases planetary designation efficiency bonus by +25% per tier. Validates prerequisites (traditions unlocked, unity cost) before dispatching.",
    {
      planet_id: z.number().int().describe("Planet / Colony ID to ascend (e.g. 3 for Earth)."),
    },
    async ({ planet_id }) => {
      try {
        const result = await client.request("ascend_colony", { planet_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error ascending colony on planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 60: stellaris_get_planet_jobs
  server.tool(
    "stellaris_get_planet_jobs",
    "Retrieves detailed job and workforce allocation for a colony in Stellaris 4.5.0 Cygnus (Layer 2 Progressive Disclosure). Returns jobs categorized by stratum (ruler, specialist, worker, civilian), detailing current workforce, max workforce capacity, bonus workforce, effective workforce, workforce limit, whether prioritized (favorite), and whether it can be prioritized.",
    {
      planet_id: z.number().int().describe("Planet / Colony ID to inspect (e.g. 3 for Earth)."),
    },
    async ({ planet_id }) => {
      try {
        const result = await client.request("get_planet_jobs", { planet_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting planet jobs for planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 61: stellaris_set_job_priority
  server.tool(
    "stellaris_set_job_priority",
    "Toggles or sets the priority (favorite) status for a job type on a planet using native engine command CSetFavoriteJobCommand. Prioritized jobs receive preferential workforce allocation from available pops.",
    {
      planet_id: z.number().int().describe("Planet / Colony ID (e.g. 3 for Earth)."),
      job_key: z.string().describe("Job type key to prioritize/toggle (e.g. 'physicist', 'metallurgist', 'politician', 'artisan')."),
    },
    async ({ planet_id, job_key }) => {
      try {
        const result = await client.request("set_job_priority", { planet_id, job_key });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error setting job priority for ${job_key} on planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 62: stellaris_set_job_workforce_limit
  server.tool(
    "stellaris_set_job_workforce_limit",
    "Adjusts the workforce limit slider for a specific job on a planet using native engine command CChangeJobWorkforceLimitCommand. Allows capping or reopening workforce capacity for specific jobs (e.g. 0 to disable, or -1 for max capacity).",
    {
      planet_id: z.number().int().describe("Planet / Colony ID (e.g. 3 for Earth)."),
      job_key: z.string().describe("Job type key to limit (e.g. 'clerk', 'technician', 'farmer')."),
      limit: z.number().int().describe("Workforce allocation limit (non-negative integer count, or -1 for uncapped maximum capacity)."),
    },
    async ({ planet_id, job_key, limit }) => {
      try {
        const result = await client.request("set_job_workforce_limit", { planet_id, job_key, limit });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error setting workforce limit for ${job_key} on planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 63: stellaris_get_planet_armies
  server.tool(
    "stellaris_get_planet_armies",
    "Retrieves complete army information for a planet/colony in Stellaris 4.5.0 Cygnus (Layer 2 Progressive Disclosure). Returns overview (stationed armies count, garrison power, assault power, deploy_in_orbit, include_in_builder), stationed armies list (army_id = full id accepted by disband, engine-rendered name, type_key, is_defense, is_occupation, power as computed by the game, health/max_health, morale/max_morale for types with morale, species), recruitable army types catalog (build time and the type's health/damage/morale/morale-damage multipliers), and current army recruitment queue.",
    {
      planet_id: z.number().int().describe("Planet / Colony ID to inspect (e.g. 3 for Earth)."),
    },
    async ({ planet_id }) => {
      try {
        const result = await client.request("get_planet_armies", { planet_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error getting planet armies for planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 64: stellaris_set_planet_army_settings
  server.tool(
    "stellaris_set_planet_army_settings",
    "Controls planet army status settings in Stellaris 4.5.0 Cygnus. Toggles or sets '部署到轨道上' (deploy_in_orbit) via CToggleDeployArmiesInOrbitCommand and/or '包含在陆军建造功能中' (include_in_builder) via CToggleIncludeInArmyBuilderCommand.",
    {
      planet_id: z.number().int().describe("Planet / Colony ID (e.g. 3 for Earth)."),
      deploy_in_orbit: z.boolean().optional().describe("Whether recruited armies should automatically deploy into orbit (true) or stay stationed on the surface (false)."),
      include_in_builder: z.boolean().optional().describe("Whether this planet is included in the empire army builder roster (true) or excluded (false)."),
    },
    async ({ planet_id, deploy_in_orbit, include_in_builder }) => {
      try {
        const result = await client.request("set_planet_army_settings", { planet_id, deploy_in_orbit, include_in_builder });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error setting planet army settings on planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 65: stellaris_embark_all_armies
  server.tool(
    "stellaris_embark_all_armies",
    "Embarks all stationed assault armies from a colony into orbit as a transport fleet using native engine command CMoveArmyToOrbitCommand ('全部登船'). Defense armies remain on the planet.",
    {
      planet_id: z.number().int().describe("Planet / Colony ID (e.g. 3 for Earth)."),
    },
    async ({ planet_id }) => {
      try {
        const result = await client.request("embark_all_armies", { planet_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error embarking armies on planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 66: stellaris_disband_planet_army
  server.tool(
    "stellaris_disband_planet_army",
    "Disbands a specific army stationed on a planet using native engine command CDisbandArmyCommand.",
    {
      planet_id: z.number().int().describe("Planet / Colony ID (e.g. 3 for Earth)."),
      army_id: z.number().int().describe("Unique army ID / handle slot to disband."),
    },
    async ({ planet_id, army_id }) => {
      try {
        const result = await client.request("disband_planet_army", { planet_id, army_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error disbanding army ${army_id} on planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );

  // Tool 67: stellaris_recruit_planet_army
  server.tool(
    "stellaris_recruit_planet_army",
    "Recruits an assault army on a colony into the planet's recruitment queue (Queue 1) using native engine command CAddBuildableToQueueCommand with CBuildableArmy.",
    {
      planet_id: z.number().int().describe("Planet / Colony ID (e.g. 3 for Earth)."),
      army_key: z.string().describe("Army type key from catalog to recruit (e.g. 'assault_army', 'xenomorph_army', 'clone_army', 'gene_warrior_army')."),
      species_id: z.number().int().optional().describe("Optional species ID for the recruited army pops (defaults to colony founder/dominant species)."),
    },
    async ({ planet_id, army_key, species_id }) => {
      try {
        const result = await client.request("recruit_planet_army", { planet_id, army_key, species_id });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (err: any) {
        return {
          isError: true,
          content: [
            {
              type: "text",
              text: `Error recruiting army ${army_key} on planet ${planet_id}: ${err.message}`,
            },
          ],
        };
      }
    }
  );
}



