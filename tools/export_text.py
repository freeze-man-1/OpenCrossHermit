#!/usr/bin/env python3
"""Exports the game's Japanese text for translating, and seals translations.

  export_text.py export [--data gamedata] [--pack languages/en]
      Reads your imported files and writes languages/work/<pack>/<group>.tsv:
      Japanese<TAB>translation, with any translation the pack already has.
      These files hold the game's text: they stay on your machine.

  export_text.py seal [--pack languages/en]
      Writes the translations to <pack>/text/<group>.tsv keyed by a hash of the
      Japanese, so a pack holds no game text and can be shared. Panel text (PANELS)
      is wrapped to fit here, so translate it as paragraphs: \\n only for a real break.

The hash is FNV-1a 64 of the UTF-8 (textKey in game/text/sjis.cpp).
"""
import argparse
import re
import struct
from pathlib import Path

from PIL import ImageFont

ROOT = Path(__file__).resolve().parent.parent

# Entries the game shows in a panel, whose translations seal wraps: font px, width.
PANELS = {
    "tacticsexp": (15, 405), "lectureexp": (15, 405),
    "skillexp": (12, 295), "itemexp": (12, 295), "jobexp": (12, 295), "kscapabilityup": (12, 175),
    "mappos0401": (12, 300), "words": (12, 300),
}
FONT = ROOT / "resources/fonts/NotoSansCJK-Regular.ttc"

# Where the exe keeps its strings, by what they are. Anything else is not exported.
EXE_GROUPS = [
    (0x4e4000, 0x4e6100, "multiplayer"), # the network screens' messages
    (0x4ec000, 0x4f0000, "stages"),      # briefings on the team screen; the multiplayer maps' names and notes
    (0x504000, 0x508000, "names"),       # conditions, jobs, attack kinds, units, chatter
    (0x508000, 0x511000, "help"),        # battle help and stat names
    (0x531000, 0x534000, "names"),       # script speakers and backgrounds
    (0x53c000, 0x53e000, "system"),      # save screen messages
    (0x651000, 0x653000, "items"),
    (0x66c000, 0x680000, "system"),      # news, network and other messages
]
SKIP = re.compile(
    r"DPERR|CODE_|欠番|テスト|デバック|デバッグ|^戦力\d|スクリプト|^(概要|目的|条件)\d|\(\)|::|Cno|ﾊﾞｯﾌｧ|ｱﾄﾞ|メモリ"  # debug
    r"|&&|^!\"|\n$|エリア$|^予備|\(仮\)|ワークは|指定範囲|指定座標|規則違反|経路ワーク|巡回を指定"  # asserts and notes
    r"|^[\x00-\x7f\uff61-\uff9f]?[\u4e00-\u9fff][\x00-\x7f\uff61-\uff9f]$|^[\x00-\x7f\uff61-\uff9f][\u4e00-\u9fff]$")  # bytes that look like text


def is_japanese(c):
    o = ord(c)
    return 0x3040 <= o <= 0x30ff or 0x4e00 <= o <= 0x9fff or 0xff01 <= o <= 0xff5e or 0x3001 <= o <= 0x303f


def wanted(s):
    if not any(is_japanese(c) for c in s) or any(ord(c) < 32 and c not in "\r\n\t" for c in s):
        return False
    if any(0xe000 <= ord(c) <= 0xf8ff or c == "\ufffd" for c in s):
        return False
    return not SKIP.search(s)


