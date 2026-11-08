#!/usr/bin/env python3
"""Inventory of every function in ch.exe and how much of it the port covers.

Reads the Ghidra exports (the whole-exe decompile and the battle export),
the original exe (for the assert source-path strings) and the port's
sources (for the FUN_xxxxxxxx comments), and writes:

  tools/inventory/functions.csv   one row per original function
  tools/inventory/report.html     the progress report

Re-run after porting more functions:

  python3 tools/inventory/build_inventory.py

Corrections the heuristics get wrong go in tools/inventory/overrides.csv
(address,status,category,note; empty fields keep the computed value).
"""
import argparse
import csv
import datetime
import html
import json
import os
import re
import struct
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))

ap = argparse.ArgumentParser()
ap.add_argument('--full', default='/home/local-admin/Downloads/CH.exe.c')
ap.add_argument('--battle', default=os.path.join(ROOT, 'tools/decomp/ch_battle_v2.c'))
ap.add_argument('--exe', default='/home/local-admin/Documents/CrossHermit6/ch.exe')
ap.add_argument('--port', nargs='*', default=['game', 'tests', 'engine', 'main.cpp'])
ap.add_argument('--out', default=HERE)
args = ap.parse_args()

HEX8 = re.compile(r'([0-9a-f]{8})$')


# ------------------------------------------------------------ decompiles ----
def parse_full(path):
    """Ghidra C export: a signature block, a blank line, '{' ... '}'."""
    L = open(path, encoding='latin-1').read().replace('\r', '').split('\n')
    out = []
    for n, l in enumerate(L):
        if l != '{':
            continue
        j = n - 1
        while j >= 0 and L[j] == '':
            j -= 1
        k = j
        while k > 0 and L[k - 1] != '':
            k -= 1
        sig = ' '.join(L[k:j + 1])
        m = re.search(r'([A-Za-z_~][\w:~@$]*)\s*\(', sig)
        name = m.group(1) if m else '?'
        e = n
        while L[e] != '}':
            e += 1
        out.append(dict(name=name, sig=sig, body='\n'.join(L[n:e + 1]), lines=e - k + 1))
    return out


def parse_battle(path):
    """Battle export: each function starts with '// FUN_xxxxxxxx @ xxxxxxxx'."""
    txt = open(path, encoding='latin-1').read().replace('\r', '')
    parts = re.split(r'^// (\S+) @ ([0-9a-f]{8})\n', txt, flags=re.M)
    out = {}
    for i in range(1, len(parts), 3):
        body = parts[i + 2].rstrip('\n')
        out[int(parts[i + 1], 16)] = dict(name=parts[i], body=body,
                                          lines=body.count('\n') + 1)
    return out


# ------------------------------------------------------------- exe data ----
class PE:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        pe = struct.unpack_from('<I', self.d, 0x3c)[0]
        nsec = struct.unpack_from('<H', self.d, pe + 6)[0]
        optsz = struct.unpack_from('<H', self.d, pe + 20)[0]
        self.base = struct.unpack_from('<I', self.d, pe + 24 + 28)[0]
        self.secs = []
        o = pe + 24 + optsz
        for i in range(nsec):
            _, vsz, va, rsz, raw = struct.unpack_from('<8sIIII', self.d, o + 40 * i)
            self.secs.append((self.base + va, max(vsz, rsz), raw, rsz))

    def cstr(self, addr):
        for va, sz, raw, rsz in self.secs:
            if va <= addr < va + sz and addr - va < rsz:
                p = raw + addr - va
                e = self.d.find(b'\0', p, p + 400)
                return self.d[p:e].decode('cp932', 'replace')
        return None


