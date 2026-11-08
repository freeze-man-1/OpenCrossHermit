#!/usr/bin/env python3
"""Mechanical Ghidra-C -> C++ translation for table-driven cutscene code.

Used for the opening's layer functions, which are ~6,500 lines of uniform
"byte/short field update + table lookup + draw call" code. The output keeps
the original control flow statement for statement; only memory access is
rewritten onto endian-safe accessors so it is well-defined C++:

    *(char *)(param_1 + 0x39)          -> I8(0x39)       (signed byte view)
    *(byte *)(param_1 + 0x51)          -> U8(0x51)
    *(short *)(param_1 + 0x82)         -> S16(0x82)      (proxy, little-endian)
    *(ushort *)(param_1 + 0xf2)        -> U16(0xf2)
    (&DAT_0075bb84)[i]                 -> T8(0x0075bb84 + (i))     .data tables
    *(short *)(&DAT_00757da8 + i)      -> TS16(0x00757da8 + (i))
    sRam00757db8 / _DAT_x / uRam_x     -> TS16 / TU16 at that address
    FUN_00505580(...)                  -> method named in CALLS

Usage: python3 -I tools/ghidra2cpp.py CH.exe.c Class out.inc FUN_xxxxxxxx ...
"""
import re
import sys

# Callee address -> C++ method name. Anything else is an error.
CALLS = {
    "00504f10": "opDraw",          "00505000": "opDrawSrc",
    "00505130": "opDrawSrcScale",  "00505260": "opDrawTint",
    "00505360": "opDrawTintScaleC","00505460": "opDrawStretchC",
    "00505580": "opDrawSrcTint",   "005056b0": "opDrawSrcTintRot",
    "005057f0": "opDrawSrcTintRotScale", "00505930": "opDrawSrcScaleC",
    "00505a60": "opDrawSrcSizeC",  "00505b90": "opDrawSrcSizeTintC",
    "00505cc0": "opDrawSrcAdd",
    "00410310": "fillRect",
    "00409ff0": "anmPlay",         "0040a100": "anmUpdate",  "0040a330": "anmDraw",
}

TYPE_VIEW = {  # pointer cast -> accessor on the task work area
    "char": "I8", "byte": "U8", "undefined1": "U8", "undefined": "U8",
    "short": "S16", "undefined2": "S16", "ushort": "U16",
    "int": "S32", "uint": "U32", "undefined4": "U32",
}
TABLE_VIEW = {"short": "TS16", "undefined2": "TS16", "ushort": "TU16", "char": "TI8", "byte": "T8",
              "undefined1": "T8", "undefined": "T8"}
LOCAL_TYPES = {"int": "std::int32_t", "uint": "std::uint32_t", "short": "std::int16_t",
               "ushort": "std::uint16_t", "char": "std::int8_t", "byte": "std::uint8_t",
               "undefined1": "std::uint8_t", "undefined2": "std::uint16_t", "undefined4": "std::uint32_t"}


def extract(src: str, name: str) -> str:
    src = src.replace("\r", "")   # the export has CRLF line ends; the patterns expect \n
    m = re.search(rf"^\w[^\n]*\b{name}\(.*?\n\{{\n.*?^\}}\n", src, re.S | re.M)
    if not m:
        raise SystemExit(f"{name}: not found")
    return m.group(0)


def balanced(text: str, start: int) -> int:
    """Index just past the bracket that closes the one at text[start]."""
    open_ch = text[start]
    close_ch = {"(": ")", "[": "]"}[open_ch]
    depth = 0
    for i in range(start, len(text)):
        if text[i] == open_ch:
            depth += 1
        elif text[i] == close_ch:
            depth -= 1
            if depth == 0:
                return i + 1
    raise ValueError("unbalanced")


