// Builds the Moonstone program from build/ghidra/<name>/{layout.tsv,labels.tsv,hunk_NN.bin}.
// Args: <prepDir>.  Run via analyzeHeadless -postScript ImportMoonstone.java <prepDir>.
//@category Moonstone
import java.io.*;
import java.nio.file.*;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class ImportMoonstone extends GhidraScript {
    @Override
    protected void run() throws Exception {
        Path dir = Paths.get(getScriptArgs()[0]);
        Memory mem = currentProgram.getMemory();
        for (MemoryBlock b : mem.getBlocks()) mem.removeBlock(b, monitor);
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();

        for (String ln : Files.readAllLines(dir.resolve("layout.tsv"))) {
            if (ln.isEmpty()) continue;
            String[] f = ln.split("\t");
            Address a = sp.getAddress(Long.decode(f[2]));
            long size = Long.parseLong(f[3]);
            MemoryBlock b;
            if (f[6].equals("-")) {
                b = mem.createUninitializedBlock(f[1], a, size, false);
            } else {
                try (InputStream in = Files.newInputStream(dir.resolve(f[6]))) {
                    b = mem.createInitializedBlock(f[1], a, in, size, monitor, false);
                }
            }
            boolean code = f[4].equals("CODE");
            b.setRead(true); b.setWrite(!code); b.setExecute(code);
            b.setComment("hunk " + f[0] + " " + f[4] + (f[5].equals("1") ? " CHIP" : ""));
        }
        MemoryBlock cu = mem.createUninitializedBlock("HW_CUSTOM", sp.getAddress(0xDFF000L), 0x1000, false);
        MemoryBlock ci = mem.createUninitializedBlock("HW_CIA", sp.getAddress(0xBFD000L), 0x2000, false);
        for (MemoryBlock b : new MemoryBlock[] {cu, ci}) {
            b.setRead(true); b.setWrite(true); b.setExecute(false); b.setVolatile(true);
        }

        List<Address> funcs = new ArrayList<>(), codes = new ArrayList<>();
        int nl = 0;
        for (String ln : Files.readAllLines(dir.resolve("labels.tsv"))) {
            if (ln.isEmpty()) continue;
            String[] f = ln.split("\t");
            Address a = sp.getAddress(Long.decode(f[1]));
            createLabel(a, f[0], true, SourceType.USER_DEFINED);
            nl++;
            if (f[4].equals("1")) codes.add(a);
            if (f[5].equals("1")) funcs.add(a);
        }
        println("labels: " + nl + ", code labels: " + codes.size() + ", funcs: " + funcs.size());
        // function entries first (reliable), then remaining code labels
        for (Address a : funcs) disassemble(a);
        for (Address a : codes) if (getInstructionAt(a) == null && getDataAt(a) == null) disassemble(a);
        int nf = 0;
        for (Address a : funcs) {
            if (getFunctionAt(a) != null) continue;
            if (createFunction(a, null) != null) nf++;
        }
        println("functions created: " + nf);
        analyzeAll(currentProgram);
        println("analysis done");
    }
}