# ------------------------------------------------------------ categories ----
CATS = [
    # (category, importance, basename patterns)
    ('Battle units/AI', 'core', ['unitctrl.cpp', 'unitctrlai.cpp', 'unitaicomctrl.cpp', 'unitanim.cpp',
                                 'findpath.cpp', 'pathfind.cpp', 'unitctrlappear.cpp', 'unitctrlstmem.cpp']),
    ('Battle skills/combat', 'core', ['unitctrlattack.cpp', 'unitctrldamage.cpp', 'unitctrlflare.cpp',
                                      'unitctrlcond.cpp', 'comenemy.cpp']),
    ('Battle map/fog', 'core', ['mapctrl.cpp']),
    ('Battle script VM', 'core', ['unitctrlscript.cpp', 'unitctrlevent*']),
    ('Battle flow/UI', 'core', ['tacticstask.cpp', 'gamectrl.cpp', 'unitctrlcursor.cpp', 'unitctrlkeyselect.cpp',
                                'unitctrlpopup.cpp', 'unitctrlwindow.cpp', 'unitctrlcommand.cpp',
                                'unitctrltalk.cpp', 'unitctrlreplay.cpp', 'mesbox.cpp', 'tactrettask.cpp']),
    ('ADV/story VM', 'core', ['ybc32.cpp', 'ybctask.cpp', 'yoshida\\ybc\\*', 'comscenario.cpp']),
    ('Game state/common', 'core', ['comround.cpp', 'comyken.cpp']),
    ('Menus/preparation', 'ui', ['preparationtask.cpp', 'workroomtask.cpp', 'weelstarttask.cpp', 'slprogtask.cpp',
                                 'kojintask.cpp', 'hantask.cpp', 'allresult*', 'tacticsresulttask.cpp',
                                 'titletask.cpp', 'titletrialtask*', 'optiontask.cpp', 'optask.cpp',
                                 'hatatask.cpp', 'demotask.cpp', 'comingtask.cpp', 'ysub.cpp',
                                 'syschatbox.cpp', 'syslistboxctrl.cpp']),
    ('Rendering/DxAnim', 'engine', ['d3dtex.cpp', 'd3dxsp.cpp', 'd3dxspfont.cpp', 'dxanim.cpp', 'dxanimdatactrl.cpp',
                                    'dxdraw.cpp', 'dxpaltbl.cpp', 'dxshow.cpp', 'dxtexpos.cpp', 'dxtextbl.cpp',
                                    'dib.cpp', 'format.cpp']),
    ('Audio/video', 'engine', ['dxaudio.cpp', 'dxplay.cpp', 'comsound.cpp']),
    ('Engine core (tasks/memory/input)', 'engine', ['system.cpp', 'sysmem.cpp', 'mtask.cpp', 'syslinklist.cpp',
                                                    'winmain.cpp', 'dxmain.cpp', 'dxinput.cpp', 'sysime.cpp']),
    ('File/VFS', 'engine', []),
    ('Network', 'optional', ['nettask.cpp', 'netmessage.cpp']),
    ('Debug/editor', 'debug', ['testtask.cpp', 'edtask.cpp']),
    ('Uncategorised game code', 'core', []),
    ('CRT/library', 'crt', []),
]
CAT_IMP = {c: i for c, i, _ in CATS}
IMP_LABEL = {'core': 'Core gameplay', 'ui': 'UI', 'engine': 'Engine/platform',
             'optional': 'Optional (network)', 'debug': 'Debug-only', 'crt': 'CRT/library (not needed)'}


def cat_of_file(path):
    p = path.lower().replace('/', '\\')
    base = p.rsplit('\\', 1)[-1]
    for cat, _, pats in CATS:
        for pat in pats:
            if pat.endswith('*'):
                if pat[:-1] in p if '\\' in pat else base.startswith(pat[:-1]):
                    return cat
            elif base == pat:
                return cat
    return None


def short_src(path):
    p = path.replace('/', '\\')
    m = re.search(r'(?i)\\src\\(.*)$', p)
    return (m.group(1) if m else p.rsplit('\\', 1)[-1]).lower()


# ----------------------------------------------------------------- port ----
NEG = re.compile(r'(?i)not (yet )?ported|unported|not implemented|isn\'t ported|aren\'t ported')
PART = re.compile(r'(?i)\bpartial(ly)?\b|\bstub\b|simplified')


