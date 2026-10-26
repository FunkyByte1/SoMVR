// SomViewScan.java - read-only instruction scans that the indexes can't answer (virtual call sites, function
// pointers stored as data). Flat2VR ShadowOfMordor analysis tooling (not mod code). Run read-only:
//   ghidra_script.sh run som-linux ro SomViewScan.java <outfile> @<batchfile>
// Batch commands (one per line, '#' comments):
//   vcall DISP[,DISP...] [LIMIT]   functions containing indirect CALLs [reg+DISP] for ALL listed DISPs (hex), with
//                                  per-DISP counts (whole program; LIMIT rows, default 300)
//   vsites ADDR                    indirect CALL sites [reg+DISP] inside the function containing ADDR, with DISP/8
//   fnptrs ADDR                    function entries referenced as data/immediates inside the function at ADDR,
//                                  each with its direct callees (one level)
//   callers ADDR [LIMIT]           direct callers of the function at ADDR, with name and size
//   vcallreg DISP [LIMIT]          functions with "MOV r,[r2+DISP]" followed within 8 instructions by CALL/JMP r
//                                  (register-indirect virtual calls that "vcall" misses), whole program
//   disp DISP[,DISP...] [LIMIT]    instructions with a memory operand [reg+DISP] (struct field use), whole program:
//                                  function, address, W (operand 0 = destination) or R, the instruction
//@category Flat2VR

import java.io.*;
import java.nio.file.*;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.OperandType;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;

public class SomViewScan extends GhidraScript {
	private PrintWriter out;

	@Override
	public void run() throws Exception {
		String[] a = getScriptArgs();
		out = new PrintWriter(new BufferedWriter(new FileWriter(a[0])));
		List<String> lines = a[1].startsWith("@") ? Files.readAllLines(Path.of(a[1].substring(1)))
				: List.of(String.join(" ", Arrays.copyOfRange(a, 1, a.length)));
		try {
			for (String line : lines) {
				line = line.strip();
				if (line.isEmpty() || line.startsWith("#")) continue;
				String[] c = line.split("\\s+");
				out.println("### " + line);
				try {
					switch (c[0]) {
						case "vcall": vcall(c[1], c.length > 2 ? Integer.parseInt(c[2]) : 300); break;
						case "vsites": vsites(addr(c[1])); break;
						case "fnptrs": fnptrs(addr(c[1])); break;
						case "callers": callers(addr(c[1]), c.length > 2 ? Integer.parseInt(c[2]) : 100); break;
						case "disp": disp(c[1], c.length > 2 ? Integer.parseInt(c[2]) : 300); break;
						case "vcallreg": vcallreg(Long.parseLong(c[1].replace("0x", ""), 16), c.length > 2 ? Integer.parseInt(c[2]) : 300); break;
						default: out.println("ERROR: unknown command " + c[0]);
					}
				}
				catch (Exception e) {
					out.println("ERROR: " + e);
				}
				out.println();
				out.flush();
			}
		}
		finally {
			out.close();
		}
	}

	private Address addr(String s) {
		return toAddr(s.startsWith("0x") ? s.substring(2) : s);
	}

	private String fname(Function f) {
		return f == null ? "-" : f.getName(true);
	}

	/** displacement of a CALL [reg+disp] instruction, or -1 */
	private long callDisp(Instruction ins) {
		if (!ins.getFlowType().isCall() || !ins.getFlowType().isComputed()) return -1;
		if (ins.getNumOperands() != 1) return -1;
		int t = ins.getOperandType(0);
		if ((t & OperandType.DYNAMIC) == 0) return -1;
		Object[] objs = ins.getOpObjects(0);
		boolean reg = false;
		long disp = 0;
		for (Object o : objs) {
			if (o instanceof ghidra.program.model.lang.Register) reg = true;
			else if (o instanceof Scalar) disp = ((Scalar) o).getSignedValue();
		}
		return reg ? disp : -1;
	}

	private void vcall(String disps, int limit) {
		String[] p = disps.split(",");
		long[] want = new long[p.length];
		for (int i = 0; i < p.length; i++) want[i] = Long.parseLong(p[i].replace("0x", ""), 16);
		Map<Function, long[]> hits = new LinkedHashMap<>();
		InstructionIterator it = currentProgram.getListing().getInstructions(true);
		FunctionManager fm = currentProgram.getFunctionManager();
		while (it.hasNext()) {
			Instruction ins = it.next();
			long d = callDisp(ins);
			if (d < 0) continue;
			for (int i = 0; i < want.length; i++) {
				if (d == want[i]) {
					Function f = fm.getFunctionContaining(ins.getAddress());
					if (f == null) continue;
					hits.computeIfAbsent(f, k -> new long[want.length])[i]++;
				}
			}
		}
		int n = 0;
		for (Map.Entry<Function, long[]> e : hits.entrySet()) {
			boolean all = true;
			for (long v : e.getValue()) all &= v > 0;
			if (!all) continue;
			if (n++ >= limit) continue;
			Function f = e.getKey();
			out.println(f.getEntryPoint() + "  " + fname(f) + "  size=" + f.getBody().getNumAddresses() + "  counts="
					+ Arrays.toString(e.getValue()));
		}
		out.println("total " + n);
	}

