#!/usr/bin/env python3
"""Builds resources/ch_data.chpatch: how the importer rebuilds ch_data.bin
from the player's own executables.

  python3 -I tools/make_ch_patch.py ch_data.bin JP12.exe TWDISC.exe OUT.chpatch

ch_data.bin is VA 0x592000..0x80c000 (.rdata + .data) of the TW build the
port was decompiled from (a later build than the one on the TW disc). Game
logic reads its tables by that build's addresses in both versions, so the
importer has to recreate it:
  source 0  "Cross Hermit.exe" of the JP 1.2 update (always required)
  source 1  "CROSS HERMIT.EXE" of the TW disc (optional)
The patch is a list of operations over the target: runs of zeros, copies
from a source file, and literal bytes. The JP exe covers all it can; copies
from the TW disc's exe are used only for that build's Big5 text, which the
patch does not contain; what neither supplies otherwise (pointer tables --
the strings sit at other addresses in each build --, changed numbers, file
names) is literal. Without the TW disc the text copies stay zero; the JP
version does not read them (CH_TRACE_TWONLY in game/state/exe_image.cpp
reports any read).

File: "CHPATCH1", then a zlib stream of
  u32 target VA, u32 target size, u32 crc32 (both sources), u32 crc32 (source 0 only)
  u8 source count, per source: u32 size, u32 crc32
  u32 op count, per op: u8 kind (0 zero / 1 copy / 2 literal), u32 length,
                        copy: u8 source, u32 offset; literal: the bytes
"""
import struct
import sys
import zlib

BASE = 0x592000
K, STEP = 16, 4


def index(s):
    d = {}
    for i in range(0, len(s) - K, STEP):
        k = s[i:i + K]
        if k.count(0) > 12:
            continue
        d.setdefault(k, i)
    return d


def big5_text(tgt):
    """Which bytes are Big5 text: NUL-delimited runs with non-ASCII bytes that
    decode as Big5, not counting dword-aligned values that look like pointers
    into the image."""
    n = len(tgt)
    ptr = bytearray(n)
    for i in range(0, n - 3, 4):
        if 0x401000 <= struct.unpack_from("<I", tgt, i)[0] < 0x810000:
            ptr[i:i + 4] = b"\1\1\1\1"
    text = bytearray(n)
    i = 0
    while i < n:
        if tgt[i] == 0 or ptr[i]:
            i += 1
            continue
        j = i
        while j < n and tgt[j] != 0 and not ptr[j]:
            j += 1
        run = tgt[i:j]
        if any(b >= 0x80 for b in run):
            try:
                run.decode("cp950")
                text[i:j] = b"\1" * (j - i)
            except UnicodeDecodeError:
                pass
        i = j
    return text


def main():
    tgt = open(sys.argv[1], "rb").read()
    srcs = [open(a, "rb").read() for a in sys.argv[2:4]]
    idx = [index(s) for s in srcs]
    text = big5_text(tgt)

    def match(si, p, hi):
        s, d = srcs[si], idx[si]
        best = (0, 0)
        for j in range(STEP):
            q = d.get(tgt[p + j:p + j + K])
            if q is None or q < j:
                continue
            q -= j
            n = 0
            while p + n < hi and q + n < len(s) and tgt[p + n] == s[q + n]:
                n += 1
            if n > best[0]:
                best = (n, q)
        return best

    def short_match(si, p, hi):
        """Longest copy at p from source si (any length >= 4), by search."""
        s = srcs[si]
        n = min(hi - p, 64)
        while n >= 4:
            q = s.find(tgt[p:p + n])
            if q >= 0:
                m = n
                while p + m < hi and q + m < len(s) and tgt[p + m] == s[q + m]:
                    m += 1
                return m, q
            n //= 2
        return 0, 0

    # ops: (kind, start, length, source, offset); kind 0 zeros, 1 copy, 2 literal.
    # Pass 1: zero runs and copies from the JP exe.
    ops, p, n_t = [], 0, len(tgt)

    def literal(out, a, b):
        if b <= a:
            return
        if out and out[-1][0] == 2 and out[-1][1] + out[-1][2] == a:
            out[-1] = (2, out[-1][1], out[-1][2] + b - a, 0, 0)
        else:
            out.append((2, a, b - a, 0, 0))
    while p < n_t:
        z = p
        while z < n_t and tgt[z] == 0:
            z += 1
        if z - p >= 8:
            ops.append((0, p, z - p, 0, 0))
            p = z
            continue
        n, q = match(0, p, n_t)
        if n >= K:
            ops.append((1, p, n, 0, q))
            p += n
        else:
            literal(ops, p, p + 1)
            p += 1

    # Pass 2: in what is left, the Big5 text runs from the TW disc's exe.
    final = []
    for op in ops:
        if op[0] != 2:
            final.append(op)
            continue
        p, hi = op[1], op[1] + op[2]
        while p < hi:
            if not text[p]:
                q = p
                while q < hi and not text[q]:
                    q += 1
                literal(final, p, q)
                p = q
                continue
            e = p
            while e < hi and text[e]:
                e += 1
            n, q = match(1, p, e)
            if n < 4:
                n, q = short_match(1, p, e)
            if n >= 2:
                final.append((1, p, n, 1, q))
                p += n
            else:
                literal(final, p, p + 1)
                p += 1
    ops = final

    def apply(have):
        out = bytearray()
        for kind, start, n, si, q in ops:
            if kind == 0:
                out += bytes(n)
            elif kind == 2:
                out += tgt[start:start + n]
            else:
                out += srcs[si][q:q + n] if si in have else bytes(n)
        return bytes(out)
    full, jp_only = apply({0, 1}), apply({0})
    if full != tgt:
        raise SystemExit("patch does not rebuild the target")
    leaked = sum(sum(text[start:start + n]) for kind, start, n, si, q in ops if kind == 2)

    body = struct.pack("<IIII", BASE, len(tgt), zlib.crc32(full), zlib.crc32(jp_only))
    body += struct.pack("<B", len(srcs))
    for s in srcs:
        body += struct.pack("<II", len(s), zlib.crc32(s))
    body += struct.pack("<I", len(ops))
    for kind, start, n, si, q in ops:
        if kind == 0:
            body += struct.pack("<BI", 0, n)
        elif kind == 1:
            body += struct.pack("<BIBI", 1, n, si, q)
        else:
            body += struct.pack("<BI", 2, n) + tgt[start:start + n]
    open(sys.argv[4], "wb").write(b"CHPATCH1" + zlib.compress(body, 9))
    print(f"{len(ops)} ops, {sum(o[2] for o in ops if o[0] == 2)} literal bytes "
          f"({leaked} of them Big5 text the TW exe lacks), "
          f"{sum(o[2] for o in ops if o[0] == 1 and o[3] == 1)} bytes of text from the TW disc exe -> {sys.argv[4]}")


if __name__ == "__main__":
    main()