def scan_port():
    mentions = defaultdict(list)   # addr -> [(file:line, text, negative)]
    for top in args.port:
        tp = os.path.join(ROOT, top)
        files = [tp] if os.path.isfile(tp) else [
            os.path.join(dp, f) for dp, _, fs in os.walk(tp) for f in fs
            if f.endswith(('.cpp', '.hpp', '.h', '.c', '.inl', '.inc'))]
        for fp in sorted(files):
            lines = open(fp, encoding='utf-8', errors='replace').read().split('\n')
            rel = os.path.relpath(fp, ROOT)
            for i, l in enumerate(lines):
                for m in re.finditer(r'FUN_([0-9a-fA-F]{8})', l):
                    # the sentence around the mention: this line and the next comment line
                    ctx = l[m.start():]
                    nxt = lines[i + 1].strip()[2:].strip() if i + 1 < len(lines) else ''
                    if lines[i + 1:i + 2] and lines[i + 1].lstrip().startswith('//') and nxt[:1].islower() \
                            and not re.search(r'[.;]\s*$', l):
                        ctx += ' ' + nxt
                    # 'not ported' said of this function itself: it follows the
                    # name in the same sentence, with no other function between.
                    tail = re.split(r'FUN_[0-9a-fA-F]{8}|[.;:]\s', ctx[12:], maxsplit=1)[0].split('//')[0]
                    hit = NEG.search(tail)
                    neg = bool(hit and hit.start() < 60 and not re.search(r'(?i)the rest|case|choice', tail[:hit.start()]))
                    mentions[int(m.group(1), 16)].append((f'{rel}:{i + 1}', l.strip()[:160], neg))
    return mentions


def load_overrides():
    p = os.path.join(HERE, 'overrides.csv')
    out = {}
    if os.path.exists(p):
        for r in csv.DictReader(l for l in open(p, encoding='utf-8') if not l.startswith('#')):
            out[int(r['address'], 16)] = r
    return out


