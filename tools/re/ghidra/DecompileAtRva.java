// DecompileAtRva - decompile selected functions in a huge PE.
//
// Why this script exists
// ----------------------
// chrome.dll from the Reference environment kit is 276 MB. A full Ghidra
// auto-analysis of that image does not fit in the REA startup budget, so the
// "open the DLL and ask for a function" flow is unusable for it.
//
// The workaround splits the work:
//
//   1. a byte-level scanner (tools/re/mojo_xref_scan.py) locates the string
//      that names the method and finds the RIP-relative reference to it;
//      .pdata (the x64 RUNTIME_FUNCTION table) then gives the *exact*
//      function boundary without any guessing;
//   2. this script imports the PE with auto-analysis disabled
//      (analyzeHeadless -noanalysis) and disassembles / creates / decompiles
//      only the requested address ranges.
//
// Nothing here interprets the program. It reports what Ghidra produced for the
// addresses it was given.
//
// Usage (analyzeHeadless -postScript):
//     DecompileAtRva <output-file> <spec> [<spec> ...] [--depth N]
//
// Each <spec> is "begin:end" in hex RVAs relative to the PE image base,
// matching the .pdata RUNTIME_FUNCTION range. A bare "rva" uses a default
// window of 0x400 bytes.
//
// --depth N additionally decompiles the N most interesting callees of every
// requested function (callees that are not leaf helpers), which is how the
// Mojo payload path gets followed without a full analysis.
//
// The output file is plain text so it can be diffed and pasted into a report.

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.SourceType;

public class DecompileAtRva extends GhidraScript {

    private static final int DEFAULT_WINDOW = 0x400;
    private static final int MAX_DEPTH = 3;

