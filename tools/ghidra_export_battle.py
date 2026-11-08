# ghidra_export_battle.py -- Cross Hermit (TW ch.exe): re-decompile the battle code
# with its return values.
#
# Why: the exe is a debug build. Every function ends with a call to __chkesp
# (0x0056ce80), and Ghidra assumes that call clobbers EAX, so it decides most
# small functions return void ("local_8 = 0x4...; return;"). The predicates and
# getters the battle is built from (FUN_004695d0, FUN_00430df0, FUN_004e2760 ...)
# then lose their results in the decompile.
#
# What it does: replaces each 5-byte "CALL __chkesp" with NOPs (in the Ghidra
# database only, not the file on disk), resets the signatures Ghidra inferred
# for the functions in the ranges below so the decompiler infers them again,
# and writes their decompiled C to OUT.
#
# Use: open ch.exe in the CodeBrowser, Window > Script Manager, add this file's
# folder to the script directories, run it. It takes a while (some thousand
# functions). Run it on a copy of the project, or close without saving
# afterwards, if you want the original listing back.
#
# @category CrossHermit
# @runtime Jython

import jarray
from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.program.model.symbol import SourceType
from ghidra.util.task import ConsoleTaskMonitor
from java.io import FileWriter, BufferedWriter
import os

OUT_PATH = os.path.join(os.path.expanduser("~"), "ch_battle_fixed.c")

CHKESP = 0x0056ce80
RANGES = [
    (0x0042c000, 0x00438000),   # script opcode handlers, the per-map battle checks
    (0x00438000, 0x00456800),   # map, paths, the stage script and its timers
    (0x00456800, 0x004a4000),   # units: states, AI, skills, damage, effects, sortie
    (0x004c1e00, 0x004d1800),   # YBC VM and its opcodes
    (0x004d9000, 0x004e3000),   # unit data, enemy records, scenario tables, values
]

monitor = ConsoleTaskMonitor()
listing = currentProgram.getListing()
memory = currentProgram.getMemory()
fm = currentProgram.getFunctionManager()

# 1. NOP the __chkesp calls.
nops = jarray.array([-112] * 5, 'b')   # 0x90
patched = 0
for ref in getReferencesTo(toAddr(CHKESP)):
    if not ref.getReferenceType().isCall():
        continue
    at = ref.getFromAddress()
    ins = listing.getInstructionAt(at)
    if ins is None or ins.getLength() != 5:
        continue
    clearListing(at, at.add(4))
    memory.setBytes(at, nops)
    disassemble(at)
    patched += 1
print("patched %d __chkesp calls" % patched)

def in_ranges(a):
    v = a.getOffset()
    for lo, hi in RANGES:
        if lo <= v < hi:
            return True
    return False

# 2. Let the decompiler infer the battle functions' prototypes again.
funcs = [f for f in fm.getFunctions(True) if in_ranges(f.getEntryPoint())]
for f in funcs:
    if f.getSignatureSource() != SourceType.USER_DEFINED:
        f.setSignatureSource(SourceType.DEFAULT)

# 3. Decompile and write.
di = DecompInterface()
opts = DecompileOptions()
di.setOptions(opts)
di.toggleCCode(True)
di.toggleSyntaxTree(False)
di.openProgram(currentProgram)
w = BufferedWriter(FileWriter(OUT_PATH))
done = 0
for f in funcs:
    res = di.decompileFunction(f, 120, monitor)
    w.write("// %s @ %s\n" % (f.getName(), f.getEntryPoint()))
    if res is not None and res.decompileCompleted():
        w.write(res.getDecompiledFunction().getC())
    else:
        w.write("// decompile failed: %s\n" % (res.getErrorMessage() if res else "?"))
    w.write("\n")
    done += 1
    if done % 200 == 0:
        print("%d / %d" % (done, len(funcs)))
w.close()
print("wrote %d functions to %s" % (done, OUT_PATH))
