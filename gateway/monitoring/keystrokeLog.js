const fs = require("fs");
const path = require("path");

// Written by script(1), wrapped around every interactive student terminal by
// student-bash.bashrc. Each terminal leaves a pair of files:
//   <stamp>-<pid>.in      "Script started on …\n", then the raw bytes typed
//   <stamp>-<pid>.timing  advanced timing log, one entry per write:
//     H 0.000000 START_TIME 2026-10-14 11:29:06+00:00
//     I 1.008822 35          35 typed bytes, 1.008822s after the previous entry
//     H 0.000000 DURATION 4.151365   (once the terminal exits)
const SESSION_PATTERN = /^\d{8}T\d{6}Z-\d+$/u;
const USERNAME_PATTERN = /^[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?$/u;

/** @returns {{startedAt: number|null, endedAt: number|null, chunks: {at: number, bytes: Buffer}[]}} */
function parseKeystrokeSession(timingText, input) {
  let startedAt = null;
  let duration = null;
  let elapsed = 0;
  const entries = [];
  for (const line of timingText.split("\n")) {
    const match = /^([HI]) (\d+(?:\.\d+)?) (.*)$/u.exec(line);
    if (!match) {
      continue;
    }
    elapsed += Number(match[2]);
    if (match[1] === "I") {
      const size = Number(match[3]);
      if (Number.isInteger(size) && size > 0) {
        entries.push({ elapsed, size });
      }
    } else if (match[3].startsWith("START_TIME ")) {
      // "2026-10-14 11:29:06+00:00"
      const at = Date.parse(match[3].slice("START_TIME ".length).replace(" ", "T"));
      startedAt = Number.isNaN(at) ? null : at;
    } else if (match[3].startsWith("DURATION ")) {
      duration = Number(match[3].slice("DURATION ".length));
    }
  }
  if (startedAt === null) {
    return { startedAt: null, endedAt: null, chunks: [] };
  }
  let offset = 0;
  if (input.subarray(0, 15).toString("latin1") === "Script started ") {
    offset = input.indexOf(0x0a) + 1;
  }
  const chunks = [];
  for (const { elapsed: at, size } of entries) {
    // The two files are flushed separately; a live terminal may be mid-write.
    if (offset + size > input.length) {
      break;
    }
    chunks.push({ at: startedAt + Math.round(at * 1000), bytes: input.subarray(offset, offset + size) });
    offset += size;
  }
  const endedAt = Number.isFinite(duration) ? startedAt + Math.round(duration * 1000) : null;
  return { startedAt, endedAt, chunks };
}

const CSI_KEYS = {
  A: "↑",
  B: "↓",
  C: "→",
  D: "←",
  H: "Home",
  F: "End",
  Z: "Shift+Tab",
};
const TILDE_KEYS = {
  1: "Home",
  2: "Insert",
  3: "Delete",
  4: "End",
  5: "PageUp",
  6: "PageDown",
  7: "Home",
  8: "End",
  200: "Paste start",
  201: "Paste end",
};
const MODIFIERS = { 2: "Shift+", 3: "Alt+", 4: "Shift+Alt+", 5: "Ctrl+", 6: "Ctrl+Shift+", 7: "Ctrl+Alt+" };

function controlKey(code) {
  switch (code) {
    case 0x0d:
    case 0x0a:
      return "Enter";
    case 0x09:
      return "Tab";
    case 0x7f:
    case 0x08:
      return "Backspace";
    case 0x00:
      return "Ctrl+Space";
    default:
      return `Ctrl+${String.fromCharCode(code + 0x40)}`;
  }
}

/**
 * Splits typed bytes into text and named keys. Replies the terminal sends on
 * its own (cursor position, colour queries) are dropped: nobody typed them.
 * @returns {(string|{key: string})[]}
 */
function decodeKeys(bytes) {
  const tokens = [];
  const text = (s) => {
    if (typeof tokens[tokens.length - 1] === "string") {
      tokens[tokens.length - 1] += s;
    } else {
      tokens.push(s);
    }
  };
  const str = bytes.toString("utf8");
  let i = 0;
  while (i < str.length) {
    const ch = str[i];
    const code = ch.charCodeAt(0);
    if (ch !== "\x1b") {
      if (code < 0x20 || code === 0x7f) {
        tokens.push({ key: controlKey(code) });
      } else {
        text(ch);
      }
      i += 1;
      continue;
    }
    const next = str[i + 1];
    if (next === "[" || next === "O") {
      const match = /^[\x30-\x3f]*[\x20-\x2f]*[\x40-\x7e]/u.exec(str.slice(i + 2));
      if (!match) {
        tokens.push({ key: "Esc" });
        i += 1;
        continue;
      }
      const seq = match[0];
      i += 2 + seq.length;
      const final = seq[seq.length - 1];
      const params = seq.slice(0, -1).split(";");
      if (/^[?>]/u.test(seq) || final === "R" || ((final === "I" || final === "O") && seq.length === 1 && next === "[")) {
        continue; // device attributes, cursor report, focus in/out
      }
      const modifier = MODIFIERS[params[1]] || "";
      if (final === "~" && TILDE_KEYS[params[0]]) {
        tokens.push({ key: modifier + TILDE_KEYS[params[0]] });
      } else if (CSI_KEYS[final]) {
        tokens.push({ key: modifier + CSI_KEYS[final] });
      } else if (next === "O" && /^[P-S]$/u.test(final)) {
        tokens.push({ key: `F${final.charCodeAt(0) - 0x4f}` });
      } else {
        tokens.push({ key: `Esc ${next}${seq}` });
      }
      continue;
    }
    if (next === "]" || next === "P" || next === "_") {
      // OSC / DCS / APC reply, up to BEL or ST.
      const end = str.slice(i + 2).search(/\x07|\x1b\\/u);
      i = end < 0 ? str.length : i + 2 + end + (str[i + 2 + end] === "\x07" ? 1 : 2);
      continue;
    }
    if (next !== undefined && next !== "\x1b") {
      const nextCode = next.charCodeAt(0);
      tokens.push({ key: nextCode < 0x20 || nextCode === 0x7f ? `Alt+${controlKey(nextCode)}` : `Alt+${next}` });
      i += 2;
      continue;
    }
    tokens.push({ key: "Esc" });
    i += 1;
  }
  return tokens;
}

// Keys whose effect on the command line cannot be seen in the input alone.
const INEXACT_KEYS = new Set(["Tab", "↑", "↓", "Ctrl+R", "Ctrl+P", "Ctrl+N", "Ctrl+Y", "PageUp", "PageDown"]);

/**
 * Replays a line's keys through a minimal readline so staff see what was most
 * likely run. `exact` is false once a key pulls in text that was never typed
 * (completion, history, yank) or the keys belong to a full-screen program.
 */
function replayLine(tokens) {
  const chars = [];
  let cursor = 0;
  let exact = true;
  for (const token of tokens) {
    if (typeof token === "string") {
      const added = [...token];
      chars.splice(cursor, 0, ...added);
      cursor += added.length;
      continue;
    }
    switch (token.key) {
      case "Backspace":
      case "Ctrl+H":
        if (cursor > 0) {
          chars.splice(cursor - 1, 1);
          cursor -= 1;
        }
        break;
      case "Delete":
        chars.splice(cursor, 1);
        break;
      case "←":
      case "Ctrl+B":
        cursor = Math.max(0, cursor - 1);
        break;
      case "→":
      case "Ctrl+F":
        cursor = Math.min(chars.length, cursor + 1);
        break;
      case "Home":
      case "Ctrl+A":
        cursor = 0;
        break;
      case "End":
      case "Ctrl+E":
        cursor = chars.length;
        break;
      case "Ctrl+U":
        chars.splice(0, cursor);
        cursor = 0;
        break;
      case "Ctrl+K":
        chars.splice(cursor);
        break;
      case "Ctrl+W": {
        let start = cursor;
        while (start > 0 && chars[start - 1] === " ") start -= 1;
        while (start > 0 && chars[start - 1] !== " ") start -= 1;
        chars.splice(start, cursor - start);
        cursor = start;
        break;
      }
      case "Paste start":
      case "Paste end":
        break;
      default:
        if (INEXACT_KEYS.has(token.key) || token.key.startsWith("Esc") || token.key.startsWith("Alt+")) {
          exact = false;
        }
    }
  }
  return { text: chars.join(""), exact };
}

/**
 * Groups a session's keystrokes into lines, each ended by Enter, Ctrl+C or
 * Ctrl+D. Lines keep the time of their first key.
 * @returns {{at: number, keys: (string|{key: string})[], text: string, exact: boolean, end: string|null}[]}
 */
function keystrokeLines(chunks) {
  const lines = [];
  let current = null;
  const finish = (end) => {
    lines.push({ ...current, ...replayLine(current.keys), end });
    current = null;
  };
  for (const chunk of chunks) {
    for (const token of decodeKeys(chunk.bytes)) {
      if (!current) {
        current = { at: chunk.at, keys: [] };
      }
      const key = typeof token === "string" ? null : token.key;
      if (key === "Enter" || key === "Ctrl+C" || key === "Ctrl+D") {
        finish(key);
        continue;
      }
      const last = current.keys[current.keys.length - 1];
      if (typeof token === "string" && typeof last === "string") {
        current.keys[current.keys.length - 1] = last + token;
      } else {
        current.keys.push(token);
      }
    }
  }
  if (current) {
    finish(null);
  }
  return lines;
}

/** Reads <dir>/<username>/*.{in,timing}, re-parsing a terminal only when its files change. */
function createKeystrokeLogReader({ dir }) {
  const cache = new Map();

  function readSession(base) {
    let timingStat;
    let inputStat;
    try {
      timingStat = fs.statSync(`${base}.timing`);
      inputStat = fs.statSync(`${base}.in`);
    } catch {
      cache.delete(base);
      return null;
    }
    const key = `${timingStat.mtimeMs}:${timingStat.size}:${inputStat.mtimeMs}:${inputStat.size}`;
    const cached = cache.get(base);
    if (cached && cached.key === key) {
      return cached.session;
    }
    let session = null;
    try {
      const parsed = parseKeystrokeSession(fs.readFileSync(`${base}.timing`, "utf8"), fs.readFileSync(`${base}.in`));
      if (parsed.startedAt !== null) {
        session = { startedAt: parsed.startedAt, endedAt: parsed.endedAt, lines: keystrokeLines(parsed.chunks) };
      }
    } catch {
      // Unreadable now; try again on the next request.
    }
    cache.set(base, { key, session });
    return session;
  }

  /** @returns {{id: string, startedAt: number, endedAt: number|null, lines: object[]}[]} oldest first */
  function sessions(username) {
    if (!dir || typeof username !== "string" || !USERNAME_PATTERN.test(username)) {
      return [];
    }
    let names;
    try {
      names = fs.readdirSync(path.join(dir, username));
    } catch {
      return [];
    }
    const ids = names
      .filter((name) => name.endsWith(".timing"))
      .map((name) => name.slice(0, -".timing".length))
      .filter((id) => SESSION_PATTERN.test(id))
      .sort();
    const result = [];
    for (const id of ids) {
      const session = readSession(path.join(dir, username, id));
      if (session) {
        result.push({ id, ...session });
      }
    }
    return result;
  }

  return {
    available: () => Boolean(dir) && fs.existsSync(dir),
    sessions,
  };
}

module.exports = { parseKeystrokeSession, decodeKeys, replayLine, keystrokeLines, createKeystrokeLogReader };