	private void vsites(Address a) {
		Function f = getFunctionContaining(a);
		if (f == null) { out.println("ERROR: no function"); return; }
		out.println(fname(f));
		for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
			long d = callDisp(ins);
			if (d >= 0) out.println("  " + ins.getAddress() + "  " + ins + "   slot " + (d / 8));
		}
	}

	private void fnptrs(Address a) {
		Function f = getFunctionContaining(a);
		if (f == null) { out.println("ERROR: no function"); return; }
		FunctionManager fm = currentProgram.getFunctionManager();
		out.println(fname(f));
		Set<Address> seen = new LinkedHashSet<>();
		for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
			for (int op = 0; op < ins.getNumOperands(); op++) {
				for (Object o : ins.getOpObjects(op)) {
					if (!(o instanceof Scalar)) continue;
					long v = ((Scalar) o).getUnsignedValue();
					if (v < 0x400000L || v > 0x7fffffffL) continue;
					Address t;
					try { t = toAddr(v); } catch (Exception e) { continue; }
					Function g = fm.getFunctionAt(t);
					if (g == null || !seen.add(t)) continue;
					StringBuilder sb = new StringBuilder();
					for (Function h : g.getCalledFunctions(monitor)) sb.append(' ').append(h.getEntryPoint()).append(':').append(fname(h));
					out.println("  " + ins.getAddress() + "  -> " + t + " " + fname(g) + "  calls:" + sb);
				}
			}
		}
	}

	private void callers(Address a, int limit) {
		Function f = getFunctionAt(a);
		if (f == null) f = getFunctionContaining(a);
		if (f == null) { out.println("ERROR: no function"); return; }
		out.println(fname(f));
		int n = 0;
		for (Function g : f.getCallingFunctions(monitor)) {
			if (n++ >= limit) break;
			out.println("  " + g.getEntryPoint() + "  " + fname(g) + "  size=" + g.getBody().getNumAddresses());
		}
	}

	private void vcallreg(long want, int limit) {
		FunctionManager fm = currentProgram.getFunctionManager();
		Map<Function, Integer> hits = new LinkedHashMap<>();
		InstructionIterator it = currentProgram.getListing().getInstructions(true);
		String pending = null;
		int left = 0;
		while (it.hasNext()) {
			Instruction ins = it.next();
			String m = ins.getMnemonicString();
			if (pending != null && left-- > 0) {
				if ((m.equals("CALL") || m.equals("JMP")) && ins.getNumOperands() == 1
						&& ins.getDefaultOperandRepresentation(0).equals(pending)) {
					Function f = fm.getFunctionContaining(ins.getAddress());
					if (f != null) hits.merge(f, 1, Integer::sum);
					pending = null;
				}
			}
			else pending = null;
			if (m.equals("MOV") && ins.getNumOperands() == 2 && ins.getOperandType(0) == OperandType.REGISTER
					&& (ins.getOperandType(1) & OperandType.DYNAMIC) != 0) {
				long disp = -1;
				boolean reg = false;
				for (Object o : ins.getOpObjects(1)) {
					if (o instanceof ghidra.program.model.lang.Register) reg = true;
					else if (o instanceof Scalar) disp = ((Scalar) o).getSignedValue();
				}
				if (reg && disp == want && ins.getDefaultOperandRepresentation(1).startsWith("qword ptr")) {
					pending = ins.getDefaultOperandRepresentation(0);
					left = 8;
				}
			}
		}
		int n = 0;
		for (Map.Entry<Function, Integer> e : hits.entrySet()) {
			if (n++ >= limit) continue;
			Function f = e.getKey();
			out.println(f.getEntryPoint() + "  " + fname(f) + "  size=" + f.getBody().getNumAddresses() + "  count=" + e.getValue());
		}
		out.println("total " + n);
	}

	private void disp(String disps, int limit) {
		Set<Long> want = new HashSet<>();
		for (String p : disps.split(",")) want.add(Long.parseLong(p.replace("0x", ""), 16));
		InstructionIterator it = currentProgram.getListing().getInstructions(true);
		FunctionManager fm = currentProgram.getFunctionManager();
		int n = 0;
		while (it.hasNext()) {
			Instruction ins = it.next();
			for (int op = 0; op < ins.getNumOperands(); op++) {
				if ((ins.getOperandType(op) & OperandType.DYNAMIC) == 0) continue;
				boolean reg = false;
				long d = -1;
				for (Object o : ins.getOpObjects(op)) {
					if (o instanceof ghidra.program.model.lang.Register) reg = true;
					else if (o instanceof Scalar) d = ((Scalar) o).getSignedValue();
				}
				if (!reg || !want.contains(d)) continue;
				if (n++ < limit) {
					Function f = fm.getFunctionContaining(ins.getAddress());
					out.println(ins.getAddress() + "  " + fname(f) + "  " + (op == 0 ? "W" : "R") + "  " + ins);
				}
			}
		}
		out.println("total " + n);
	}
}