# ----------------------------------------------------------------- main ----
def main():
    pe = PE(args.exe)
    full = parse_full(args.full)
    battle = parse_battle(args.battle)

    funcs = {}            # addr -> record
    approx_seq = []       # order of the full export, for unnamed-address placement
    last_addr = 0x400000
    for f in full:
        m = HEX8.search(f['name'])
        if m and not f['name'].startswith('thunk_'):
            a = int(m.group(1), 16)
            exact = True
        else:
            a = last_addr + 1     # named (FID/renamed) function: place just after its neighbour
            while a in funcs:
                a += 1
            exact = False
        last_addr = a
        instrumented = '__chkesp' in f['body'] or '0xcccccccc' in f['body']
        rec = funcs.get(a)
        if rec and rec['instrumented'] and not instrumented:
            continue
        funcs[a] = dict(addr=a, exact=exact, name=f['name'], lines=f['lines'], body=f['body'],
                        instrumented=instrumented, in_battle=False, src_full=True)
        approx_seq.append(a)
    for a, b in battle.items():
        if a in funcs:
            funcs[a]['in_battle'] = True
        else:
            funcs[a] = dict(addr=a, exact=True, name=b['name'], lines=b['lines'], body=b['body'],
                            instrumented='__chkesp' in b['body'] or '0xcccccccc' in b['body'],
                            in_battle=True, src_full=False)

    # names of library functions from their CRT assert file (fopen.c, ...)
    strcache = {}

    def strings_in(body):
        out = []
        for m in re.finditer(r'\bs_\w*?_([0-9a-f]{8})\b', body):
            a = int(m.group(1), 16)
            if a not in strcache:
                strcache[a] = pe.cstr(a)
            if strcache[a]:
                out.append(strcache[a])
        return out

    SRC = re.compile(r'(?i)^[a-z]:\\.*\\src\\.*\.(cpp|c)$')
    CRT_SRC = re.compile(r'(?i)^[\w]+\.c(pp)?$')
    for f in funcs.values():
        ss = strings_in(f['body'])
        srcs = [s for s in ss if SRC.match(s)]
        f['src'] = Counter(srcs).most_common(1)[0][0] if srcs else ''
        f['src_how'] = 'assert' if srcs else ''
        crt = [s for s in ss if CRT_SRC.match(s)]
        f['crt_file'] = crt[0] if crt and not f['instrumented'] else ''
        f['calls'] = set(int(x, 16) for x in re.findall(r'\bFUN_([0-9a-f]{8})\b', f['body'])) - {f['addr']}
        f['apis'] = set(re.findall(r'\b(CreateFileA|ReadFile|WriteFile|FindFirstFileA|SetFilePointer|'
                                   r'DirectSoundCreate8?|mciSendString\w*|waveOut\w+)\s*\(', f['body']))

    callers0 = defaultdict(set)
    for a, f in funcs.items():
        for c in f['calls']:
            callers0[c].add(a)

    # Fill source files of user functions without an assert from their neighbours
    # (MSVC lays out each .obj contiguously).
    # Between two different files, split the gap where the call graph says
    # the left part talks to the left file and the right part to the right.
    user = sorted(a for a, f in funcs.items() if f['instrumented'])
    anchors = [i for i, a in enumerate(user) if funcs[a]['src_how'] == 'assert']
    for i, a in enumerate(user):
        if anchors and i < anchors[0]:
            funcs[a]['src'], funcs[a]['src_how'] = funcs[user[anchors[0]]]['src'], 'nearest'
        if anchors and i > anchors[-1]:
            funcs[a]['src'], funcs[a]['src_how'] = funcs[user[anchors[-1]]]['src'], 'nearest'
    for pi, ni in zip(anchors, anchors[1:]):
        A, B = funcs[user[pi]]['src'], funcs[user[ni]]['src']
        gap = user[pi + 1:ni]
        if not gap:
            continue
        if A == B:
            for a in gap:
                funcs[a]['src'], funcs[a]['src_how'] = A, 'between'
            continue
        known_a = {x for x, f in funcs.items() if f['src_how'] in ('assert', 'between') and f['src'] == A}
        known_b = {x for x, f in funcs.items() if f['src_how'] in ('assert', 'between') and f['src'] == B}

        def aff(x):
            nb = funcs[x]['calls'] | callers0.get(x, set())
            return len(nb & known_a) - len(nb & known_b)
        scores = [aff(x) for x in gap]
        # split k: gap[:k] -> A, gap[k:] -> B; maximise sum(A side) - sum(B side), ties at the middle
        best, bestk = None, len(gap) // 2
        pref = [0]
        for s in scores:
            pref.append(pref[-1] + s)
        for k in range(len(gap) + 1):
            v = pref[k] - (pref[-1] - pref[k])
            if best is None or v > best or (v == best and abs(k - len(gap) / 2) < abs(bestk - len(gap) / 2)):
                best, bestk = v, k
        for j, x in enumerate(gap):
            funcs[x]['src'], funcs[x]['src_how'] = (A if j < bestk else B), 'nearest'

    # Library file functions (to spot the game's file layer)
    crt_file_io = {a for a, f in funcs.items()
                   if f['crt_file'].lower() in ('fopen.c', 'fclose.c', '_open.c', 'fread.c', 'fseek.c',
                                                'ftell.c', 'read.c', 'lseek.c', 'findfile.c')}
    crt_file_io |= {a for a, f in funcs.items() if not f['instrumented'] and f['calls'] & crt_file_io}

    callers = defaultdict(set)
    for a, f in funcs.items():
        for c in f['calls']:
            callers[c].add(a)

    for a, f in funcs.items():
        if not f['instrumented']:
            f['category'] = 'CRT/library'
            continue
        cat = cat_of_file(f['src']) if f['src'] else None
        uses_io = bool(f['apis'] & {'CreateFileA', 'ReadFile', 'WriteFile', 'FindFirstFileA', 'SetFilePointer'}
                       or f['calls'] & crt_file_io)
        if uses_io and cat in (None, 'Engine core (tasks/memory/input)', 'Uncategorised game code'):
            cat = 'File/VFS'
        if f['apis'] & {'DirectSoundCreate', 'DirectSoundCreate8'} and cat != 'Audio/video':
            cat = 'Audio/video'
        if f['name'] in ('Master_Debug_Menu', 'Debug_Boot_Environment_Initializer'):
            cat = 'Debug/editor'
        f['category'] = cat or 'Uncategorised game code'

    # Debug-only: everything whose callers are all debug-only code.
    debug = {a for a, f in funcs.items() if f['category'] == 'Debug/editor'}
    changed = True
    while changed:
        changed = False
        for a, f in funcs.items():
            if a in debug or not f['instrumented']:
                continue
            cs = callers.get(a)
            if cs and cs <= debug:
                debug.add(a)
                changed = True

    # ---- port coverage
    mentions = scan_port()
    overrides = load_overrides()
    notes = {}
    np_ = os.path.join(HERE, 'notes.csv')
    if os.path.exists(np_):
        for r in csv.DictReader(l for l in open(np_, encoding='utf-8') if not l.startswith('#')):
            notes[int(r['address'], 16)] = r['guess']
    for a, f in funcs.items():
        ms = mentions.get(a, [])
        f['mentions'] = ms
        if not ms:
            f['status'] = 'missing'
        elif all(neg for _, _, neg in ms):
            f['status'] = 'missing'       # only named as not ported
        elif any(neg for _, _, neg in ms):
            f['status'] = 'partial'
        else:
            f['status'] = 'ported'
        f['note'] = notes.get(a, '')
        f['importance'] = 'debug' if a in debug and f['instrumented'] else CAT_IMP[f['category']]
        o = overrides.get(a)
        if o:
            if o.get('status'):
                f['status'] = o['status']
            if o.get('category'):
                f['category'] = o['category']
                f['importance'] = CAT_IMP.get(o['category'], f['importance'])
            f['note'] = o.get('note') or f['note']
        if f['category'] == 'CRT/library' and f['status'] == 'missing':
            f['status'] = 'not needed'

    unknown = sorted(a for a in mentions if a not in funcs)

    # ---- one-line guesses for the big missing functions
    for f in funcs.values():
        f['guess'] = guess(f, funcs, strcache, callers)

    write_csv(funcs)
    write_html(funcs, unknown, mentions)
    if os.environ.get('INV_DEBUG'):
        print('functions', len(funcs), 'unknown port refs', [f'{a:08x}' for a in unknown])
        for a in sorted(funcs):
            for loc, txt, neg in funcs[a]['mentions']:
                if neg:
                    print(f'NEG {a:08x} {funcs[a]["status"]:8s} {loc}: {txt}')


