#!/usr/bin/env bash
# tests/sanitize.sh (= build.sh --sanitize) - every test of build.sh --check (the unit tests, selftest.c, vklayer/build.sh
# --check's layer tests and the layer itself) built with AddressSanitizer (+ LeakSanitizer) and UndefinedBehaviorSanitizer
# and run (Linux; docs/re-xr-pipeline.md 151). The normal build is untouched: build.sh --check runs in a mirror of the tree
# under build/san/ (real directories, symlinked files) with a `gcc` first on PATH that compiles each source with the same
# gcc and flags as build.sh's plus -fsanitize=address,undefined,float-cast-overflow (gcc's `undefined` leaves the float to
# int overflow out; any report stops the test: -fno-sanitize-recover) and links
# with clang (the runtime: this system's gcc ships none; the two share its ABI). somvr.so alone stays the plain build (the
# inert check preloads it into /bin/true); the layer and the test objects are instrumented and loaded by the instrumented
# test programs. Outputs stay in build/san/; nothing here launches the game or a runtime.
#   tests/sanitize.sh            build and run all (a sanitizer report fails the run: exit 86 ASan/LSan, 87 UBSan)
#   SAN_KEEP=1 tests/sanitize.sh keep build/san/ afterwards (the sanitized test programs, to rerun one by hand)
set -euo pipefail
D=$(cd "$(dirname "$0")/.." && pwd)            # mod/somvr
S=$(cd "$D/../.." && pwd)                      # ShadowOfMordor
command -v clang >/dev/null || { echo "sanitize: no clang (its ASan/UBSan runtime links the programs)"; exit 2; }
M=$D/build/san
rm -rf -- "$M"
T=$M/tree
mkdir -p "$T/mod/somvr" "$M/bin" "$M/obj"
# the mirror: ShadowOfMordor/{third_party,reference,scripts} and mod/somvr's files as symlinks; mod/somvr's directories (and
# their directories once more) as real directories of symlinks, so every build/ the scripts write is the mirror's own
for x in third_party reference scripts; do [ -e "$S/$x" ] && ln -s "$S/$x" "$T/$x"; done
mirror() { # mirror SRC DST DEPTH
  local e n
  for e in "$1"/* "$1"/.[!.]*; do
    [ -e "$e" ] || continue
    n=$(basename "$e")
    [ "$n" = build ] && continue
    if [ -d "$e" ] && [ "$3" -gt 0 ]; then mkdir -p "$2/$n"; mirror "$e" "$2/$n" $(($3 - 1)); else ln -s "$e" "$2/$n"; fi
  done
}
mirror "$D" "$T/mod/somvr" 2
# selftest.c (-no-pie, as the game) is linked at 0x20000000: sanitized it is ~10x bigger and at 0x400000 its own globals
# (their redzones) would cover the game binary's addresses (0x400000-0x5000000) that the mod reads at sites with no
# stand-in, where the plain selftest has nothing mapped or its own code: the same "not matched" either way.
# selftest.c's stand-ins (the game's functions, every one __attribute__((noipa))) stay uninstrumented: the hooks are checked
# against their machine code (a call within the first bytes, a prologue) and the sanitizers' code would move it; the mod's
# code (somvr.c, included) and the test's own are instrumented
rm -f -- "$T/mod/somvr/selftest.c"
sed 's/__attribute__((noipa/__attribute__((no_sanitize("address", "undefined"), noipa/' "$D/selftest.c" > "$T/mod/somvr/selftest.c"
G=$(command -v gcc)
cat > "$M/bin/gcc" <<EOF
#!/usr/bin/env bash
# the mirror's gcc (tests/sanitize.sh)
set -euo pipefail
G=$G
OBJ=$M/obj
EOF
cat >> "$M/bin/gcc" <<'EOF'
SAN=(-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer -Wno-error)
args=("$@")
out=a.out; srcs=(); cf=(); lf=(); objs=(); shared=0
while [ $# -gt 0 ]; do
  case $1 in
    -c|-S|-E|-fsyntax-only) exec "$G" "${args[@]}" "${SAN[@]}" ;;
    -o) out=$2; shift ;;
    *.c) srcs+=("$1") ;;
    *.o|*.a|*.so) objs+=("$1") ;;
    -shared) shared=1; lf+=(-shared) ;;
    -Wl,-z,defs) ;; # (a library's sanitizer calls are the loading program's runtime's)
    -l*|-L*|-Wl,*|-rdynamic|-pthread) lf+=("$1") ;;
    -no-pie) lf+=(-no-pie -Wl,-Ttext-segment=0x20000000) ;;
    *) cf+=("$1") ;;
  esac
  shift
done
[ $shared = 1 ] && [ "$(basename "$out")" = somvr.so ] && exec "$G" "${args[@]}"
O=$OBJ/$(basename "$out")
mkdir -p "$O"
i=0
for s in "${srcs[@]}"; do
  i=$((i + 1))
  "$G" "${cf[@]}" "${SAN[@]}" -c -o "$O/$i.o" "$s"
  objs+=("$O/$i.o")
done
exec clang -fsanitize=address,undefined,float-cast-overflow -o "$out" "${objs[@]}" "${lf[@]}"
EOF
chmod +x "$M/bin/gcc"
# a report fails the test (and so build.sh): halt_on_error + exitcode; leaks too (detect_leaks; tests/lsan.supp: one line
# each, why)
export ASAN_OPTIONS=${ASAN_OPTIONS:-halt_on_error=1:exitcode=86:detect_leaks=1:strict_string_checks=1:detect_stack_use_after_return=1:check_initialization_order=1}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1:exitcode=87}
export LSAN_OPTIONS=${LSAN_OPTIONS:-suppressions=$D/tests/lsan.supp:print_suppressions=0}
rc=0
# every gcc line run as written (buildplan.py: no object cache, no split into -c and a link): the wrapper above sees the
# line the way it was written for (somvr.so plain, everything else instrumented); the lines still run in parallel
PATH="$M/bin:$PATH" SOMVR_PLAN_DIRECT=1 "$T/mod/somvr/build.sh" --check || rc=$?
[ "${SAN_KEEP:-0}" = 1 ] || rm -rf -- "$M"
if [ $rc = 0 ]; then echo "sanitize: all of build.sh --check's tests clean under ASan + LSan + UBSan (gcc $(gcc -dumpversion), clang $(clang -dumpversion)'s runtime)"
else echo "sanitize: FAILED (exit $rc; 86 = an ASan/LSan report, 87 = a UBSan report)"; fi
exit $rc
