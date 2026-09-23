import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { PipeClient } from "./pipe_client.js";

export function registerTools(server: McpServer, client: PipeClient) {
  // Tool 1: stellaris_get_status
  server.tool(
    "stellaris_get_status",
    "Retrieves current Stellaris game status (Layer 1 Global Perception in progressive disclosure architecture). Includes session active state, pause state, speed (0-4), date, empire statistics (empire_size, colonies, starbases, naval_capacity), empire resources (stockpile/income/expense/net for all 26 resources), situation_log indicators (situations_count, special_projects_count, anomalies_count), council indicators (ruler_name, active_agenda, agenda_progress, agenda_cost, agenda_ready, councilor_count), society indicators (can_unlock_tradition, next_tradition_cost, adopted_trees_count, unlocked_traditions_count, active_edicts_count, edict_fund), and leaders indicators (total_hired, leader_capacity, pool_count, has_unspent_trait_points).",
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
    "Retrieves all currently active pending event windows in the game session, including event ID, title, description, and available option choices with their validity status.",
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
      option_index: z.number().int().describe("The index of the option to choose (0-based, must be valid/enabled)."),
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

  // Tool 13: stellaris_get_situation_log
  server.tool(
    "stellaris_get_situation_log",
    "Retrieves the detailed situation log state, including active situations (stage progress, monthly change rate, current approach, owner country), special projects, and anomalies. Implements progressive disclosure (Layer 2) for F1 Situation Log.",
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
    "Retrieves full leader and recruitment pool details (Layer 2 Domain Deep-Dive in progressive disclosure architecture). Returns summary counts, list of all hired leaders (id, localized name, class, subclass, level, age, ethic, assignment_type, target_id, hire_date), and candidate recruitment pool (id, name, class, level, age, hire_cost, ethic).",
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
}



