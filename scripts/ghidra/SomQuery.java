// SomQuery.java - read-only queries against an already analysed program in a persistent Ghidra project.
// Flat2VR ShadowOfMordor analysis tooling (not mod code). Run through scripts/ghidra_query.sh, which opens the
// project with -process -noanalysis -readOnly, so nothing is ever saved back.
//
// Script args:  <outfile> <command> [args...]      one command
//               <outfile> @<batchfile>             one command per line ('#' comments), one JVM start for all
// Commands (ADDR = 0x hex address, or a symbol / function name, or Namespace::name):
//   info                          program, language, image base, memory blocks, counts
//   func ADDR                     function containing ADDR: entry, size, signature, callers, callees
//   decompile ADDR [SECS]         decompiler C for the function containing ADDR (timeout SECS, default 120)
//   disasm ADDR [N]               N instructions from ADDR (default: the whole function, max 2000)
//   xrefs ADDR [LIMIT]            references TO ADDR: from-address, type, containing function (default 200)
//   strings REGEX [LIMIT]         defined strings matching REGEX (Java regex, find()), with xref functions
//   symbols REGEX [LIMIT]         symbols whose full name (Namespace::name) matches REGEX
//   functions REGEX [LIMIT]       functions whose full name matches REGEX
//   classes REGEX [LIMIT]         class namespaces (e.g. from RTTI) matching REGEX, with their vftable labels
//   bytes ADDR [N]                hex dump of N bytes (default 64)
//@category Flat2VR

