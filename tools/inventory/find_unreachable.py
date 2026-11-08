#!/usr/bin/env python3
"""Lists the yoshida (battle) functions nothing in ch.exe can reach.

A function counts as reachable when any of these holds:
  - a function body in either decompile (CH.exe.c, ch_battle_v2.c) names it
    (a call, or a pointer passed along), and that function is reachable;
  - it is named outside any function body (data initialisers);
  - ch.exe holds its address as a 32-bit value anywhere (tables, vtables,
    pushed constants);
  - a direct E8 / E9 call or jump in ch.exe's .text lands on it from a
    reachable function (catches calls the decompiles leave out).
Only functions whose assert strings place them in src\\yoshida are judged;
everything else (CRT, engine) is taken as reachable.

Prints override lines (address,not needed,,note) for the ones still marked
missing or partial in functions.csv.
"""
import argparse
import bisect
import collections
import csv
import os
import re
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
ap = argparse.ArgumentParser()
ap.add_argument('--full', default='/home/local-admin/Downloads/CH.exe.c')
ap.add_argument('--battle', default=os.path.join(ROOT, 'tools/decomp/ch_battle_v2.c'))
ap.add_argument('--exe', default='/home/local-admin/Documents/CrossHermit6/ch.exe')
ap.add_argument('--functions', default=os.path.join(HERE, 'functions.csv'))
args = ap.parse_args()

REF = re.compile(r'FUN_([0-9a-f]{8})')
DEF = re.compile(r'^[A-Za-z_][^;]*?\bFUN_([0-9a-f]{8})\(')


def scan(path):
    """callers {callee: {caller}}, names used outside bodies, defined names."""
    callers, outside, defined = collections.defaultdict(set), set(), set()
    cur = pending = None
    for line in open(path, encoding='latin-1'):
        s = line.rstrip('\r\n')
        if cur is None:
            if s == '{' and pending is not None:
                cur, pending = pending, None
                defined.add(cur)
                continue
            m = DEF.match(s)
            if m and not s.endswith(';'):
                pending = int(m.group(1), 16)
                continue
            if pending is not None and (s == '' or s[:1] in (' ', '\t')):
                continue                       # the signature's next lines
            pending = None
            if not s.startswith('//'):
                outside.update(int(x, 16) for x in REF.findall(s))
        elif s == '}':
            cur = None
        else:
            for x in REF.findall(s):
                if int(x, 16) != cur:
                    callers[int(x, 16)].add(cur)
    return callers, outside, defined


def main():
    callers, outside, defined = collections.defaultdict(set), set(), set()
    for p in (args.full, args.battle):
        c, o, d = scan(p)
        for k, v in c.items():
            callers[k] |= v
        outside |= o
        defined |= d
    exe = open(args.exe, 'rb').read()
    pe = struct.unpack_from('<I', exe, 0x3c)[0]
    base = struct.unpack_from('<I', exe, pe + 0x34)[0]
    opt = struct.unpack_from('<H', exe, pe + 0x14)[0]
    sec = pe + 0x18 + opt                      # the first section: .text
    va, raw, rawoff = struct.unpack_from('<I', exe, sec + 12)[0], struct.unpack_from('<I', exe, sec + 16)[0], \
        struct.unpack_from('<I', exe, sec + 20)[0]
    text = exe[rawoff:rawoff + raw]
    sites = collections.defaultdict(list)
    for i in range(len(text) - 5):
        if text[i] in (0xE8, 0xE9):
            sites[base + va + i + 5 + struct.unpack_from('<i', text, i + 1)[0]].append(base + va + i)
    starts = sorted(defined)

    def owner(a):
        i = bisect.bisect_right(starts, a) - 1
        return starts[i] if i >= 0 else None

    rows = list(csv.DictReader(open(args.functions, encoding='utf-8')))
    info = {int(r['address'], 16): r for r in rows if re.fullmatch('[0-9a-f]{8}', r['address'])}
    judged = {a for a in defined if a in info and info[a]['source_file'].lower().startswith('yoshida')}
    pinned = {a for a in judged if a in outside or exe.count(struct.pack('<I', a))}

    dead = set()
    while True:
        new = set()
        for a in judged - pinned - dead:
            if all(c in dead for c in callers[a]) and all(owner(s) in dead or owner(s) == a for s in sites.get(a, [])):
                new.add(a)
        # a direct call from a live function revives a candidate
        if not new:
            break
        dead |= new
        revived = {a for a in dead if any(owner(s) not in dead and owner(s) != a for s in sites.get(a, []))}
        if revived:
            dead -= revived
            pinned |= revived
    for a in sorted(dead):
        r = info[a]
        if r['status'] not in ('missing', 'partial'):
            continue
        cs = sorted(callers[a])
        note = 'no caller in ch.exe' if not cs else 'only called from unused ' + ' / '.join('FUN_%08x' % c for c in cs)
        print(f'{r["address"]},not needed,,{note}')


main()