def port_label(f):
    """What the port calls a ported function: the C++ name next to the
    comment, else the '// FUN_x: summary' header that introduces it."""
    for loc, txt, neg in f['mentions']:
        code = txt.split('//')[0]
        m = re.search(r'(~?\w+)\s*\([^)]*\)\s*(const)?\s*(override)?\s*[;{]?\s*$', code.strip())
        if m and m.group(1) not in ('if', 'for', 'while', 'switch', 'return', 'sizeof'):
            return m.group(1) + '()'
    for loc, txt, neg in f['mentions']:
        m = re.match(r'//\s*FUN_[0-9a-fA-F]{8}\s*[:,]\s*(.+)', txt)
        if m:
            return re.split(r'[.;(]|, ', m.group(1))[0].strip()[:40]
    return ''


def guess(f, funcs, strcache, callers):
    """A short hint of what a function does: a hand note, else what it touches."""
    if f['note']:
        return f['note']
    b = f['body']
    hints = []
    src = f['src'].rsplit('\\', 1)[-1] if f['src'] else ''
    if src:
        hints.append(src + ('' if f['src_how'] == 'assert' else '?'))
    nsw = len(re.findall(r'\bcase \w+:', b))
    if nsw >= 8:
        hints.append(f'{nsw}-case switch')
    by = [port_label(funcs[c]) for c in sorted(callers.get(f['addr'], ())) if c in funcs and funcs[c]['status'] == 'ported']
    by = [x for x in dict.fromkeys(by) if x][:2]
    if by:
        hints.append('called by ' + ' / '.join(by))
    to = [port_label(funcs[c]) for c in sorted(f['calls']) if c in funcs and funcs[c]['status'] == 'ported']
    to = [x for x in dict.fromkeys(to) if x][:2]
    if to:
        hints.append('uses ' + ' / '.join(to))
    strs = []
    for m in re.finditer(r'\bs_(\w*?)_([0-9a-f]{8})\b', b):
        s = strcache.get(int(m.group(2), 16)) or ''
        if s and not re.match(r'(?i)^[a-z]:\\', s) and len(s) > 3 and '==' not in s and '!=' not in s:
            strs.append(s)
    if strs:
        hints.append('text ' + ', '.join(repr(s[:24]) for s in list(dict.fromkeys(strs))[:2]))
    apis = re.findall(r'\b([A-Z][a-z]+[A-Z]\w+)\s*\(', b)
    apis = [x for x in dict.fromkeys(apis) if not x.startswith(('FUN', 'LAB'))][:2]
    if apis:
        hints.append('Win32 ' + ', '.join(apis))
    if len(hints) <= 1:
        nc = len(callers.get(f['addr'], ()))
        hints.append(f"{len(f['calls'])} callees, " + (f'{nc} callers' if nc else 'no direct callers (table/vtable)'))
    return '; '.join(hints)


