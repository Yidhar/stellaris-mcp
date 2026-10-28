// Empire designs as game script: the text the game reads its user designs from
// (user_empire_designs_*.txt) and its prescripted empires from (prescripted_countries\*.txt).
// A small reader / writer of that script, finding one design's block in a file, and applying
// field changes the way the game's own design file spells them (CEmpireDesign::WriteMembers).
import fs from "node:fs";
import path from "node:path";

export type Value = string | Entry[];
export interface Entry {
  key: string | null; // null: a bare value or an anonymous block inside a block
  op: string | null;
  value: Value;
}

interface Token {
  kind: "word" | "string" | "op" | "open" | "close";
  text: string;
  start: number;
  end: number;
}

function tokenize(src: string): Token[] {
  const out: Token[] = [];
  let i = 0;
  while (i < src.length) {
    const c = src[i];
    if (c === "#") {
      while (i < src.length && src[i] !== "\n") i++;
    } else if (/\s/.test(c) || c === "﻿") {
      i++;
    } else if (c === "{" || c === "}") {
      out.push({ kind: c === "{" ? "open" : "close", text: c, start: i, end: i + 1 });
      i++;
    } else if (c === '"') {
      let j = i + 1;
      while (j < src.length && src[j] !== '"') j += src[j] === "\\" ? 2 : 1;
      out.push({ kind: "string", text: src.slice(i, j + 1), start: i, end: j + 1 });
      i = j + 1;
    } else if ("=<>!?".includes(c)) {
      let j = i + 1;
      while (j < src.length && "=<>".includes(src[j])) j++;
      out.push({ kind: "op", text: src.slice(i, j), start: i, end: j });
      i = j;
    } else {
      let j = i;
      while (j < src.length && !/[\s{}"=<>!?#]/.test(src[j])) j++;
      if (j === i) j++;
      out.push({ kind: "word", text: src.slice(i, j), start: i, end: j });
      i = j;
    }
  }
  return out;
}

export const unquote = (s: string) => (s.startsWith('"') && s.endsWith('"') && s.length >= 2 ? s.slice(1, -1).replace(/\\"/g, '"') : s);
export const quote = (s: string) => `"${s.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"`;

function parseEntries(tokens: Token[], at: { i: number }): Entry[] {
  const out: Entry[] = [];
  while (at.i < tokens.length && tokens[at.i].kind !== "close") {
    const t = tokens[at.i];
    const value = (): Value => {
      const v = tokens[at.i];
      if (!v) throw new Error("The script ends inside a block");
      if (v.kind === "open") {
        at.i++;
        const inner = parseEntries(tokens, at);
        if (tokens[at.i]?.kind !== "close") throw new Error("A block is not closed");
        at.i++;
        return inner;
      }
      at.i++;
      return v.text;
    };
    if (t.kind === "open") {
      out.push({ key: null, op: null, value: value() });
    } else if (tokens[at.i + 1]?.kind === "op") {
      at.i++;
      const op = tokens[at.i++].text;
      out.push({ key: t.text, op, value: value() });
    } else {
      at.i++;
      out.push({ key: null, op: null, value: t.text });
    }
  }
  return out;
}

export function parse(src: string): Entry[] {
  const tokens = tokenize(src);
  const at = { i: 0 };
  const out = parseEntries(tokens, at);
  if (at.i < tokens.length) throw new Error("An unmatched } in the script");
  return out;
}

export function write(entries: Entry[], depth = 0): string {
  const pad = "\t".repeat(depth);
  return entries
    .map((e) => {
      const head = e.key === null ? "" : `${e.key} ${e.op ?? "="} `;
      if (typeof e.value === "string") return pad + head + e.value;
      const flat = e.value.every((x) => x.key === null && typeof x.value === "string");
      if (flat) return `${pad}${head}{ ${e.value.map((x) => x.value).join(" ")} }`;
      return `${pad}${head}{\n${write(e.value, depth + 1)}\n${pad}}`;
    })
    .join("\n");
}

// the top-level `key = { ... }` blocks of a file: their unquoted key and [start, end) in the text
export function topBlocks(src: string): { key: string; start: number; end: number }[] {
  const tokens = tokenize(src);
  const out: { key: string; start: number; end: number }[] = [];
  for (let i = 0; i + 2 < tokens.length; ) {
    const [k, op, open] = [tokens[i], tokens[i + 1], tokens[i + 2]];
    if ((k.kind === "word" || k.kind === "string") && op.kind === "op" && open.kind === "open") {
      let depth = 0, j = i + 2;
      for (; j < tokens.length; j++) {
        if (tokens[j].kind === "open") depth++;
        else if (tokens[j].kind === "close" && --depth === 0) break;
      }
      if (j >= tokens.length) break;
      out.push({ key: unquote(k.text), start: k.start, end: tokens[j].end });
      i = j + 1;
    } else {
      i++;
    }
  }
  return out;
}

// one design's script: the user design file first (the game looks prescripted keys up first, but
// a user design never shares one), then the game's prescripted_countries
export function findDesignText(userFile: string, gameDir: string, key: string, type: "user" | "prescripted"): { text: string; file: string } | null {
  const files = type === "user"
    ? [userFile]
    : fs.existsSync(path.join(gameDir, "prescripted_countries"))
      ? fs.readdirSync(path.join(gameDir, "prescripted_countries")).filter((f) => f.endsWith(".txt")).sort()
          .map((f) => path.join(gameDir, "prescripted_countries", f))
      : [];
  for (const file of files) {
    if (!fs.existsSync(file)) continue;
    const src = fs.readFileSync(file, "utf-8");
    const block = topBlocks(src).find((b) => b.key === key);
    if (block) return { text: src.slice(block.start, block.end), file };
  }
  return null;
}

// --- field changes, spelled the way the game writes its user designs -------------------------

export interface DesignChanges {
  name?: string;
  adjective?: string;
  ethics?: string[];
  civics?: string[];
  authority?: string;
  government?: string;
  origin?: string;
  species_class?: string;
  portrait?: string;
  species_name?: string;
  species_plural?: string;
  species_adjective?: string;
  name_list?: string;
  gender?: string;
  traits?: string[];
  planet_class?: string;
  planet_name?: string;
  system_name?: string;
  initializer?: string;
  ship_prefix?: string;
  ship_set?: string;
  city_set?: string;
  room?: string;
  advisor_voice?: string;
  spawn?: "yes" | "no" | "always";
}

const literal = (text: string): Value => [
  { key: "key", op: "=", value: quote(text) },
  { key: "literal", op: "=", value: "yes" },
];

// replace every entry spelled by one of `names` with `add` (at the first one's place, or the end)
function setEntries(block: Entry[], names: string[], add: Entry[]) {
  const at = block.findIndex((e) => e.key !== null && names.includes(unquote(e.key)));
  const kept = block.filter((e) => !(e.key !== null && names.includes(unquote(e.key))));
  const pos = at < 0 ? kept.length : block.slice(0, at).filter((e) => !(e.key !== null && names.includes(unquote(e.key)))).length;
  kept.splice(pos, 0, ...add);
  block.splice(0, block.length, ...kept);
}

const one = (key: string, value: Value): Entry[] => [{ key, op: "=", value }];
const many = (key: string, values: string[]): Entry[] => values.map((v) => ({ key, op: "=", value: quote(v) }));

// the design block `key = { ... }` with `changes` applied and `newKey` as its key
export function applyChanges(designText: string, newKey: string, changes: DesignChanges): string {
  const top = parse(designText);
  if (top.length !== 1 || top[0].key === null || typeof top[0].value === "string") {
    throw new Error("The design text must be one block: key = { ... }");
  }
  const d = top[0].value;
  const c = changes;
  if (c.name !== undefined) setEntries(d, ["name"], one("name", literal(c.name)));
  if (c.adjective !== undefined) setEntries(d, ["adjective"], one("adjective", literal(c.adjective)));
  if (c.ethics) setEntries(d, ["ethic"], many("ethic", c.ethics));
  if (c.civics) setEntries(d, ["civics", "civic"], one("civics", c.civics.map((v) => ({ key: null, op: null, value: quote(v) }))));
  for (const [field, token] of [["authority", "authority"], ["government", "government"], ["origin", "origin"],
    ["planet_class", "planet_class"], ["initializer", "initializer"], ["ship_set", "graphical_culture"],
    ["city_set", "city_graphical_culture"], ["room", "room"], ["advisor_voice", "advisor_voice_type"]] as const) {
    const v = c[field];
    if (v !== undefined) setEntries(d, [token], one(token, quote(v)));
  }
  if (c.planet_name !== undefined) setEntries(d, ["planet_name"], one("planet_name", literal(c.planet_name)));
  if (c.system_name !== undefined) setEntries(d, ["system_name"], one("system_name", literal(c.system_name)));
  if (c.ship_prefix !== undefined) setEntries(d, ["ship_prefix"], one("ship_prefix", literal(c.ship_prefix)));
  if (c.spawn !== undefined) setEntries(d, ["spawn_enabled"], one("spawn_enabled", c.spawn));

  const speciesFields = [c.species_class, c.portrait, c.species_name, c.species_plural, c.species_adjective, c.name_list, c.gender, c.traits];
  if (speciesFields.some((v) => v !== undefined)) {
    let sp = d.find((e) => e.key !== null && unquote(e.key) === "species" && typeof e.value !== "string");
    if (!sp) {
      sp = { key: "species", op: "=", value: [] };
      d.push(sp);
    }
    const s = sp.value as Entry[];
    if (c.species_class !== undefined) setEntries(s, ["class", "species_class"], one("class", quote(c.species_class)));
    if (c.portrait !== undefined) setEntries(s, ["portrait"], one("portrait", quote(c.portrait)));
    // the reader takes name / plural / adjective (prescripted files) and species_* (user files)
    if (c.species_name !== undefined) setEntries(s, ["name", "species_name"], one("species_name", literal(c.species_name)));
    if (c.species_plural !== undefined) setEntries(s, ["plural", "species_plural"], one("species_plural", literal(c.species_plural)));
    if (c.species_adjective !== undefined) setEntries(s, ["adjective", "species_adjective"], one("species_adjective", literal(c.species_adjective)));
    if (c.name_list !== undefined) setEntries(s, ["name_list"], one("name_list", quote(c.name_list)));
    if (c.gender !== undefined) setEntries(s, ["gender"], one("gender", c.gender));
    if (c.traits) setEntries(s, ["trait"], many("trait", c.traits));
  }

  setEntries(d, ["key"], one("key", quote(newKey)));
  return write([{ key: quote(newKey), op: "=", value: d }]);
}

// the user design file with one design's block added, replaced or removed (other designs as they
// are: a design an unloaded mod defined stays whole)
export function editUserFile(file: string, key: string, block: string | null) {
  const src = fs.existsSync(file) ? fs.readFileSync(file, "utf-8") : "";
  const eol = src.includes("\r\n") ? "\r\n" : "\n";
  const body = block === null ? null : block.replace(/\r?\n/g, eol);
  const found = topBlocks(src).filter((b) => b.key === key);
  let out: string;
  if (found.length) {
    out = src;
    for (const b of [...found].reverse()) {
      // a removed block takes its line break along
      const end = body === null && out.slice(b.end, b.end + eol.length) === eol ? b.end + eol.length : b.end;
      out = out.slice(0, b.start) + (b === found[0] && body !== null ? body : "") + out.slice(end);
    }
  } else if (body !== null) {
    out = src + (src.length && !src.endsWith("\n") ? eol : "") + body + eol;
  } else {
    return false;
  }
  if (fs.existsSync(file)) fs.copyFileSync(file, file + ".mcp_backup");
  fs.writeFileSync(file, out, "utf-8");
  return true;
}