def rewrite_tables(body: str) -> str:
    # *(type *)(&DAT_x + expr)
    pat = re.compile(r"\*\((short|undefined2|ushort|char|byte|undefined1) \*\)\(&(?:DAT|PTR_DAT)_([0-9a-f]{8})")
    while (m := pat.search(body)):
        open_at = body.index("(", m.end(1) + 2)          # the "(&DAT" paren
        end = balanced(body, open_at)
        inner = body[m.end():end - 1].strip()             # "+ expr" or ""
        expr = f"0x{m.group(2)}" + (f" {inner}" if inner else "")
        body = body[:m.start()] + f"{TABLE_VIEW[m.group(1)]}({expr})" + body[end:]
    # (&DAT_x)[expr]
    pat = re.compile(r"\(&(?:DAT|PTR_DAT)_([0-9a-f]{8})\)\[")
    while (m := pat.search(body)):
        end = balanced(body, m.end() - 1)
        expr = body[m.end():end - 1]
        body = body[:m.start()] + f"T8(0x{m.group(1)} + ({expr}))" + body[end:]
    # bare scalars, with Ghidra sub-piece suffixes
    def scalar(m):
        pre, addr, piece = m.group(1), int(m.group(2), 16), m.group(3)
        if piece == "._2_2_":
            addr += 2
        view = "TU16" if pre == "uRam" else "TS16"
        return f"{view}(0x{addr:08x})"
    body = re.sub(r"\b(sRam|uRam|_DAT_|PTR_DAT_|DAT_)([0-9a-f]{8})(\._[02]_2_)?", scalar, body)
    return body


def rewrite_fields(body: str) -> str:
    pat = re.compile(r"\*\((\w+) \*\)\(param_1 \+ (0x[0-9a-f]+|\d+)\)")
    body = pat.sub(lambda m: f"{TYPE_VIEW[m.group(1)]}({m.group(2)})", body)
    # remaining address arithmetic on `this` is an offset into the work area
    body = re.sub(r"\bparam_1 \+ (0x[0-9a-f]+|\d+)", r"OFF(\1)", body)
    return body


def rewrite_ghidra_isms(body: str) -> str:
    # white with alpha: (a << 24) | 0xffffff
    body = re.sub(r"CONCAT31\(\(uint3\)\(CONCAT22\(\(short\)\(\(\(uint\)(.*?) << 0x18\) >>\s*0x10\),0xff00\) >> 8\) \|\s*0xff00,0xff\)",
                  r"whiteA(\1)", body, flags=re.S)
    body = re.sub(r"CONCAT22\(\(short\)\(uVar2 >> 0x10\),\s*", "(", body)
    body = re.sub(r"CONCAT22\(extraout_var,\s*", "(", body)
    body = re.sub(r"^\s*uVar2 = CONCAT31\(\(int3\)\(\(uint\)extraout_EDX >> 8\),.*?\n", "", body, flags=re.M)
    # low-byte updates of the sprite-index local
    body = re.sub(r"(local_\w+) = CONCAT31\(\1\._1_3_,\(char\)\1 \+ '\\x01'\);",
                  r"\1 = (\1 & ~0xFFu) | std::uint8_t(\1 + 1);", body)
    body = re.sub(r"(local_\w+) = CONCAT31\(\1\._1_3_,(0x[0-9a-f]+|\d+)\);", r"\1 = (\1 & ~0xFFu) | \2;", body)
    body = re.sub(r"_(local_\w+) = CONCAT31\(uStack_\w+,(0x[0-9a-f]+|\d+)\);", r"\1 = (\1 & ~0xFFu) | \2;", body)
    body = re.sub(r"(local_\w+)\._0_1_ = (0x[0-9a-f]+|\d+);", r"\1 = (\1 & ~0xFFu) | \2;", body)
    # any other low-byte store: x = CONCAT31(x._1_3_, v)
    body = re.sub(r"(local_\w+) = CONCAT31\(\1\._1_3_,(.+?)\);$", r"\1 = (\1 & ~0xFFu) | std::uint8_t(\2);", body, flags=re.M)
    body = re.sub(r"\(uint\)(local_\w+)\._1_3_ << 8", r"(\1 & ~0xFFu)", body)
    return body


