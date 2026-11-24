#!/bin/sh
# Run a main board image on cdj-nxs2-qemu's emulated CDJ-2000.  SCIF0's
# output goes to stdout, SCIF1's to build/scif1.log.  Ctrl-C (or SECONDS=n)
# ends it.  QEMU: CDJ_QEMU, else cdj-nxs2-qemu's build beside this repo's
# parent (setup.sh --build-dir), else ~/sandbox/cdj-nxs2-build.
img=${1:-build/main.bin}
qemu=${CDJ_QEMU:-$HOME/sandbox/cdj-nxs2-build/qemu-build/qemu-system-sh4}
[ -x "$qemu" ] || { echo "no qemu-system-sh4 with the cdj2000 machine (set CDJ_QEMU)" >&2; exit 1; }
mkdir -p build
set -- "$qemu" -M cdj2000 -kernel "$img" -display none -monitor none \
    -serial stdio -serial file:build/scif1.log
if [ -n "$SECONDS_LIMIT" ]; then exec timeout "$SECONDS_LIMIT" "$@"; else exec "$@"; fi