def write_csv(funcs):
    p = os.path.join(args.out, 'functions.csv')
    with open(p, 'w', newline='', encoding='utf-8') as fh:
        w = csv.writer(fh)
        w.writerow(['address', 'name', 'lines', 'category', 'importance', 'status', 'source_file',
                    'source_how', 'in_battle_export', 'library', 'crt_file', 'port_refs', 'note'])
        for a in sorted(funcs):
            f = funcs[a]
            w.writerow([f'{a:08x}' + ('' if f['exact'] else '~'), f['name'], f['lines'], f['category'],
                        f['importance'], f['status'], short_src(f['src']) if f['src'] else '', f['src_how'],
                        int(f['in_battle']), int(not f['instrumented']), f['crt_file'],
                        ' '.join(sorted({m[0] for m in f['mentions']}))[:300], f['note']])


def write_html(funcs, unknown, mentions):
    rows = []
    for a in sorted(funcs):
        f = funcs[a]
        rows.append(dict(a=f'{a:08x}', n=f['name'], l=f['lines'], c=f['category'], i=f['importance'],
                         s=f['status'], f=short_src(f['src']) if f['src'] else '', h=f['src_how'],
                         g=f['guess'] if f['instrumented'] else (f['crt_file'] or ''), r=sorted({m[0] for m in f['mentions']})[:4]))
    meta = dict(cats=[c for c, _, _ in CATS], imp=IMP_LABEL,
                unknown=[f'{a:08x}' for a in unknown],
                total=len(funcs), port_refs=len(mentions),
                battle=sum(1 for f in funcs.values() if f['in_battle']),
                how=Counter(f['src_how'] for f in funcs.values() if f['instrumented']),
                generated=datetime.date.today().isoformat())
    tpl = open(os.path.join(HERE, 'report_template.html'), encoding='utf-8').read()
    data = json.dumps(dict(rows=rows, meta=meta), ensure_ascii=False, separators=(',', ':'))
    data = data.replace('</', '<\\/')
    out = tpl.replace('/*__DATA__*/null', data)
    with open(os.path.join(args.out, 'report.html'), 'w', encoding='utf-8') as fh:
        fh.write(out)


if __name__ == '__main__':
    main()
