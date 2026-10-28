// Save games on disk: <user dir>\save games\<campaign>\*.sav. A .sav is a zip whose "meta" entry
// holds the save's header in the game's text format (version, empire name, game date, ironman ...).
// Only the zip's directory and that small entry are read, never the game state.
import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";

export interface SaveMeta {
  version?: string;
  name?: string;
  date?: string;
  ironman?: boolean;
  required_dlcs?: string[];
}

export interface SaveFile {
  campaign: string;
  file: string;
  modified: string;
  meta: SaveMeta;
}

function readAt(fd: number, offset: number, length: number): Buffer {
  const buf = Buffer.alloc(length);
  const n = fs.readSync(fd, buf, 0, length, offset);
  return buf.subarray(0, n);
}

// the "meta" entry of a .sav (zip): end-of-central-directory -> central directory -> local header
function readMetaEntry(file: string): string | null {
  const fd = fs.openSync(file, "r");
  try {
    const size = fs.fstatSync(fd).size;
    const tailLen = Math.min(size, 0x10000 + 22);
    const tail = readAt(fd, size - tailLen, tailLen);
    let eocd = -1;
    for (let i = tail.length - 22; i >= 0; i--) {
      if (tail.readUInt32LE(i) === 0x06054b50) { eocd = i; break; }
    }
    if (eocd < 0) return null;
    const entries = tail.readUInt16LE(eocd + 10);
    const cdSize = tail.readUInt32LE(eocd + 12);
    const cdOffset = tail.readUInt32LE(eocd + 16);
    const cd = readAt(fd, cdOffset, cdSize);
    let p = 0;
    for (let e = 0; e < entries && p + 46 <= cd.length; e++) {
      if (cd.readUInt32LE(p) !== 0x02014b50) break;
      const method = cd.readUInt16LE(p + 10);
      const compSize = cd.readUInt32LE(p + 20);
      const nameLen = cd.readUInt16LE(p + 28), extraLen = cd.readUInt16LE(p + 30), commentLen = cd.readUInt16LE(p + 32);
      const localOffset = cd.readUInt32LE(p + 42);
      const name = cd.subarray(p + 46, p + 46 + nameLen).toString("utf8");
      p += 46 + nameLen + extraLen + commentLen;
      if (name !== "meta") continue;
      const lh = readAt(fd, localOffset, 30);
      const dataStart = localOffset + 30 + lh.readUInt16LE(26) + lh.readUInt16LE(28);
      const data = readAt(fd, dataStart, compSize);
      if (method === 0) return data.toString("utf8");
      if (method === 8) return zlib.inflateRawSync(data).toString("utf8");
      return null;
    }
    return null;
  } finally {
    fs.closeSync(fd);
  }
}

function parseMeta(text: string): SaveMeta {
  const str = (key: string) => text.match(new RegExp(`(?:^|\\n)\\s*${key}\\s*=\\s*"([^"]*)"`))?.[1];
  const meta: SaveMeta = { version: str("version"), name: str("name"), date: str("date") };
  const iron = text.match(/(?:^|\n)\s*ironman\s*=\s*(yes|no)/);
  if (iron) meta.ironman = iron[1] === "yes";
  const dlcs = text.match(/required_dlcs\s*=\s*\{([^}]*)\}/);
  if (dlcs) meta.required_dlcs = [...dlcs[1].matchAll(/"([^"]+)"/g)].map((m) => m[1]);
  return meta;
}

export function saveMeta(file: string): SaveMeta {
  try {
    const text = readMetaEntry(file);
    return text ? parseMeta(text) : {};
  } catch {
    return {};
  }
}

export function savesRoot(userDir: string): string {
  return path.join(userDir, "save games");
}

// one campaign's saves, newest first
export function listCampaignSaves(userDir: string, campaign: string, withMeta = true): SaveFile[] {
  const dir = path.join(savesRoot(userDir), campaign);
  if (!fs.existsSync(dir)) return [];
  return fs.readdirSync(dir)
    .filter((f) => f.toLowerCase().endsWith(".sav"))
    .map((f) => ({ f, st: fs.statSync(path.join(dir, f)) }))
    .sort((a, b) => b.st.mtimeMs - a.st.mtimeMs)
    .map(({ f, st }) => ({
      campaign,
      file: f,
      modified: st.mtime.toISOString(),
      meta: withMeta ? saveMeta(path.join(dir, f)) : {},
    }));
}

export interface CampaignSummary {
  campaign: string;
  saves: number;
  latest_file: string;
  latest_modified: string;
  empire?: string;
  date?: string;
  ironman?: boolean;
}

// every campaign folder with its newest save's header, newest campaign first
export function listCampaigns(userDir: string): CampaignSummary[] {
  const root = savesRoot(userDir);
  if (!fs.existsSync(root)) return [];
  const out: CampaignSummary[] = [];
  for (const c of fs.readdirSync(root)) {
    if (!fs.statSync(path.join(root, c)).isDirectory()) continue;
    const saves = listCampaignSaves(userDir, c, false);
    if (!saves.length) continue;
    const meta = saveMeta(path.join(root, c, saves[0].file));
    out.push({ campaign: c, saves: saves.length, latest_file: saves[0].file, latest_modified: saves[0].modified,
               empire: meta.name, date: meta.date, ironman: meta.ironman });
  }
  return out.sort((a, b) => b.latest_modified.localeCompare(a.latest_modified));
}

// the save the main menu's "Continue" opens: continue_game.json's title ("save games/<campaign>/<file>")
export function continueTarget(userDir: string): { campaign: string; file: string } | null {
  try {
    const j = JSON.parse(fs.readFileSync(path.join(userDir, "continue_game.json"), "utf8"));
    const m = String(j.title ?? "").replace(/\\/g, "/").match(/^save games\/([^/]+)\/(.+?)(\.sav)?$/);
    if (!m) return null;
    const file = m[2] + ".sav";
    return fs.existsSync(path.join(savesRoot(userDir), m[1], file)) ? { campaign: m[1], file } : null;
  } catch {
    return null;
  }
}
