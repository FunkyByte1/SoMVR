#!/usr/bin/env python3
"""minidump_info.py DUMP [N]  - what a Breakpad minidump (the Feral crash handler's *.dmp) says, without symbols:
the exception (signal, fault address), the crashing thread's RIP/RSP mapped to module+offset, and a heuristic stack
scan: the first N (default 25) stack words that point into an executable mapping (candidate return addresses).
For the non-PIE game binary (base 0x400000) the printed address is the Ghidra address (som-linux project).
"""
import struct
import sys

SIGNALS = {4: "SIGILL", 6: "SIGABRT", 7: "SIGBUS", 8: "SIGFPE", 11: "SIGSEGV", 5: "SIGTRAP"}


def main(path, n=25):
    d = open(path, "rb").read()
    if d[:4] != b"MDMP":
        sys.exit("not a minidump")
    nstreams, dir_rva = struct.unpack_from("<II", d, 8)
    streams = {}
    for i in range(nstreams):
        t, size, rva = struct.unpack_from("<III", d, dir_rva + 12 * i)
        streams[t] = (size, rva)

    def mdstring(rva):
        ln = struct.unpack_from("<I", d, rva)[0]
        return d[rva + 4: rva + 4 + ln].decode("utf-16-le", "replace")

    # memory map: LinuxMaps (0x47670009) when present, else the module list
    maps = []
    if 0x47670009 in streams:
        size, rva = streams[0x47670009]
        for line in d[rva: rva + size].decode("latin-1").splitlines():
            f = line.split()
            if len(f) >= 5 and "-" in f[0]:
                lo, hi = (int(x, 16) for x in f[0].split("-"))
                maps.append((lo, hi, f[1], int(f[2], 16), f[5] if len(f) > 5 else "[anon]"))
    mods = []
    if 4 in streams:
        size, rva = streams[4]
        cnt = struct.unpack_from("<I", d, rva)[0]
        for i in range(cnt):
            base, sz, _ck, _ts, name_rva = struct.unpack_from("<QIIII", d, rva + 4 + 108 * i)
            mods.append((base, base + sz, mdstring(name_rva)))

    def where(a):
        for lo, hi, perm, off, name in maps:
            if lo <= a < hi:
                return "%s+0x%x (%s)" % (name.rsplit("/", 1)[-1], a - lo + off, perm), "x" in perm
        for lo, hi, name in mods:
            if lo <= a < hi:
                return "%s+0x%x" % (name.rsplit("/", 1)[-1], a - lo), True
        return "?", False

    if 6 not in streams:
        sys.exit("no exception stream")
    size, rva = streams[6]
    tid, _al, code, flags, _rec, addr = struct.unpack_from("<IIIIQQ", d, rva)
    ctx_size, ctx_rva = struct.unpack_from("<II", d, rva + 8 + 8 + 8 + 8 + 4 + 4 + 15 * 8)
    sig = code
    print("exception: signal %d (%s), code/flags 0x%x, fault address 0x%x, thread %d" % (sig, SIGNALS.get(sig, "?"), flags, addr, tid))
    rsp, rip = struct.unpack_from("<Q", d, ctx_rva + 0x98)[0], struct.unpack_from("<Q", d, ctx_rva + 0xF8)[0]
    print("RIP 0x%x  %s" % (rip, where(rip)[0]))
    print("RSP 0x%x" % rsp)
    regs = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
    vals = struct.unpack_from("<16Q", d, ctx_rva + 0x78)
    print("regs " + " ".join("%s=%x" % (r, v) for r, v in zip(regs, vals)))
    # the crashing thread's stack memory
    if 3 in streams:
        size, rva = streams[3]
        cnt = struct.unpack_from("<I", d, rva)[0]
        for i in range(cnt):
            t_id, _s, _pc, _p, _teb, st_start, st_size, st_rva, _cs, _cr = struct.unpack_from("<IIIIQQIIII", d, rva + 4 + 48 * i)
            if t_id != tid:
                continue
            mem = d[st_rva: st_rva + st_size]
            print("stack scan (thread %d, %d bytes from 0x%x):" % (tid, st_size, st_start))
            shown = 0
            for off in range(max(0, rsp - st_start), len(mem) - 7, 8):
                v = struct.unpack_from("<Q", mem, off)[0]
                w, ex = where(v)
                if ex:
                    print("  [rsp+0x%04x] 0x%x  %s" % (st_start + off - rsp, v, w))
                    shown += 1
                    if shown >= n:
                        break
    if 0x47670004 in streams:   # LinuxProcStatus: thread name is per-thread, the process name here
        size, rva = streams[0x47670004]
        name = [l for l in d[rva: rva + size].decode("latin-1").splitlines() if l.startswith("Name:")]
        print("process " + (name[0] if name else "?"))


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 25)