def exe_strings(data):
    """Strings the exe points at: code and data references to their first byte."""
    b = (data / "common/static/jp.exe").read_bytes()
    pe = struct.unpack_from("<I", b, 0x3c)[0]
    count = struct.unpack_from("<H", b, pe + 6)[0]
    opt = struct.unpack_from("<H", b, pe + 20)[0]
    image = struct.unpack_from("<I", b, pe + 52)[0]
    sections = []
    for i in range(count):
        o = pe + 24 + opt + 40 * i
        name = b[o:o + 8].rstrip(b"\0")
        size, va, raw_size, raw = struct.unpack_from("<IIII", b, o + 8)
        sections.append((name, image + va, raw, raw_size))
    refs = set()
    for name, va, raw, size in sections:
        step = 1 if name == b".text" else 4
        for i in range(raw, raw + size - 3, step):
            refs.add(int.from_bytes(b[i:i + 4], "little"))
    out = {}
    for name, va, raw, size in sections:
        if name not in (b".rdata", b".data"):
            continue
        for m in re.finditer(rb"[^\0]+", b[raw:raw + size]):
            at = va + m.start()
            if at not in refs:
                continue
            group = next((g for lo, hi, g in EXE_GROUPS if lo <= at < hi), None)
            if not group:
                continue
            try:
                s = m.group().decode("cp932")
            except UnicodeDecodeError:
                continue
            if wanted(s):
                out.setdefault(group, {}).setdefault(s, hex(at))
    return out


def ybc_lines(path):
    """A .ybc's text table (block 2); scripts break lines with a backslash."""
    b = path.read_bytes()
    blocks = struct.unpack_from("<I", b, 0)[0]
    if blocks < 3:
        return
    base = struct.unpack_from("<I", b, 4 + 8)[0]
    if base + 4 > len(b):
        return
    n = struct.unpack_from("<I", b, base)[0]
    for i in range(n):
        if base + 8 + 4 * i > len(b):
            return
        at = base + (struct.unpack_from("<I", b, base + 4 + 4 * i)[0] & 0xfffff)
        end = b.find(b"\0", at)
        raw = b[at:end if end >= 0 else len(b)]
        line, k = bytearray(), 0
        while k < len(raw):
            c = raw[k]
            if c == 0x5c:
                yield bytes(line)
                line = bytearray()
                k += 2
            elif c >= 0x80 and k + 1 < len(raw):
                line += raw[k:k + 2]
                k += 2
            else:
                line.append(c)
                k += 1
        yield bytes(line)


def ys_passages(path):
    """A ystext file's entries, each its lines joined: the game looks them up as one."""
    b = path.read_bytes()
    u32 = lambda at: struct.unpack_from("<I", b, at)[0]
    base, index = u32(4 + 8), u32(4 + 12)
    for i in range(u32(index)):
        first, n = struct.unpack_from("<HH", b, index + 4 + 4 * i)
        lines = []
        for k in range(first, first + n):
            at = base + (u32(base + 4 + 4 * k) & 0xfffff)
            lines.append(b[at:b.index(b"\0", at)])
        yield b"\n".join(lines)


def script_strings(data):
    """The battle scripts' lines: tactics/script/*.bin keep them as NUL-terminated Shift-JIS."""
    for f in sorted((data / "jp/data/tactics/script").glob("*.bin")):
        for raw in re.findall(rb"(?:[\x81-\x9f\xe0-\xef][\x40-\xfc]|[\x20-\x7e]){2,}(?=\0)", f.read_bytes()):
            try:
                s = raw.decode("cp932")
            except UnicodeDecodeError:
                continue
            if wanted(s):
                yield s, f.stem


def ybc_strings(data):
    """ystext: a group per file (item help, glossary and so on). story: what the scripts say.
    tutorial: what the tutorial's scripts say."""
    out = {}
    for f in sorted((data / "jp").rglob("*.ybc")):
        ys = f.parent.name == "ystext"
        for raw in ys_passages(f) if ys else ybc_lines(f):
            try:
                s = raw.decode("cp932")
            except UnicodeDecodeError:
                continue
            if wanted(s):
                out.setdefault(f.stem if ys else "story", {}).setdefault(s, f.stem)
    # tutorial: the team screen's tutorial (mm001.ybc and what it runs), on both discs.
    for f in sorted((data / "common/data/adv/dat").glob("*.ybc")):
        if f.stem.lower().startswith(("han", "kojin", "menu", "mm", "battlemain", "subchack")):
            for raw in ybc_lines(f):
                try:
                    s = raw.decode("cp932")
                except UnicodeDecodeError:
                    continue
                if wanted(s):
                    out.setdefault("tutorial", {}).setdefault(s, f.stem)
    return out