def mark_fallthrough(body: str) -> str:
    """Ghidra prints `break` wherever the binary leaves a case; a case label
    reached any other way is a real fall-through in the original."""
    out, prev = [], ""
    for line in body.split("\n"):
        s = line.strip()
        if re.match(r"(case .+|default):$", s) and prev and not (
                prev in ("break;", "return;", "{") or prev.startswith(("switch", "case ", "default:"))):
            indent = line[:len(line) - len(line.lstrip())]
            out.append(f"{indent}  [[fallthrough]];   // as in the original binary")
        out.append(line)
        if s:
            prev = s
    return "\n".join(out)


def rewrite_calls(body: str) -> str:
    def call(m):
        addr = m.group(1)
        if addr not in CALLS:
            raise SystemExit(f"unmapped call FUN_{addr}")
        return CALLS[addr] + "("
    return re.sub(r"\bFUN_([0-9a-f]{8})\(", call, body)


def translate(fn_text: str, cls: str) -> str:
    head, body = fn_text.split("{", 1)
    name = re.search(r"FUN_([0-9a-f]{8})", head).group(1)
    body = body.rsplit("}", 1)[0]

    locals_ = {n: t for t, n in re.findall(r"^\s+(int|uint|short|ushort|char|byte|undefined[124]) (\w+);$", body, re.M)}
    body = re.sub(r"^\s+(?:int|uint|short|ushort|char|byte|float|double|float10|code|void|int3|uint3|"
                  r"undefined\d?)\s*\*?\s*\*?\w+(?: \[\d+\])?;\n", "", body, flags=re.M)   # declarations
    body = re.sub(r"^\s+puVar\d+ = local_\w+;\n", "", body, flags=re.M)
    body = re.sub(r"^\s+for \(iVar\d+ = 0x[0-9a-f]+; iVar\d+ != 0; iVar\d+ = iVar\d+ \+ -1\) \{\n"
                  r"\s+\*puVar\d+ = 0xcccccccc;\n\s+puVar\d+ = puVar\d+ \+ 1;\n\s+\}\n", "", body, flags=re.M)
    alias = re.search(r"^\s+(local_\w+) = param_1;\n", body, re.M)
    if alias:
        body = body.replace(alias.group(0), "")
        body = re.sub(rf"\b{alias.group(1)}\b", "param_1", body)
        locals_.pop(alias.group(1), None)
    body = re.sub(r"^\s+\w+ = 0x[0-9a-f]{6,8};\n\s+__chkesp\(\);\n\s+return;\n", "", body, flags=re.M)
    body = re.sub(r"^\s+__chkesp\(\);\n", "", body, flags=re.M)
    body = re.sub(r"^\s+return;\n$", "", body)

    body = body.replace("FUN_004077c0(DAT_007a49fc);", "anmSpriteBind();")   # sprite at +0x1e0
    body = rewrite_ghidra_isms(body)
    body = rewrite_tables(body)
    body = rewrite_fields(body)
    body = rewrite_calls(body)
    body = mark_fallthrough(body)
    if re.search(r"\bparam_1\b|CONCAT|extraout|_\d_\d_", body):
        bad = re.search(r".*(\bparam_1\b|CONCAT|extraout|_\d_\d_).*", body).group(0)
        raise SystemExit(f"FUN_{name}: untranslated construct: {bad.strip()}")

    used = sorted(set(re.findall(r"\b(local_\w+|[a-z]Var\d+)\b", body)))
    decls = "".join(f"    {LOCAL_TYPES.get(locals_.get(v, 'uint'), 'std::uint32_t')} {v} = 0;\n" for v in used)
    return f"// FUN_{name}\nvoid {cls}::op_{name}() {{\n{decls}{body.rstrip()}\n}}\n"


def main() -> None:
    src = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    cls, out, names = sys.argv[2], sys.argv[3], sys.argv[4:]
    parts = ["// GENERATED by tools/ghidra2cpp.py from the Ghidra export. Do not edit;\n"
             "// fix the generator or hand-port the function instead.\n"
             "// NOLINTBEGIN\n"]
    parts += [translate(extract(src, n), cls) for n in names]
    parts.append("// NOLINTEND\n")
    open(out, "w").write("\n".join(parts))
    print(f"wrote {len(names)} functions to {out}")


if __name__ == "__main__":
    main()