    private DecompInterface decomp;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: DecompileAtRva <output-file> <begin:end|rva> [...] [--depth N]");
            return;
        }
        File outFile = new File(args[0]);
        List<long[]> ranges = new ArrayList<>();
        int depth = 0;
        for (int i = 1; i < args.length; i++) {
            String raw = args[i].trim();
            if (raw.equals("--depth")) {
                if (i + 1 < args.length) {
                    depth = Integer.parseInt(args[++i].trim());
                }
                continue;
            }
            ranges.add(parseSpec(raw));
        }

        Address imageBase = currentProgram.getImageBase();
        println("image base : " + imageBase);
        println("output     : " + outFile.getAbsolutePath());

        decomp = new DecompInterface();
        decomp.setSimplificationStyle("decompile");
        if (!decomp.openProgram(currentProgram)) {
            println("decompiler could not open the program: " + decomp.getLastMessage());
            return;
        }

        try (PrintWriter out = new PrintWriter(outFile, "UTF-8")) {
            out.println("# program   : " + currentProgram.getName());
            out.println("# image base: " + imageBase);
            out.println("# ranges    : " + ranges.size());
            out.println();

            Set<Long> seen = new LinkedHashSet<>();
            for (long[] range : ranges) {
                dumpRange(range, out, depth, seen, 0);
            }
        }
        decomp.dispose();
        println("wrote " + outFile.getAbsolutePath());
    }

    private long[] parseSpec(String raw) {
        String text = raw.trim();
        if (text.startsWith("0x") || text.startsWith("0X")) {
            text = text.substring(2);
        }
        int colon = text.indexOf(':');
        if (colon < 0) {
            long begin = Long.parseLong(text, 16);
            return new long[] { begin, begin + DEFAULT_WINDOW };
        }
        long begin = Long.parseLong(text.substring(0, colon), 16);
        long end = Long.parseLong(text.substring(colon + 1), 16);
        return new long[] { begin, end };
    }

    private void dumpRange(long[] range, PrintWriter out, int depth, Set<Long> seen,
            int level) {
        long beginRva = range[0];
        long endRva = range[1];
        Address begin = currentProgram.getImageBase().add(beginRva);
        Address end = currentProgram.getImageBase().add(endRva);

        out.println("================================================================");
        out.println("# RVA 0x" + Long.toHexString(beginRva) + "..0x"
                + Long.toHexString(endRva) + "  ->  " + begin + ".." + end
                + (level > 0 ? "   (depth " + level + ")" : ""));
        out.println("================================================================");

        if (!seen.add(beginRva)) {
            out.println("  (already dumped above)");
            out.println();
            return;
        }

        Function function = ensureFunction(begin, end);
        if (function == null) {
            out.println("  (no function could be created at this address)");
            out.println();
            return;
        }
        out.println("function: " + function.getName()
                + "  entry=" + function.getEntryPoint()
                + "  body=" + function.getBody().getNumAddresses() + " bytes");
        out.println();

        List<Function> callees = dumpCalls(function, out);
        out.println("---- decompiled ----");
        out.println(decompile(function));
        out.println();

        if (depth > 0) {
            long imageBaseOffset = currentProgram.getImageBase().getOffset();
            for (Function callee : callees) {
                if (callee == null || callee.getEntryPoint() == null) {
                    continue;
                }
                // getOffset() returns the absolute address; convert to RVA.
                long calleeRva = callee.getEntryPoint().getOffset() - imageBaseOffset;
                long calleeEnd =
                        callee.getBody().getMaxAddress().getOffset() - imageBaseOffset + 1;
                if (calleeRva < 0 || seen.contains(calleeRva)) {
                    continue;
                }
                dumpRange(new long[] { calleeRva, calleeEnd }, out, depth - 1, seen,
                        level + 1);
            }
        }
    }

    private Function ensureFunction(Address begin, Address end) {
        FunctionManager fm = currentProgram.getFunctionManager();
        Function existing = fm.getFunctionAt(begin);
        if (existing != null && existing.getBody().getNumAddresses() > 1) {
            return existing;
        }
        if (existing != null) {
            fm.removeFunction(existing.getEntryPoint());
        }
        // Without auto-analysis nothing is disassembled yet, so do it here.
        DisassembleCommand dis = new DisassembleCommand(begin, null, true);
        if (!dis.applyTo(currentProgram, monitor)) {
            println("disassemble failed at " + begin);
            return null;
        }
        AddressSet body = new AddressSet(begin, end.subtract(1));
        CreateFunctionCmd create = new CreateFunctionCmd(
                "reference_" + begin.toString(), begin, body, SourceType.USER_DEFINED);
        create.applyTo(currentProgram, monitor);
        return fm.getFunctionAt(begin);
    }

    private List<Function> dumpCalls(Function function, PrintWriter out) {
        List<Function> callees = new ArrayList<>();
        out.println("---- outgoing calls ----");
        InstructionIterator it =
                currentProgram.getListing().getInstructions(function.getBody(), true);
        while (it.hasNext()) {
            Instruction insn = it.next();
            if (!insn.getFlowType().isCall()) {
                continue;
            }
            Address[] flows = insn.getFlows();
            if (flows == null || flows.length == 0) {
                continue;
            }
            Function callee = resolveCallee(flows[0]);
            out.println("  " + insn.getAddress() + "  "
                    + (callee != null ? callee.getName() : flows[0].toString()));
            if (callee != null && !callees.contains(callee)) {
                callees.add(callee);
            }
        }
        out.println();
        return callees;
    }

    /**
     * Get the function at a call target, creating it when the PE has no
     * symbol for it. Without auto-analysis Ghidra knows no functions at all,
     * so every callee has to be materialised before it can be decompiled.
     */
    private Function resolveCallee(Address target) {
        FunctionManager fm = currentProgram.getFunctionManager();
        Function existing = fm.getFunctionAt(target);
        if (existing != null && existing.getBody().getNumAddresses() > 1) {
            return existing;
        }
        if (existing != null) {
            fm.removeFunction(existing.getEntryPoint());
        }
        DisassembleCommand dis = new DisassembleCommand(target, null, true);
        if (!dis.applyTo(currentProgram, monitor)) {
            return null;
        }
        AddressSet body = new AddressSet(target, target.add(DEFAULT_WINDOW - 1));
        CreateFunctionCmd create = new CreateFunctionCmd(
                "reference_" + target.toString(), target, body, SourceType.USER_DEFINED);
        create.applyTo(currentProgram, monitor);
        return fm.getFunctionAt(target);
    }

    private String decompile(Function function) {
        DecompileResults results = decomp.decompileFunction(function, 180, monitor);
        if (!results.decompileCompleted()) {
            return "/* decompilation failed: " + results.getErrorMessage() + " */";
        }
        return results.getDecompiledFunction().getC();
    }

    /** Unused helper kept out of the report; listed to document the API shape. */
    @SuppressWarnings("unused")
    private int countFunctions() {
        int n = 0;
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            it.next();
            n++;
        }
        return n;
    }
}