def text_key(s):
    h = 0xcbf29ce484222325
    for c in s.encode("utf-8"):
        h = ((h ^ c) * 0x100000001b3) & 0xffffffffffffffff
    return f"@{h:016x}"


def escape(s):
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n")


def unescape(s):
    return re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t", "r": "\r"}.get(m.group(1), m.group(1)), s)


def read_tsv(path):
    for line in path.read_text("utf-8-sig").split("\n"):
        line = line.rstrip("\r")
        if line and not line.startswith("#") and "\t" in line:
            key, value = line.split("\t", 1)
            if value:
                yield key, value


def wrap(text, px, width):
    """Breaks text at spaces to fit width, keeping its own line breaks."""
    font = ImageFont.truetype(str(FONT), px)
    out = []
    for para in text.split("\n"):
        line = ""
        for word in para.split(" "):
            joined = f"{line} {word}" if line else word
            if line and font.getlength(joined) > width:
                out.append(line)
                line = word
            else:
                line = joined
        out.append(line)
    return "\n".join(out)


def export(args):
    data, pack = Path(args.data), Path(args.pack)
    known = {}
    for f in (pack / "text").glob("*.tsv"):
        known.update(read_tsv(f))
    # Drafts win over the sealed text, whose panels have been wrapped.
    for f in (ROOT / "languages/work" / pack.name).glob("*.tsv"):
        known.update((text_key(unescape(k)), v) for k, v in read_tsv(f) if v)
    groups = exe_strings(data)
    groups["battle"] = dict(script_strings(data))
    for group, strings in ybc_strings(data).items():
        groups.setdefault(group, {}).update(strings)
    out = ROOT / "languages/work" / pack.name
    out.mkdir(parents=True, exist_ok=True)
    seen = set()
    # Battle lines go last: a line the story also says stays with the story.
    for group, strings in sorted(groups.items(), key=lambda g: (g[0] == "battle", g[0])):
        done = 0
        with open(out / f"{group}.tsv", "w", encoding="utf-8", newline="\n") as f:
            f.write(f"# {group}: Japanese<TAB>translation. Blank stays Japanese. Holds game text: do not share.\n")
            for s, where in strings.items():
                if s in seen:
                    continue
                seen.add(s)
                value = known.get(text_key(s), "")
                done += bool(value)
                f.write(f"{escape(s)}\t{value}\n")
        print(f"{group}: {done} of {len(strings)} translated")
    print(out)


def seal(args):
    pack = Path(args.pack)
    for f in sorted((ROOT / "languages/work" / pack.name).glob("*.tsv")):
        rows = list(read_tsv(f))
        (pack / "text").mkdir(exist_ok=True)
        (pack / "text" / f.name).unlink(missing_ok=True)
        if not rows:
            continue
        with open(pack / "text" / f.name, "w", encoding="utf-8", newline="\n") as o:
            o.write(f"# {f.stem}: keys are hashes of the Japanese (tools/export_text.py)\n")
            for k, v in rows:
                if f.stem in PANELS:
                    v = escape(wrap(unescape(v), *PANELS[f.stem]))
                o.write(f"{text_key(unescape(k))}\t{v}\n")
        print(f"{f.stem}: {len(rows)}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("command", choices=["export", "seal"])
    p.add_argument("--data", default=str(ROOT / "gamedata"))
    p.add_argument("--pack", default=str(ROOT / "languages/en"))
    args = p.parse_args()
    (export if args.command == "export" else seal)(args)


if __name__ == "__main__":
    main()
