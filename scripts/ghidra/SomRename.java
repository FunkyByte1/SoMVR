// SomRename.java - rename functions that analysis identified, from a TSV list. Flat2VR ShadowOfMordor analysis
// tooling (not mod code). Run through scripts/ghidra_script.sh (rw to save, ro for a dry run that changes nothing).
//
// Script args:  <list.tsv> <report.txt>
// list.tsv: one function per line, tab-separated: ADDR <tab> NEWNAME [<tab> comment ignored]; '#' lines are comments.
// Rules: the address must be a function entry; only functions whose current name is Ghidra's default (FUN_...)
// are renamed (source USER_DEFINED, global namespace); a function that already has NEWNAME is "same"; any other
// name is kept and reported ("kept"). The report lists every line's outcome.
//@category Flat2VR

import java.io.*;
import java.nio.file.*;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class SomRename extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] a = getScriptArgs();
		if (a.length < 2) {
			printerr("usage: SomRename.java <list.tsv> <report.txt>");
			return;
		}
		int renamed = 0, same = 0, kept = 0, bad = 0;
		try (PrintWriter out = new PrintWriter(new BufferedWriter(new FileWriter(a[1])))) {
			for (String line : Files.readAllLines(Path.of(a[0]))) {
				if (line.isBlank() || line.startsWith("#")) continue;
				String[] f = line.split("\t");
				if (f.length < 2) { out.println("bad line: " + line); bad++; continue; }
				String s = f[0].strip(), name = f[1].strip();
				Address addr = toAddr(s.startsWith("0x") ? s.substring(2) : s);
				Function fn = addr == null ? null : getFunctionAt(addr);
				if (fn == null) { out.println("no function at " + s + " (" + name + ")"); bad++; continue; }
				String old = fn.getName(true);
				if (fn.getName().equals(name)) { out.println("same    " + s + "  " + name); same++; continue; }
				if (!fn.getName().startsWith("FUN_")) { out.println("kept    " + s + "  " + old + "  (wanted " + name + ")"); kept++; continue; }
				fn.setName(name, SourceType.USER_DEFINED);
				out.println("renamed " + s + "  " + old + " -> " + fn.getName(true));
				renamed++;
			}
			out.println("total renamed=" + renamed + " same=" + same + " kept=" + kept + " bad=" + bad);
		}
		println("SomRename: renamed=" + renamed + " same=" + same + " kept=" + kept + " bad=" + bad + " -> " + a[1]);
	}
}
