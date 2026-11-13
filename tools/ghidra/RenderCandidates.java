// Targeted offline decompilation only; FDE ranges are evidence, not proven ABI.
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;

public class RenderCandidates extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) throw new IllegalArgumentException("expected sites.tsv output.txt ELF_min_load_vaddr");
        long elfBase = Long.parseUnsignedLong(args[2], 16);
        long base = currentProgram.getImageBase().getOffset();
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        DecompInterface decompiler = new DecompInterface();
        if (!decompiler.openProgram(currentProgram)) throw new IOException("decompiler failed to open program");
        int completed = 0;
        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(Paths.get(args[1]), StandardCharsets.UTF_8))) {
            out.println("Targeted Ghidra output; speculative pseudocode, NOT a verified hook or ABI.");
            out.printf("Ghidra image base: 0x%x; ELF minimum PT_LOAD vaddr: 0x%x%n", base, elfBase);
            for (String line : Files.readAllLines(Paths.get(args[0]), StandardCharsets.UTF_8)) {
                String[] fields = line.split("\\t");
                if (fields.length != 4) throw new IOException("bad sites row: " + line);
                long site = Long.parseUnsignedLong(fields[1], 16);
                long start = Long.parseUnsignedLong(fields[2], 16);
                long end = Long.parseUnsignedLong(fields[3], 16);
                out.printf("%n=== %s ELF site=0x%x FDE=0x%x..0x%x ===%n", fields[0], site, start, end);
                try {
                    if (start < elfBase || end <= start || end-start > 1024*1024) throw new IOException("bad FDE range");
                    Address a = space.getAddress(Math.addExact(base, start - elfBase));
                    Address last = space.getAddress(Math.addExact(base, end - elfBase - 1));
                    AddressSet body = new AddressSet(a, last);
                    DisassembleCommand cmd = new DisassembleCommand(a, body, true);
                    if (!cmd.applyTo(currentProgram, monitor)) throw new IOException("disassembly failed: " + cmd.getStatusMsg());
                    Function f = currentProgram.getFunctionManager().getFunctionAt(a);
                    if (f == null) f = currentProgram.getFunctionManager().createFunction(null, a, body, SourceType.USER_DEFINED);
                    DecompileResults result = decompiler.decompileFunction(f, 60, monitor);
                    if (!result.decompileCompleted() || result.getDecompiledFunction() == null)
                        throw new IOException("decompilation failed: " + result.getErrorMessage());
                    out.println("Ghidra entry=" + f.getEntryPoint());
                    out.println(result.getDecompiledFunction().getC());
                    completed++;
                } catch (Exception e) {
                    out.println("FAILED: " + e);
                }
            }
            out.println("\nCompleted: " + completed);
            if (out.checkError()) throw new IOException("failed to write report");
        } finally { decompiler.dispose(); }
        // Headless may log script errors but still return success, so the workflow also verifies this marker.
        if (completed == 0) throw new IOException("No candidate could be decompiled");
    }
}
