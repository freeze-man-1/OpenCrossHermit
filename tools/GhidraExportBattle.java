// GhidraExportBattle.java -- Cross Hermit (TW ch.exe): re-decompile the battle
// code with its return values. Java port of ghidra_export_battle.py; works on
// Ghidra 10.2 and later (no Jython / PyGhidra needed).
//
// Why: the exe is a debug build. Every function ends with a call to __chkesp
// (0x0056ce80), and Ghidra assumes that call clobbers EAX, so it decides most
// small functions return void. The predicates and getters the battle is built
// from (FUN_004695d0, FUN_00430df0, FUN_004e2760 ...) lose their results.
//
// What it does (in the Ghidra database only, not the file on disk):
//   1. replaces each 5-byte "CALL __chkesp" with NOPs,
//   2. resets the non-user signatures of the functions in RANGES,
//   3. pass 1: marks the functions that read ECX as __thiscall,
//   4. pass 2: decompiles each one and stores the parameters and return type
//      it now infers, so callers pass and use them,
//   5. pass 3: decompiles again and writes the C to ~/ch_battle_fixed.c.
//
// Use: put this file in a script directory (Window > Script Manager > Manage
// Script Directories, or ~/ghidra_scripts), open ch.exe in the CodeBrowser,
// run GhidraExportBattle.java. Headless:
//   analyzeHeadless <projDir> <projName> -process ch.exe -noanalysis \
//       -scriptPath <this dir> -postScript GhidraExportBattle.java
// Run it on a copy of the project, or close without saving, to keep the
// original listing.
//
// @category CrossHermit

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.HighFunctionDBUtil;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;

public class GhidraExportBattle extends GhidraScript {

    private static final long CHKESP = 0x0056ce80L;
    private static final long[][] RANGES = {
        {0x0042c000L, 0x00438000L},   // script opcode handlers, the per-map battle checks
        {0x00438000L, 0x00456800L},   // map, paths, the stage script and its timers
        {0x00456800L, 0x004a4000L},   // units: states, AI, skills, damage, effects, sortie
        {0x004c1e00L, 0x004d1800L},   // YBC VM and its opcodes
        {0x004d9000L, 0x004e3000L},   // unit data, enemy records, scenario tables, values
    };
    private static final int TIMEOUT = 120;   // seconds per function

    private static boolean inRanges(Address a) {
        long v = a.getOffset();
        for (long[] r : RANGES) {
            if (v >= r[0] && v < r[1]) {
                return true;
            }
        }
        return false;
    }

    @Override
    protected void run() throws Exception {
        File out = new File(System.getProperty("user.home"), "ch_battle_fixed.c");

        // 1. NOP the __chkesp calls.
        byte[] nops = {(byte) 0x90, (byte) 0x90, (byte) 0x90, (byte) 0x90, (byte) 0x90};
        int patched = 0;
        for (Reference ref : getReferencesTo(toAddr(CHKESP))) {
            if (!ref.getReferenceType().isCall()) {
                continue;
            }
            Address at = ref.getFromAddress();
            Instruction ins = getInstructionAt(at);
            if (ins == null || ins.getLength() != 5) {
                continue;
            }
            clearListing(at, at.add(4));
            setBytes(at, nops);
            disassemble(at);
            patched++;
        }
        println("patched " + patched + " __chkesp calls");

        // 2. Let the decompiler infer the battle functions' prototypes again.
        List<Function> funcs = new ArrayList<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (inRanges(f.getEntryPoint())) {
                funcs.add(f);
            }
        }
        for (Function f : funcs) {
            if (f.getSignatureSource() != SourceType.USER_DEFINED) {
                f.setSignatureSource(SourceType.DEFAULT);
            }
        }

        DecompInterface di = new DecompInterface();
        di.setOptions(new DecompileOptions());
        di.toggleCCode(true);
        di.toggleSyntaxTree(true);
        di.setSimplificationStyle("decompile");
        if (!di.openProgram(currentProgram)) {
            printerr("decompiler: " + di.getLastMessage());
            return;
        }

        // 3. Pass 1: functions that read ECX before writing it are MSVC
        //    __thiscall (the battle's "this" is the tactics task); say so, so
        //    their stack arguments are found too.
        int thiscalls = 0, done = 0;
        for (Function f : funcs) {
            monitor.checkCancelled();
            if (++done % 200 == 0) {
                println("pass 1: " + done + " / " + funcs.size());
            }
            if (f.getSignatureSource() == SourceType.USER_DEFINED) {
                continue;
            }
            DecompileResults res = di.decompileFunction(f, TIMEOUT, monitor);
            if (res == null || !res.decompileCompleted()) {
                continue;
            }
            if (res.getDecompiledFunction().getC().contains("in_ECX")
                    && !"__thiscall".equals(f.getCallingConventionName())) {
                f.setCallingConvention("__thiscall");
                f.setSignatureSource(SourceType.DEFAULT);
                thiscalls++;
            }
        }
        println("pass 1: " + thiscalls + " __thiscall functions");

        // Pass 2: store the parameters and return types the decompiler now
        // finds, so the callers pass and use them.
        int committed = 0;
        done = 0;
        for (Function f : funcs) {
            monitor.checkCancelled();
            if (++done % 200 == 0) {
                println("pass 2: " + done + " / " + funcs.size());
            }
            if (f.getSignatureSource() == SourceType.USER_DEFINED) {
                continue;
            }
            DecompileResults res = di.decompileFunction(f, TIMEOUT, monitor);
            HighFunction hf = res != null && res.decompileCompleted() ? res.getHighFunction() : null;
            if (hf == null) {
                continue;
            }
            try {
                HighFunctionDBUtil.commitParamsToDatabase(hf, true,
                    HighFunctionDBUtil.ReturnCommitOption.COMMIT_NO_VOID, SourceType.ANALYSIS);
                committed++;
            } catch (Exception e) {
                printerr(f.getName() + ": " + e.getMessage());
            }
        }
        println("pass 2: stored " + committed + " prototypes");

        // 4. Pass 3: decompile again and write.
        di.toggleSyntaxTree(false);
        done = 0;
        try (BufferedWriter w = new BufferedWriter(new FileWriter(out))) {
            for (Function f : funcs) {
                monitor.checkCancelled();
                DecompileResults res = di.decompileFunction(f, TIMEOUT, monitor);
                w.write("// " + f.getName() + " @ " + f.getEntryPoint() + "\n");
                if (res != null && res.decompileCompleted()) {
                    w.write(res.getDecompiledFunction().getC());
                } else {
                    w.write("// decompile failed: " + (res != null ? res.getErrorMessage() : "?") + "\n");
                }
                w.write("\n");
                if (++done % 200 == 0) {
                    println("pass 3: " + done + " / " + funcs.size());
                }
            }
        }
        di.dispose();
        println("wrote " + done + " functions to " + out);
    }
}
