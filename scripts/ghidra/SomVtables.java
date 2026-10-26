// SomVtables.java - Itanium C++ ABI (GCC) RTTI pass: finds every typeinfo object and vtable in the program,
// demangles the class names, labels them and writes classes.tsv + vfuncs.tsv.
// Flat2VR ShadowOfMordor analysis tooling (not mod code). Built for som-linux (the Feral ELF, non-PIE, stripped,
// libstdc++ linked in statically, so no RTTI relocations: every pointer is a plain absolute qword).
//
// Script args:  <outdir> [dry]
//   outdir   where classes.tsv and vfuncs.tsv go (created if missing)
//   dry      change nothing (read-only use, e.g. to re-export the TSVs with the current names)
// Run it with scripts/ghidra_script.sh (rw = labels and saves; ro + dry = export only).
//
// What it labels (never renames anything that already has a non-default name):
//   Class::typeinfo        the typeinfo object (__class_type_info / __si_class_type_info / __vmi_class_type_info)
//   Class::typeinfo_name   its mangled name string
//   Class::vtable          a primary vtable, AT ITS ADDRESS POINT (the value an object's vptr holds = _ZTV + 16)
//   Class::vtable_0x10     a secondary vtable (multiple inheritance) for the subobject at offset 0x10
//   Class::vf_NN           the function in slot NN of the primary vtable, only where it still has a FUN_ name
//   Class::vf_0x10_NN      slot NN of the secondary vtable for subobject 0x10 (usually a this-adjusting thunk)
//   __cxa_pure_virtual / __cxa_deleted_virtual   found by their message strings
// Classes are processed bases first (depth in the inheritance graph), primary vtables before secondary ones, so a
// function inherited unchanged gets the name of the class that introduced it. A function in several vtables keeps
// the first name; vfuncs.tsv lists every slot. Slot targets with no function get one (disassembled, no analysis).
// The vtable scan: after the address point, qwords that are 0 or point to a function entry (or to an instruction
// that starts no function), until the next vtable's offset-to-top/typeinfo pair, a typeinfo object, a pointer into
// the middle of a function (a switch table follows), or any other value; trailing zero slots are dropped
// (they can't be told from alignment padding). Code often READs single slots directly (devirtualised calls), so a
// referenced slot address is no sign of a table end.
//@category Flat2VR

