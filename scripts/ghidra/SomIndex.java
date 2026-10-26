// SomIndex.java - read-only export of grep-friendly offline indexes (TSV, header line, one record per line).
// Flat2VR ShadowOfMordor analysis tooling (not mod code). Run it read-only with scripts/ghidra_script.sh:
//   ghidra_script.sh run som-linux ro SomIndex.java /abs/outdir [nocalls]
// Writes into outdir:
//   functions.tsv      addr, name (Namespace::name), size (bytes in the body), callers (distinct calling
//                      functions), callees (distinct called functions)
//   calls.tsv          caller, callee: one line per distinct (calling function, called function) pair, from the
//                      program's call references (direct calls only: virtual calls through a vptr are not here)
//   strings_xrefs.tsv  addr, nrefs, text (<= 200 chars, \t \n \r \\ escaped), refs = "0xENTRY:name" of the
//                      referencing functions (max 20, then "+N more"; a reference from outside any function is
//                      "0xFROM:-"). Only strings Ghidra defined as data.
// "nocalls" skips calls.tsv (the counts in functions.tsv are still computed).
//@category Flat2VR

import java.io.*;
import java.nio.file.*;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class SomIndex extends GhidraScript {

	private static String hex(Address a) { return "0x" + Long.toHexString(a.getOffset()); }
	private static String tsv(String s) {
		return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r");
	}

	@Override
	public void run() throws Exception {
		String[] a = getScriptArgs();
		if (a.length < 1) { printerr("usage: SomIndex.java <outdir> [nocalls]"); return; }
		Path outdir = Path.of(a[0]);
		boolean calls = !(a.length > 1 && a[1].equals("nocalls"));
		Files.createDirectories(outdir);
		long t0 = System.currentTimeMillis();
		FunctionManager fm = currentProgram.getFunctionManager();
		ReferenceManager rm = currentProgram.getReferenceManager();

		// calls: for every function, the distinct functions with a call reference to its entry
		Map<Long, Integer> callers = new HashMap<>(), callees = new HashMap<>();
		long edges = 0, loose = 0;
		PrintWriter cw = calls ? new PrintWriter(new BufferedWriter(new FileWriter(outdir.resolve("calls.tsv").toFile()), 1 << 20)) : null;
		if (cw != null) cw.println("caller\tcallee");
		for (Function f : fm.getFunctions(true)) {
			if (monitor.isCancelled()) return;
			Set<Long> seen = new HashSet<>();
			for (Reference r : rm.getReferencesTo(f.getEntryPoint())) {
				if (!r.getReferenceType().isCall()) continue;
				Function c = fm.getFunctionContaining(r.getFromAddress());
				if (c == null) { loose++; continue; }
				long ce = c.getEntryPoint().getOffset();
				if (!seen.add(ce)) continue;
				callees.merge(ce, 1, Integer::sum);
				if (cw != null) cw.println("0x" + Long.toHexString(ce) + "\t" + hex(f.getEntryPoint()));
				edges++;
			}
			if (!seen.isEmpty()) callers.put(f.getEntryPoint().getOffset(), seen.size());
		}
		if (cw != null) cw.close();
		println("SomIndex: calls " + edges + " distinct caller->callee pairs (" + loose + " call refs from outside functions)" +
			(calls ? "" : ", calls.tsv skipped"));

		int n = 0;
		try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(outdir.resolve("functions.tsv").toFile()), 1 << 20))) {
			w.println("addr\tname\tsize\tcallers\tcallees");
			for (Function f : fm.getFunctions(true)) {
				long e = f.getEntryPoint().getOffset();
				w.println(hex(f.getEntryPoint()) + "\t" + tsv(f.getName(true)) + "\t" + f.getBody().getNumAddresses() + "\t" +
					callers.getOrDefault(e, 0) + "\t" + callees.getOrDefault(e, 0));
				n++;
			}
		}
		println("SomIndex: functions.tsv " + n + " rows");

		n = 0;
		int noref = 0;
		try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(outdir.resolve("strings_xrefs.tsv").toFile()), 1 << 20))) {
			w.println("addr\tnrefs\ttext\trefs");
			for (Data d : currentProgram.getListing().getDefinedData(true)) {
				if (monitor.isCancelled()) return;
				if (!d.hasStringValue()) continue;
				Object v = d.getValue();
				if (v == null) continue;
				String s = v.toString();
				if (s.length() > 200) s = s.substring(0, 200);
				LinkedHashMap<String, Boolean> fs = new LinkedHashMap<>();
				for (Reference r : rm.getReferencesTo(d.getAddress())) {
					Function c = fm.getFunctionContaining(r.getFromAddress());
					fs.put(c == null ? hex(r.getFromAddress()) + ":-" : hex(c.getEntryPoint()) + ":" + c.getName(true), true);
				}
				List<String> l = new ArrayList<>(fs.keySet());
				String refs = String.join(",", l.subList(0, Math.min(20, l.size()))) + (l.size() > 20 ? ",+" + (l.size() - 20) + " more" : "");
				if (l.isEmpty()) noref++;
				w.println(hex(d.getAddress()) + "\t" + l.size() + "\t" + tsv(s) + "\t" + tsv(refs));
				n++;
			}
		}
		println("SomIndex: strings_xrefs.tsv " + n + " rows (" + noref + " without references)");
		println("SomIndex: done in " + (System.currentTimeMillis() - t0) / 1000.0 + " s");
	}
}
