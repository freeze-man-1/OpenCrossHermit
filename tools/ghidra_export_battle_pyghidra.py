# ghidra_export_battle_pyghidra.py -- Cross Hermit (TW ch.exe): re-decompile the
# battle code with its return values. PyGhidra (Python 3, Ghidra 11.3+) port of
# ghidra_export_battle.py; GhidraExportBattle.java does the same in Java.
#
# Why: the exe is a debug build. Every function ends with a call to __chkesp
# (0x0056ce80), and Ghidra assumes that call clobbers EAX, so it decides most
# small functions return void. The predicates and getters the battle is built
# from (FUN_004695d0, FUN_00430df0, FUN_004e2760 ...) lose their results.
#
# What it does (in the Ghidra database only, not the file on disk):
#   1. replaces each 5-byte "CALL __chkesp" with NOPs,
#   2. resets the non-user signatures of the functions in RANGES,
#   3. pass 1: marks the functions that read ECX as __thiscall,
#   4. pass 2: decompiles each one and stores the parameters and return type
#      it now infers, so callers pass and use them,
#   5. pass 3: decompiles again and writes the C to ~/ch_battle_fixed.c.
#
# Use: start Ghidra with pyghidraRun (support/pyghidraRun), open ch.exe in the
# CodeBrowser, Script Manager, run this script. Run it on a copy of the
# project, or close without saving, to keep the original listing.
#
# @category CrossHermit
# @runtime PyGhidra

import os

from jpype import JArray, JByte
from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.program.model.pcode import HighFunctionDBUtil
from ghidra.program.model.symbol import SourceType

OUT_PATH = os.path.join(os.path.expanduser("~"), "ch_battle_fixed.c")
CHKESP = 0x0056ce80
TIMEOUT = 120   # seconds per function
RANGES = [
    (0x0042c000, 0x00438000),   # script opcode handlers, the per-map battle checks
    (0x00438000, 0x00456800),   # map, paths, the stage script and its timers
    (0x00456800, 0x004a4000),   # units: states, AI, skills, damage, effects, sortie
    (0x004c1e00, 0x004d1800),   # YBC VM and its opcodes
    (0x004d9000, 0x004e3000),   # unit data, enemy records, scenario tables, values
]


def in_ranges(a):
    v = a.getOffset()
    return any(lo <= v < hi for lo, hi in RANGES)


# 1. NOP the __chkesp calls.
nops = JArray(JByte)([-112] * 5)   # 0x90
patched = 0
for ref in list(getReferencesTo(toAddr(CHKESP))):
    if not ref.getReferenceType().isCall():
        continue
    at = ref.getFromAddress()
    ins = getInstructionAt(at)
    if ins is None or ins.getLength() != 5:
        continue
    clearListing(at, at.add(4))
    setBytes(at, nops)
    disassemble(at)
    patched += 1
print("patched %d __chkesp calls" % patched)

# 2. Let the decompiler infer the battle functions' prototypes again.
funcs = [f for f in currentProgram.getFunctionManager().getFunctions(True)
         if in_ranges(f.getEntryPoint())]
for f in funcs:
    if f.getSignatureSource() != SourceType.USER_DEFINED:
        f.setSignatureSource(SourceType.DEFAULT)

di = DecompInterface()
di.setOptions(DecompileOptions())
di.toggleCCode(True)
di.toggleSyntaxTree(True)
di.setSimplificationStyle("decompile")
if not di.openProgram(currentProgram):
    raise RuntimeError("decompiler: %s" % di.getLastMessage())

# 3. Pass 1: functions that read ECX before writing it are MSVC __thiscall
#    (the battle's "this" is the tactics task); say so, so their stack
#    arguments are found too.
thiscalls = 0
for n, f in enumerate(funcs, 1):
    monitor.checkCancelled()
    if n % 200 == 0:
        print("pass 1: %d / %d" % (n, len(funcs)))
    if f.getSignatureSource() == SourceType.USER_DEFINED:
        continue
    res = di.decompileFunction(f, TIMEOUT, monitor)
    if res is None or not res.decompileCompleted():
        continue
    if "in_ECX" in str(res.getDecompiledFunction().getC()) and \
            f.getCallingConventionName() != "__thiscall":
        f.setCallingConvention("__thiscall")
        f.setSignatureSource(SourceType.DEFAULT)
        thiscalls += 1
print("pass 1: %d __thiscall functions" % thiscalls)

# Pass 2: store the parameters and return types the decompiler now finds, so
# the callers pass and use them.
committed = 0
for n, f in enumerate(funcs, 1):
    monitor.checkCancelled()
    if n % 200 == 0:
        print("pass 2: %d / %d" % (n, len(funcs)))
    if f.getSignatureSource() == SourceType.USER_DEFINED:
        continue
    res = di.decompileFunction(f, TIMEOUT, monitor)
    if res is None or not res.decompileCompleted() or res.getHighFunction() is None:
        continue
    try:
        HighFunctionDBUtil.commitParamsToDatabase(res.getHighFunction(), True,
            HighFunctionDBUtil.ReturnCommitOption.COMMIT_NO_VOID, SourceType.ANALYSIS)
        committed += 1
    except Exception as e:
        print("%s: %s" % (f.getName(), e))
print("pass 2: stored %d prototypes" % committed)

# 4. Pass 3: decompile again and write.
di.toggleSyntaxTree(False)
with open(OUT_PATH, "w", encoding="utf-8") as w:
    for n, f in enumerate(funcs, 1):
        monitor.checkCancelled()
        res = di.decompileFunction(f, TIMEOUT, monitor)
        w.write("// %s @ %s\n" % (f.getName(), f.getEntryPoint()))
        if res is not None and res.decompileCompleted():
            w.write(str(res.getDecompiledFunction().getC()))
        else:
            w.write("// decompile failed: %s\n" % (res.getErrorMessage() if res else "?"))
        w.write("\n")
        if n % 200 == 0:
            print("pass 3: %d / %d" % (n, len(funcs)))
di.dispose()
print("wrote %d functions to %s" % (len(funcs), OUT_PATH))