import java.io.*;
import java.nio.*;
import java.nio.file.*;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.app.util.SymbolPathParser;
import ghidra.app.util.NamespaceUtils;
import ghidra.app.util.demangler.gnu.GnuDemanglerNativeProcess;
import ghidra.app.util.demangler.gnu.GnuDemanglerOptions;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class SomVtables extends GhidraScript {

	// ---- memory snapshot of the data blocks ----
	private static final class Blk {
		String name; long start, end; ByteBuffer bb;
	}
	private final List<Blk> data = new ArrayList<>();
	private final List<long[]> exec = new ArrayList<>();
	private Blk last;

	private Blk blk(long va) {
		if (last != null && va >= last.start && va < last.end) return last;
		for (Blk b : data) if (va >= b.start && va < b.end) return last = b;
		return null;
	}
	private boolean mapped(long va) { Blk b = blk(va); return b != null && va + 8 <= b.end; }
	private long q(long va) { Blk b = blk(va); return b.bb.getLong((int) (va - b.start)); }
	private boolean isExec(long v) {
		for (long[] r : exec) if (v >= r[0] && v < r[1]) return true;
		return false;
	}
	private String cstr(long va, int max) {
		Blk b = blk(va);
		if (b == null) return null;
		StringBuilder sb = new StringBuilder();
		for (long p = va; p < b.end && sb.length() < max; p++) {
			int c = b.bb.get((int) (p - b.start)) & 0xff;
			if (c == 0) return sb.toString();
			if (c < 0x20 || c > 0x7e) return null;
			sb.append((char) c);
		}
		return null;
	}

	// ---- model ----
	private static final class TI {
		long addr, nameAddr; String kind, mangled, demangled, ghidraPath; int len;
		List<String> gparts = new ArrayList<>();
		List<long[]> bases = new ArrayList<>();   // {base TI, offset_flags}
		List<VT> vts = new ArrayList<>();
		int depth = -1; Namespace ns;
	}
	private static final class VT {
		TI ti; long ap; long ott; long[] slots;
	}
	private final Map<Long, String> kindByVptr = new HashMap<>();
	private final Map<Long, TI> tis = new LinkedHashMap<>();
	private final Set<Long> inTI = new HashSet<>();
	private final List<VT> vts = new ArrayList<>();

	private boolean dry;
	private final Map<String, Integer> stat = new TreeMap<>();
	private void count(String k) { stat.merge(k, 1, Integer::sum); }
	private void count(String k, int n) { stat.merge(k, n, Integer::sum); }

	private Address A(long v) { return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v); }
	private static String hex(long v) { return "0x" + Long.toHexString(v); }

	@Override
	public void run() throws Exception {
		String[] a = getScriptArgs();
		if (a.length < 1) { printerr("usage: SomVtables.java <outdir> [dry]"); return; }
		Path outdir = Path.of(a[0]);
		dry = a.length > 1 && a[1].equals("dry");
		Files.createDirectories(outdir);
		long t0 = System.currentTimeMillis();

		loadMemory();
		findAbiKinds();
		findTypeinfos();
		findVtables();
		demangle();
		long t1 = System.currentTimeMillis();
		println("SomVtables: model built in " + (t1 - t0) / 1000.0 + " s");

		List<VT> order = new ArrayList<>(vts);
		for (TI t : tis.values()) depth(t, 0);
		order.sort(Comparator.<VT>comparingInt(v -> v.ti.depth).thenComparingInt(v -> v.ott == 0 ? 0 : 1)
			.thenComparingLong(v -> v.ap));
		slotStats();
		if (!dry) {
			label(order);
			println("SomVtables: labelled in " + (System.currentTimeMillis() - t1) / 1000.0 + " s");
		}
		export(outdir, order);
		for (Map.Entry<String, Integer> e : stat.entrySet()) println("SomVtables stat " + e.getKey() + " = " + e.getValue());
		println("SomVtables: done in " + (System.currentTimeMillis() - t0) / 1000.0 + " s" + (dry ? " (dry: nothing changed)" : ""));
	}

	private void loadMemory() throws Exception {
		Set<String> want = Set.of(".rodata", ".data.rel.ro", ".data");
		for (MemoryBlock mb : currentProgram.getMemory().getBlocks()) {
			if (mb.isExecute()) {
				exec.add(new long[] { mb.getStart().getOffset(), mb.getEnd().getOffset() + 1 });
				continue;
			}
			if (!mb.isInitialized() || !want.contains(mb.getName())) continue;
			Blk b = new Blk();
			b.name = mb.getName(); b.start = mb.getStart().getOffset(); b.end = mb.getEnd().getOffset() + 1;
			byte[] buf = new byte[(int) mb.getSize()];
			mb.getBytes(mb.getStart(), buf);
			b.bb = ByteBuffer.wrap(buf).order(ByteOrder.LITTLE_ENDIAN);
			data.add(b);
			println("SomVtables: data block " + b.name + " " + hex(b.start) + " " + buf.length + " bytes");
		}
		if (data.isEmpty()) throw new IllegalStateException("no .rodata/.data.rel.ro/.data blocks");
	}

	// The __cxxabiv1 typeinfo classes: their name strings -> their typeinfo objects -> their vtables' address points.
	private void findAbiKinds() {
		Map<Long, String> abiTI = new HashMap<>();
		for (Blk b : data) {
			for (long p = (b.start + 7) & ~7L; p + 16 <= b.end; p += 8) {
				long v = q(p);
				if (blk(v) == null) continue;
				String s = cstr(v, 80);
				if (s != null && s.startsWith("N10__cxxabiv1") && s.endsWith("_type_infoE")) {
					String k = s.replaceAll("^N10__cxxabiv1\\d+__", "").replaceAll("_type_infoE$", "");
					if (k.equals("si_class")) k = "si";
					else if (k.equals("vmi_class")) k = "vmi";
					abiTI.put(p - 8, k);
				}
			}
		}
		for (Blk b : data) {
			for (long p = (b.start + 15) & ~7L; p + 8 <= b.end; p += 8) {
				String k = abiTI.get(q(p));
				if (k != null && q(p - 8) == 0 && !kindByVptr.containsValue(k)) kindByVptr.put(p + 8, k);
			}
		}
		println("SomVtables: abi typeinfo kinds " + kindByVptr);
		if (!kindByVptr.containsValue("class") || !kindByVptr.containsValue("si") || !kindByVptr.containsValue("vmi"))
			throw new IllegalStateException("missing __class/__si_class/__vmi_class_type_info vtables");
	}

	private void findTypeinfos() {
		for (Blk b : data) {
			for (long p = (b.start + 7) & ~7L; p + 16 <= b.end; p += 8) {
				String k = kindByVptr.get(q(p));
				if (k == null) continue;
				long np = q(p + 8);
				String nm = blk(np) == null ? null : cstr(np, 4096);
				if (nm == null || nm.isEmpty()) { count("ti rejected (no name)"); continue; }
				TI t = new TI();
				t.addr = p; t.nameAddr = np; t.kind = k; t.mangled = nm;
				switch (k) {
					case "class": t.len = 16; break;
					case "si": t.len = 24; t.bases.add(new long[] { q(p + 16), 2 }); break;
					case "vmi": {
						long w = q(p + 16);
						int n = (int) (w >>> 32);
						if (n < 0 || n > 256 || !mapped(p + 24 + 16L * n)) { count("vmi bad base count"); n = 0; }
						t.len = 24 + 16 * n;
						for (int i = 0; i < n; i++) t.bases.add(new long[] { q(p + 24 + 16L * i), q(p + 32 + 16L * i) });
						break;
					}
					case "pointer": case "pbase": t.len = 32; break;
					case "pointer_to_member": t.len = 40; break;
					default: t.len = 16;
				}
				for (int o = 0; o < t.len; o += 8) inTI.add(p + o);
				tis.put(p, t);
				count("ti kind " + k);
			}
		}
		// keep only class kinds in the model; the others only mark ranges as "inside a typeinfo"
		tis.values().removeIf(t -> !(t.kind.equals("class") || t.kind.equals("si") || t.kind.equals("vmi")));
		for (TI t : tis.values()) for (long[] bse : t.bases) if (!tis.containsKey(bse[0])) count("base typeinfo not found");
		println("SomVtables: class typeinfos " + tis.size());
	}

	private boolean plausibleOtt(long v) { return v <= 0 && v > -(1L << 24) && (v & 7) == 0; }

	private void findVtables() {
		Map<Long, List<Long>> refs = new HashMap<>();
		Set<Long> tiRef = new HashSet<>();
		for (Blk b : data) {
			for (long p = (b.start + 7) & ~7L; p + 8 <= b.end; p += 8) {
				long v = q(p);
				if (tis.containsKey(v) && !inTI.contains(p)) {
					refs.computeIfAbsent(v, x -> new ArrayList<>()).add(p);
					tiRef.add(p);
				}
			}
		}
		FunctionManager fm = currentProgram.getFunctionManager();
		Listing lst = currentProgram.getListing();
		for (Map.Entry<Long, List<Long>> e : refs.entrySet()) {
			TI t = tis.get(e.getKey());
			for (long r : e.getValue()) {
				if (!mapped(r - 8)) continue;
				long ott = q(r - 8);
				if (!plausibleOtt(ott)) { count("ti ref without offset-to-top"); continue; }
				long ap = r + 8;
				List<Long> sl = new ArrayList<>();
				String why = "end of block";
				while (true) {
					long sa = ap + 8L * sl.size();
					if (!mapped(sa)) break;
					long v = q(sa);
					if (inTI.contains(sa)) { why = "typeinfo"; break; }
					if (tiRef.contains(sa + 8) && plausibleOtt(v)) { why = "next vtable"; break; }
					if (v == 0) { sl.add(v); continue; }
					if (isExec(v)) {
						// switch tables (absolute case-label addresses in .rodata) often follow a vtable: a target
						// inside a function, or with no instruction, ends the table
						Address va = A(v);
						if (fm.getFunctionAt(va) == null) {
							if (fm.getFunctionContaining(va) != null) { why = "target inside a function"; break; }
							if (lst.getInstructionAt(va) == null) { why = "target not code"; break; }
						}
						sl.add(v);
						continue;
					}
					why = "other value";
					break;
				}
				count("vtable end: " + why);
				while (!sl.isEmpty() && sl.get(sl.size() - 1) == 0) sl.remove(sl.size() - 1);
				VT vt = new VT();
				vt.ti = t; vt.ap = ap; vt.ott = ott;
				vt.slots = sl.stream().mapToLong(Long::longValue).toArray();
				t.vts.add(vt);
				vts.add(vt);
				count(ott == 0 ? "vtables primary" : "vtables secondary");
				if (vt.slots.length == 0) count("vtables with no slots");
				stat.merge("vtable slots", vt.slots.length, Integer::sum);
			}
		}
		for (TI t : tis.values()) {
			t.vts.sort(Comparator.<VT>comparingInt(v -> v.ott == 0 ? 0 : 1).thenComparingLong(v -> -v.ott).thenComparingLong(v -> v.ap));
			if (t.vts.isEmpty()) count("classes without vtable");
			else count("classes with vtable");
			if (t.vts.stream().filter(v -> v.ott == 0).count() > 1) count("classes with >1 primary vtable");
			if (t.vts.stream().anyMatch(v -> v.ott != 0)) count("classes with secondary vtables");
		}
	}

	private void demangle() throws Exception {
		GnuDemanglerOptions o = new GnuDemanglerOptions();
		GnuDemanglerNativeProcess proc =
			GnuDemanglerNativeProcess.getDemanglerNativeProcess(o.getDemanglerName(), o.getDemanglerApplicationArguments());
		Map<String, Integer> seen = new HashMap<>();
		for (TI t : tis.values()) {
			String m = t.mangled.startsWith("*") ? t.mangled.substring(1) : t.mangled;
			String d = null;
			try {
				d = proc.demangle("_ZTI" + m);
			}
			catch (IOException ex) {
				count("demangle io error");
				proc = GnuDemanglerNativeProcess.getDemanglerNativeProcess(o.getDemanglerName(), o.getDemanglerApplicationArguments());
			}
			if (d != null && d.startsWith("typeinfo for ")) t.demangled = d.substring(13);
			else { t.demangled = m; count("demangle failed"); }
			// Ghidra namespace path: template-aware split, invalid chars replaced, overlong parts shortened
			List<String> parts;
			try { parts = SymbolPathParser.parse(t.demangled); }
			catch (Exception ex) { parts = List.of(t.demangled); count("namespace parse failed"); }
			for (String part : parts) {
				String s = SymbolUtilities.replaceInvalidChars(part, true);
				if (s.length() > 1000) s = s.substring(0, 960) + "..#" + Integer.toHexString(part.hashCode());
				t.gparts.add(s);
			}
			t.ghidraPath = String.join("::", t.gparts);
			if (seen.merge(t.ghidraPath, 1, Integer::sum) == 2) count("class names used by >1 typeinfo");
		}
	}

	private int depth(TI t, int guard) {
		if (t.depth >= 0) return t.depth;
		int d = 0;
		if (guard < 64) {
			for (long[] b : t.bases) {
				TI bt = tis.get(b[0]);
				if (bt != null) d = Math.max(d, depth(bt, guard + 1) + 1);
			}
		}
		return t.depth = d;
	}

	private void slotStats() {
		FunctionManager fm = currentProgram.getFunctionManager();
		Listing l = currentProgram.getListing();
		Set<Long> seen = new HashSet<>();
		for (VT v : vts) {
			for (long f : v.slots) {
				if (f == 0 || !seen.add(f)) continue;
				Address fa = A(f);
				Function fn = fm.getFunctionAt(fa);
				if (fn != null) {
					count("slot targets: function entry");
					if (fn.getSymbol().getSource() != SourceType.DEFAULT) count("slot targets: function already named");
				}
				else if (fm.getFunctionContaining(fa) != null) count("slot targets: inside another function");
				else if (l.getInstructionAt(fa) != null) count("slot targets: instruction, no function");
				else count("slot targets: no instruction");
			}
		}
		stat.put("slot targets: distinct", seen.size());
	}

	private final Map<Long, Set<Long>> anc = new HashMap<>();
	private Set<Long> ancestors(TI t) {   // t and all its bases (typeinfo addresses)
		Set<Long> s = anc.get(t.addr);
		if (s != null) return s;
		s = new HashSet<>();
		s.add(t.addr);
		anc.put(t.addr, s);   // cycle guard
		for (long[] b : t.bases) {
			TI bt = tis.get(b[0]);
			if (bt != null) s.addAll(ancestors(bt));
		}
		return s;
	}

	// ---- labelling ----

	private Namespace classNs(TI t) throws Exception {
		if (t.ns != null) return t.ns;
		SymbolTable st = currentProgram.getSymbolTable();
		Namespace parent = currentProgram.getGlobalNamespace();
		String[] parts = t.gparts.toArray(new String[0]);
		for (int i = 0; i < parts.length; i++) {
			Namespace ns = st.getNamespace(parts[i], parent);
			if (ns == null) {
				ns = (i == parts.length - 1) ? st.createClass(parent, parts[i], SourceType.ANALYSIS)
					: st.createNameSpace(parent, parts[i], SourceType.ANALYSIS);
				count(i == parts.length - 1 ? "ghidra classes created" : "ghidra namespaces created");
			}
			else if (i == parts.length - 1 && !(ns instanceof GhidraClass) && !(ns instanceof Function)) {
				ns = NamespaceUtils.convertNamespaceToClass(ns);
				count("ghidra namespaces converted to class");
			}
			parent = ns;
		}
		return t.ns = parent;
	}

	private void addLabel(long va, String name, Namespace ns) throws Exception {
		SymbolTable st = currentProgram.getSymbolTable();
		Address ad = A(va);
		for (Symbol s : st.getSymbols(ad)) {
			if (s.getName().equals(name) && s.getParentNamespace().equals(ns)) return;
		}
		Symbol s = st.createLabel(ad, name, ns, SourceType.ANALYSIS);
		Symbol prim = st.getPrimarySymbol(ad);
		if (prim == null || prim.getSource() == SourceType.DEFAULT) s.setPrimary();
		count("labels " + name.replaceAll("_0x[0-9a-f]+$", "_0xNN"));
	}

	private void nameLibFunc(String message, String name) throws Exception {
		for (Data d : currentProgram.getListing().getDefinedData(true)) {
			if (monitor.isCancelled()) return;
			if (!d.hasStringValue() || d.getValue() == null || !d.getValue().toString().startsWith(message)) continue;
			for (Reference r : getReferencesTo(d.getAddress())) {
				Function f = getFunctionContaining(r.getFromAddress());
				if (f != null && f.getSymbol().getSource() == SourceType.DEFAULT) {
					f.setName(name, SourceType.ANALYSIS);
					println("SomVtables: " + name + " = " + f.getEntryPoint());
					count("named " + name);
				}
			}
			return;
		}
	}

	private void label(List<VT> order) throws Exception {
		nameLibFunc("pure virtual method called", "__cxa_pure_virtual");
		nameLibFunc("deleted virtual method called", "__cxa_deleted_virtual");
		Listing l = currentProgram.getListing();
		FunctionManager fm = currentProgram.getFunctionManager();
		int n = 0;
		for (TI t : tis.values()) {
			if (monitor.isCancelled()) return;
			Namespace ns;
			try { ns = classNs(t); }
			catch (Exception ex) {
				count("class namespace failed");
				printerr("SomVtables: namespace for " + t.ghidraPath + ": " + ex);
				t.ns = ns = currentProgram.getGlobalNamespace();
			}
			addLabel(t.addr, "typeinfo", ns);
			addLabel(t.nameAddr, "typeinfo_name", ns);
			if (!t.bases.isEmpty() && l.getComment(CommentType.EOL, A(t.addr)) == null) {
				l.setComment(A(t.addr), CommentType.EOL, "typeinfo for " + t.demangled + "; bases: " + basesString(t));
			}
		}
		Set<Long> renamed = new HashSet<>();
		for (VT v : order) {
			if (monitor.isCancelled()) return;
			Namespace ns = v.ti.ns;
			addLabel(v.ap, v.ott == 0 ? "vtable" : "vtable_" + hex(-v.ott), ns);
			for (int i = 0; i < v.slots.length; i++) {
				long f = v.slots[i];
				if (f == 0) continue;
				Address fa = A(f);
				Function fn = fm.getFunctionAt(fa);
				if (fn == null) {
					if (l.getInstructionAt(fa) == null) {
						if (l.getUndefinedDataAt(fa) == null && l.getDefinedDataAt(fa) != null) { count("slot target is data"); continue; }
						disassemble(fa);
					}
					fn = createFunction(fa, null);
					count(fn == null ? "slot function create failed" : "slot functions created");
					if (fn == null) continue;
				}
				if (fn.isThunk() || fn.isExternal()) continue;
				if (fn.getSymbol().getSource() != SourceType.DEFAULT || renamed.contains(f)) continue;
				String nm = String.format(v.ott == 0 ? "vf_%02d" : "vf_" + hex(-v.ott) + "_%02d", i);
				try { fn.getSymbol().setNameAndNamespace(nm, ns, SourceType.ANALYSIS); }
				catch (Exception ex) { count("vfunc rename failed"); printerr("SomVtables: rename " + fa + ": " + ex); continue; }
				renamed.add(f);
				count(v.ott == 0 ? "vfuncs named (primary)" : "vfuncs named (secondary)");
			}
			if (++n % 1000 == 0) println("SomVtables: labelled " + n + " / " + order.size() + " vtables");
		}
	}

	// ---- export ----

	private String basesString(TI t) {
		List<String> out = new ArrayList<>();
		for (long[] b : t.bases) {
			TI bt = tis.get(b[0]);
			String nm = bt != null ? bt.demangled : "?" + hex(b[0]);
			if (t.kind.equals("si")) { out.add(nm); continue; }
			long of = b[1];
			boolean virt = (of & 1) != 0, pub = (of & 2) != 0;
			long off = of >> 8;
			out.add((virt ? "virtual " : "") + (pub ? "" : "nonpublic ") + nm + (virt ? "@vbo" + (off < 0 ? "-" + hex(-off) : hex(off)) : off != 0 ? "@" + hex(off) : ""));
		}
		return String.join(";", out);
	}

	private static String tsv(String s) { return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r"); }

	private void export(Path outdir, List<VT> order) throws Exception {
		FunctionManager fm = currentProgram.getFunctionManager();
		Map<Long, Integer> uses = new HashMap<>();
		Map<Long, Set<TI>> users = new HashMap<>();
		for (VT v : vts) {
			for (long f : v.slots) {
				if (f == 0) continue;
				uses.merge(f, 1, Integer::sum);
				users.computeIfAbsent(f, x -> new HashSet<>()).add(v.ti);
			}
		}
		// a function in the vtables of several classes has an owner when one of them is a base of all the others
		Set<Long> noOwner = new HashSet<>();
		for (Map.Entry<Long, Set<TI>> e : users.entrySet()) {
			Set<TI> cs = e.getValue();
			if (cs.size() < 2) continue;
			boolean ok = false;
			for (TI c : cs) {
				boolean all = true;
				for (TI o : cs) if (!ancestors(o).contains(c.addr)) { all = false; break; }
				if (all) { ok = true; break; }
			}
			if (!ok) noOwner.add(e.getKey());
		}
		stat.put("functions in >1 class's vtables", (int) users.values().stream().filter(x -> x.size() > 1).count());
		stat.put("functions in >1 class's vtables, no common owner", noOwner.size());
		int rows = 0;
		try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(outdir.resolve("classes.tsv").toFile())))) {
			w.println("class\ttypeinfo\tkind\tbases\tvtables\tvfuncs\tmangled\tghidra_ns");
			for (TI t : tis.values()) {
				List<String> vl = new ArrayList<>();
				int nvf = -1;
				for (VT v : t.vts) {
					vl.add(hex(v.ap) + (v.ott == 0 ? "" : "@" + hex(-v.ott)));
					if (v.ott == 0 && nvf < 0) nvf = v.slots.length;
				}
				w.println(tsv(t.demangled) + "\t" + hex(t.addr) + "\t" + t.kind + "\t" + tsv(basesString(t)) + "\t" +
					String.join(";", vl) + "\t" + (nvf < 0 ? "-" : String.valueOf(nvf)) + "\t" + tsv(t.mangled) + "\t" + tsv(t.ghidraPath));
				rows++;
			}
		}
		println("SomVtables: classes.tsv " + rows + " rows");
		rows = 0;
		try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(outdir.resolve("vfuncs.tsv").toFile())))) {
			w.println("class\tvtable\tsubobject\tslot\tfunction\tname\tnote");
			for (VT v : order) {
				for (int i = 0; i < v.slots.length; i++) {
					long f = v.slots[i];
					String name, note;
					if (f == 0) { name = "-"; note = "null"; }
					else {
						Function fn = fm.getFunctionAt(A(f));
						name = fn == null ? "-" : fn.getName(true);
						int u = uses.getOrDefault(f, 0);
						note = fn == null ? "no function" : fn.isThunk() ? "thunk" : "";
						if (u > 1) note = (note.isEmpty() ? "" : note + ";") + "in " + u + " slots";
						if (noOwner.contains(f)) note += ";no common owner";
					}
					w.println(tsv(v.ti.demangled) + "\t" + hex(v.ap) + "\t" + hex(-v.ott) + "\t" + i + "\t" + hex(f) + "\t" + tsv(name) + "\t" + note);
					rows++;
				}
			}
		}
		println("SomVtables: vfuncs.tsv " + rows + " rows");
	}
}
