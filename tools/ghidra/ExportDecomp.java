// Exports decompiled C to <outDir>/<LABEL>.c.  Args: <outDir> [LABEL ... | ALL]
//@category Moonstone
import java.io.*;
import java.nio.file.*;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class ExportDecomp extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Path out = Paths.get(a[0]);
        Files.createDirectories(out);
        Set<String> want = new LinkedHashSet<>(Arrays.asList(a).subList(1, a.length));
        boolean all = want.isEmpty() || want.contains("ALL");
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        List<Function> fl = new ArrayList<>();
        if (all) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) fl.add(f);
        } else {
            for (String l : want) {
                Function f = null;
                for (Symbol s : currentProgram.getSymbolTable().getSymbols(l)) {
                    f = getFunctionAt(s.getAddress());
                    if (f == null) f = createFunction(s.getAddress(), l);
                    if (f != null) break;
                }
                if (f == null) println("no function for label " + l); else fl.add(f);
            }
        }
        int n = 0;
        for (Function f : fl) {
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            String name = f.getName().replaceAll("[^A-Za-z0-9_]", "_");
            String body = r.decompileCompleted() ? r.getDecompiledFunction().getC()
                : "// decompile failed: " + r.getErrorMessage() + "\n";
            Files.writeString(out.resolve(name + ".c"),
                "// " + currentProgram.getName() + " " + f.getName() + " @ " + f.getEntryPoint() + "\n" + body);
            n++;
        }
        println("exported " + n + " functions to " + out);
    }
}
