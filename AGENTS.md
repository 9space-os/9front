# AGENTS.md

## What this is
The **9front** OS source tree (Plan 9 fork). Branch `front` = mirror of github.com/9front/9front. Branch `gcc` = port that builds 9front with **gcc** on Linux instead of 9front's own compilers (8c/6c/7c + mk). Docs: man pages `sys/man`, papers `sys/doc`. Status and open items of the port: `sys/src/build/host/MEMORY.md` (German).

## Layout
- `386/ amd64/ arm64/`: per arch `include/u.h`, `include/ureg.h`; built files land in `<arch>/bin`, `<arch>/lib.gcc`.
- `sys/src/`: `lib*` libraries, `cmd` + `games` programs, `9` kernel (`9/port` common, `9/pc` 386, `9/pc64` amd64, `9/arm64` qemu), `ape` POSIX layer, `boot` loaders.
- `sys/include/`: shared headers. `rc/`, `lib/`, `acme/`: scripts and data.
- `sys/src/build/`: what a build needs: the root manifests (`build.json`, `build.$ARCH.json`), `step/` build steps (`step/rc/` their rc versions for 9front), `gen/` manifest generators, `lib/`. Its `host/` is the Linux host's only (not on the CD): `guest/` QEMU/CD/boot loaders, `check/` + `fix/` porting tools, `debug/`, `tests/` test suites, `sys/src/build/host/patch-upstream/`, `MEMORY.md` (see `sys/src/build/README.md`). The tree's root holds only 9front's directories.

## Build (gcc port)
- Build tool (Go) once: `cd sys/src/cmd/build && GO111MODULE=off go build -o build.exe .`
- Everything for one arch, from the tree root: `CC=gcc ARCH=amd64 sys/src/cmd/build/build.exe` (or `ARCH=386`, `ARCH=arm64`). Ends with `NNN files expected, ok`.
- One manifest: `CC=gcc ARCH=amd64 sys/src/cmd/build/build.exe <path/to/x.json>`.
- Only what is out of date (like mk): add `-D`. After switching ARCH it rebuilds everything (objects of another arch are out of date).
- Never run two builds in the same tree at once (they share object files).
- After kernel changes rebuild with the full command, not the kernel manifest alone: its generated files (`sys/src/9/port/initcode.i`, ...) are shared by the arches and made by the full build just before the kernel. arm64: the guest boots `arm64/9qemu.u`, which only `sys/src/build/host/guest/build-boot.sh` remakes.
- Manifests (`*.json`) are generated from 9front's mkfiles and checked in. Never hand-edit; regenerate: `sys/src/build/gen/genlibc.py genlibs.py gencmds.py genkernel.py genbuild.py` (other arch: `objtype=386 python3 sys/src/build/gen/...`).
- Boot loaders: `sys/src/build/host/guest/build-boot.sh` (in a QEMU guest). Self-hosting (the guest builds the tree with the native gcc): `sys/src/build/host/guest/build-self.sh`. Install CD: `objtype=amd64 sys/src/build/host/guest/mkiso.sh`.
- Tests: `bash sys/src/build/host/tests/static/run.sh` (no build needed), `bash sys/src/build/host/tests/run.sh [static asm syscall build guest boot gui disk iso rio usb]` (see `sys/src/build/host/tests/README.md`).

## Plan 9 C
- `#include <u.h>` then `<libc.h>`; `nil` not NULL; errors via `werrstr`/`%r`, `sysfatal`.
- Old style: tabs, K&R where the file has it, `/* */` comments only, no C99 features, no designated initializers. Match the surrounding file.

## Porting rules
- Change as little as possible against `front`; never change behaviour, only what gcc needs.
- Mark every change with a `/* GCC port: why */` comment.
- Prefer a cast or a type change over rewriting code. Do not reformat.
- Generated files: change the generator, not the output (e.g. `sd53c8xx.c` from `sd53c8xx.n`).
- Assembly: keep 9front's `.s`, add a GAS `.S` port beside it (System V ABI).
- New helper scripts go in the matching `sys/src/build/` subdirectory (`host/` if only the Linux host runs them); list them in `sys/src/build/README.md`.
- Log fixes and findings in `sys/src/build/host/MEMORY.md`.

## gcc vs 9front's compilers: known traps
- `long` is 32 bit in 9front on every arch, 64 bit in gcc on amd64/arm64. `ulong` is typedef'd to 32 bit; plain `long` is not. For data on disk/wire/hardware use `s32int`.
- `#pragma pack on` is ignored by gcc: add `#pragma pack(push,1)` / `pack(pop)` under `#ifdef __GNUC__`.
- Hardware registers: gcc may read/write them as single bytes; use `volatile` pointers.
- Busy-wait loops on memory need `volatile` (gcc hoists the load).
- Embedded structs (`Lock;`) need `-fplan9-extensions`; keep member paths exactly as in 9front (`&mc->Mouse`, not `&mc`).
- `uchar`/`ushort` promote to unsigned in 9front, to `int` in gcc.
- Checks for these: `sys/src/build/host/tests/static/run.sh`, `sys/src/build/host/check/cmp-struct-layout.sh`.