import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class SomQuery extends GhidraScript {
	private PrintWriter out;
	private DecompInterface dec;

	@Override
	public void run() throws Exception {
		String[] a = getScriptArgs();
		if (a.length < 2) {
			printerr("usage: SomQuery.java <outfile> <command> [args...] | <outfile> @<batchfile>");
			return;
		}
		List<String[]> cmds = new ArrayList<>();
		if (a[1].startsWith("@")) {
			for (String line : Files.readAllLines(Path.of(a[1].substring(1)))) {
				line = line.strip();
				if (!line.isEmpty() && !line.startsWith("#")) cmds.add(line.split("\\s+"));
			}
		}
		else {
			cmds.add(Arrays.copyOfRange(a, 1, a.length));
		}
		out = new PrintWriter(new BufferedWriter(new FileWriter(a[0])));
		try {
			out.println("# program " + currentProgram.getName() + "  (" + currentProgram.getExecutablePath() + ")");
			for (String[] c : cmds) {
				out.println("### " + String.join(" ", c));
				try {
					dispatch(c);
				}
				catch (Exception e) {
					out.println("ERROR: " + e);
				}
				out.println();
				out.flush();
			}
		}
		finally {
			if (dec != null) dec.dispose();
			out.close();
		}
		println("SomQuery wrote " + a[0]);
	}

	private void dispatch(String[] c) throws Exception {
		String op = c[0];
		String arg = c.length > 1 ? c[1] : null;
		int n = c.length > 2 ? Integer.parseInt(c[2]) : -1;
		switch (op) {
			case "info": info(); break;
			case "func": func(resolve(arg)); break;
			case "decompile": decompile(resolve(arg), n > 0 ? n : 120); break;
			case "disasm": disasm(resolve(arg), n); break;
			case "xrefs": xrefs(resolve(arg), n > 0 ? n : 200); break;
			case "strings": strings(Pattern.compile(arg), n > 0 ? n : 200); break;
			case "symbols": symbols(Pattern.compile(arg), n > 0 ? n : 200); break;
			case "functions": functions(Pattern.compile(arg), n > 0 ? n : 200); break;
			case "classes": classes(Pattern.compile(arg), n > 0 ? n : 200); break;
			case "bytes": bytes(resolve(arg), n > 0 ? n : 64); break;
			default: out.println("ERROR: unknown command " + op);
		}
	}

	// ---- helpers ----

	private Address resolve(String s) throws Exception {
		if (s == null) throw new IllegalArgumentException("missing address or name");
		if (s.matches("(0x)?[0-9a-fA-F]+")) {
			Address addr = toAddr(s.startsWith("0x") ? s.substring(2) : s);
			if (addr != null && currentProgram.getMemory().contains(addr)) return addr;
		}
		List<Symbol> syms = new ArrayList<>(NamespaceUtils.getSymbols(s, currentProgram));
		if (syms.isEmpty()) {
			SymbolIterator it = currentProgram.getSymbolTable().getSymbols(s);
			while (it.hasNext()) syms.add(it.next());
		}
		if (syms.isEmpty()) throw new IllegalArgumentException("no address or symbol '" + s + "'");
		if (syms.size() > 1) {
			out.println("note: " + syms.size() + " symbols named '" + s + "', using the first:");
			for (Symbol sym : syms) out.println("  " + sym.getAddress() + "  " + sym.getName(true));
		}
		return syms.get(0).getAddress();
	}

	private String where(Address a) {
		Function f = getFunctionContaining(a);
		if (f == null) return "-";
		long off = a.subtract(f.getEntryPoint());
		return f.getName(true) + (off == 0 ? "" : "+0x" + Long.toHexString(off));
	}

	private Function needFunction(Address a) {
		Function f = getFunctionContaining(a);
		if (f == null) throw new IllegalArgumentException("no function contains " + a);
		return f;
	}

	// ---- commands ----

	private void info() {
		Program p = currentProgram;
		out.println("name       " + p.getName());
		out.println("format     " + p.getExecutableFormat());
		out.println("language   " + p.getLanguageID() + "  compiler " + p.getCompilerSpec().getCompilerSpecID());
		out.println("image base " + p.getImageBase());
		out.println("sha256     " + p.getExecutableSHA256());
		out.println("functions  " + p.getFunctionManager().getFunctionCount());
		out.println("symbols    " + p.getSymbolTable().getNumSymbols());
		for (MemoryBlock b : p.getMemory().getBlocks()) {
			out.printf("block %-12s %s-%s %8d %s%s%s%n", b.getName(), b.getStart(), b.getEnd(), b.getSize(),
				b.isRead() ? "r" : "-", b.isWrite() ? "w" : "-", b.isExecute() ? "x" : "-");
		}
	}

	private void func(Address a) {
		Function f = needFunction(a);
		out.println("name      " + f.getName(true));
		out.println("entry     " + f.getEntryPoint() + "  body " + f.getBody().getNumAddresses() + " bytes");
		out.println("signature " + f.getPrototypeString(false, false));
		out.println("callers:");
		for (Function g : f.getCallingFunctions(monitor)) out.println("  " + g.getEntryPoint() + "  " + g.getName(true));
		out.println("callees:");
		for (Function g : f.getCalledFunctions(monitor)) out.println("  " + g.getEntryPoint() + "  " + g.getName(true));
	}

	private void decompile(Address a, int secs) {
		Function f = needFunction(a);
		if (dec == null) {
			dec = new DecompInterface();
			dec.openProgram(currentProgram);
		}
		DecompileResults r = dec.decompileFunction(f, secs, monitor);
		if (!r.decompileCompleted()) {
			out.println("ERROR: decompile failed: " + r.getErrorMessage());
			return;
		}
		out.println("// " + f.getName(true) + " @ " + f.getEntryPoint());
		out.print(r.getDecompiledFunction().getC());
	}

	private void disasm(Address a, int count) {
		Listing l = currentProgram.getListing();
		Function f = getFunctionContaining(a);
		AddressSetView limit = (count <= 0 && f != null) ? f.getBody() : null;
		if (count <= 0) count = 2000;
		InstructionIterator it = limit != null ? l.getInstructions(limit, true) : l.getInstructions(a, true);
		int i = 0;
		while (it.hasNext() && i++ < count) {
			Instruction ins = it.next();
			Symbol s = getSymbolAt(ins.getAddress());
			if (s != null) out.println(s.getName(true) + ":");
			out.printf("  %s  %s%n", ins.getAddress(), ins);
		}
	}

	private void xrefs(Address a, int limit) {
		int i = 0;
		for (Reference r : getReferencesTo(a)) {
			if (i++ >= limit) { out.println("... (limit " + limit + ")"); break; }
			out.printf("%s  %-18s %s%n", r.getFromAddress(), r.getReferenceType(), where(r.getFromAddress()));
		}
		out.println("total shown " + Math.min(i, limit));
	}

	private void strings(Pattern re, int limit) {
		int i = 0;
		for (Data d : currentProgram.getListing().getDefinedData(true)) {
			if (monitor.isCancelled()) break;
			if (!d.hasStringValue()) continue;
			Object v = d.getValue();
			if (v == null || !re.matcher(v.toString()).find()) continue;
			if (i++ >= limit) { out.println("... (limit " + limit + ")"); break; }
			Set<String> users = new TreeSet<>();
			for (Reference r : getReferencesTo(d.getAddress())) users.add(where(r.getFromAddress()));
			out.printf("%s  \"%s\"  <- %s%n", d.getAddress(), v.toString().replace("\n", "\\n"),
				users.isEmpty() ? "(no refs)" : String.join(", ", users));
		}
	}

	private void symbols(Pattern re, int limit) {
		int i = 0;
		for (Symbol s : currentProgram.getSymbolTable().getAllSymbols(true)) {
			if (monitor.isCancelled()) break;
			if (!re.matcher(s.getName(true)).find()) continue;
			if (i++ >= limit) { out.println("... (limit " + limit + ")"); break; }
			out.printf("%s  %-10s %s%n", s.getAddress(), s.getSymbolType(), s.getName(true));
		}
	}

	private void functions(Pattern re, int limit) {
		int i = 0;
		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			if (monitor.isCancelled()) break;
			if (!re.matcher(f.getName(true)).find()) continue;
			if (i++ >= limit) { out.println("... (limit " + limit + ")"); break; }
			out.printf("%s  %6d  %s%n", f.getEntryPoint(), f.getBody().getNumAddresses(), f.getName(true));
		}
	}

	private void classes(Pattern re, int limit) {
		SymbolTable st = currentProgram.getSymbolTable();
		int i = 0;
		Iterator<GhidraClass> it = st.getClassNamespaces();
		while (it.hasNext()) {
			GhidraClass c = it.next();
			if (!re.matcher(c.getName(true)).find()) continue;
			if (i++ >= limit) { out.println("... (limit " + limit + ")"); break; }
			StringBuilder vt = new StringBuilder();
			for (Symbol s : st.getSymbols(c)) {
				if (s.getName().contains("vftable") || s.getName().contains("vtable")) {
					vt.append(" ").append(s.getName()).append("@").append(s.getAddress());
				}
			}
			out.println(c.getName(true) + (vt.length() > 0 ? "  " + vt.toString().trim() : ""));
		}
	}

	private void bytes(Address a, int n) throws MemoryAccessException {
		byte[] b = new byte[n];
		int got = currentProgram.getMemory().getBytes(a, b);
		for (int i = 0; i < got; i += 16) {
			StringBuilder sb = new StringBuilder();
			for (int j = i; j < Math.min(i + 16, got); j++) sb.append(String.format("%02x ", b[j] & 0xff));
			out.println(a.add(i) + "  " + sb.toString().trim());
		}
	}
}
