# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository. `AGENTS.md` (included below) is the short version, kept compact for small local models; the details are here.

@AGENTS.md

## Branches
- `front`: mirror of upstream `github.com/9front/9front`, branch `front`. Treat as pristine 9front source; update it by fast-forward to `origin/front`, then rebase `gcc` onto it.
- `upstream`: fixes for 9front itself found on `gcc` (not about gcc), one commit each on `origin/front`, exported to `sys/src/build/host/patch-upstream/` (README.md there). A new one goes on `gcc` and on `upstream`.
- `gcc` (work branch): the gcc port. Keep diffs against `front` minimal and reviewable. After a rebase, regenerate the manifests (upstream mkfile changes add sources) and run `sys/src/build/host/tests/static`.

## gcc port status: MEMORY.md
`sys/src/build/host/MEMORY.md` (German) is the checked-in log of the port: fixes per part ("Teil N"), open items, and porting notes. Check it before starting gcc-port work, update it when you land or find something.

## Build tool and manifests
- Go sources in `sys/src/cmd/build/` (`build.go`, `codegen.go`, `genheaders.go`, `paths.go`, `doc.go`); `build.exe` is built there and not checked in. Format of the manifests: `doc.go`.
- From `ARCH` the tool sets, unless already in the environment: `objtype`, `O` (8/6/7), `SUFFIX` (none for amd64, `.386`, `.arm64`: names of the per-arch manifests), `CONF` (pc, pc64, qemu), `TOOLPREFIX` (arm64: `aarch64-linux-musl-`), `CPUS`. `PLAN9` defaults to the git checkout (on Plan 9 without one: `/`). Without an argument it builds `build.json` of the current directory, else the tree's.
- `sys/src/build/build.json` (what the tool builds without an argument outside a manifest's directory) → `sys/src/build/build.$ARCH.json` (from `sys/src/build/gen/genbuild.py`), in order: `prep` (`sys/src/build/step/prep-objtype.sh`: install dirs, `libgcc.a`; 386: ../9front-gcc's i686-pc-plan9 libgcc, else the host's -m32 one with SSE2/cmov and a warning), libc, the libraries (`genlibs.py --list`), every `sys/src/lib*/klib.json` (kernel archives `klib*.a`), the programs (`gencmds.py --list`), `kernelgen` (`sys/src/build/step/gen-*.sh`: rebootcode.i, apbootstrap.i, etherm10g/sd53c8xx .i, initcode, `/boot/boot` + `$CONF.bootfs.paq`), the kernel, `install` (`elf2aout.py`: the a.out where 9boot loads it; `check-install.py -r`). `Pre`/`Post` steps run without a shell (split on spaces), hence `bash /sys/src/build/step/...` (an absolute word is the tree's: the tool puts `$PLAN9` before it). Do not build a kernel manifest alone after building another arch: `kernelgen` writes arch-specific files into shared places (`sys/src/9/port/initcode.i`); an amd64 kernel with 386's initcode panics at boot ("boot process died: ... pc=0x200028"). `-clean` runs only the `Projects` (`sys/src/build/step/clean.sh`). `-D` builds only what is out of date, like mk: per object gcc's `x.d` (`-MMD`, headers) plus a signature of the compile command; kernel libraries name their objects `x.k.o` (`Objtag`, klib.json), as they compile the userspace libraries' sources in the same directories; generated files that come out the same keep their time (`Clean` lists, `writefile`; the generator scripts replace their output only when it changes). A no-op `-D` run takes seconds and ends with the same files as a full build. Each manifest's source files compile in parallel, `CPUS` at a time (one gcc run per file); manifests run one after another.
- Kernel manifests: `sys/src/9/pc64/build.json` (hand-made), `pc/build.json` and `arm64/build.json` (`sys/src/build/gen/genkernel.py`). `Config` holds the CONF sections, each element one CONF line; the tool generates `<conf>.c` (mkdevc + mkrootc), `errstr.h`, `../port/systab.h`. The `link` section calls only the first name's link function (as mkdevc).
- Library manifests: `sys/src/lib<x>/lib<x>[.arch].json` (userspace) and `klib.json` + `<arch>.json` (kernel); 9front's `reduce` (port file dropped when the machine dir has one) is replicated.
- Generators read the mkfiles: `genlibc.py`, `genlibs.py`, `gencmds.py` (`--probe` rebuilds `sys/src/build/gen/cmds-broken*.txt`), `genkernel.py`, `genbuild.py`; per arch with `objtype=386|arm64`. `sys/src/build/host/tests/static` fails if a checked-in manifest differs from its generator.
- 9front's boot loaders are not built by gcc: `sys/src/build/host/guest/build-boot.sh` boots the built system and runs 9front's own mkfiles with 8c/6c/7c there.

## Guest, CD, tests
- `sys/src/build/host/guest/boot-cli.sh` boots the built kernel in QEMU (serial console, Harvey's ufs as root); `sys/src/build/host/guest/mkiso.sh` / `sys/src/build/host/guest/mkdisk.sh` make the install CD / disk image in the guest, as 9front's `sys/lib/dist/mkfile` does. Work dir `$CLI_DIR` (`~/.cache/9front-cli`).
- Self-hosting: `sys/src/build/host/guest/build-self.sh` builds the whole tree in the guest with `build.exe` (GOOS=plan9) and the native gcc of `../9front-gcc`, on an ext4 disk made from a copy (`$CLI_DIR/self.$objtype`, `ext4srv`; `objtype=386`: a 9pc guest with `/386/gnu`); the steps run as `sys/src/build/step/rc/*.rc` (`build.go`: `plan9step`). It compares every object with the Linux build; `KEEP=1` reuses the copy. On 9front, `gccmk` (from `../9front-gcc/plan9`, in `/$objtype/gnu/bin`) runs 9front's own mkfiles with gcc (userland only).
- Go: `../9front-go` (go1.27.3, plan9/arm64 ported there; `scripts/install.sh` makes `/$objtype/go`); `GO_OUT=../9front-go/out/$objtype` puts it on the CD (`stage-dist.sh`, `sys/src/build/host/guest/boot/goproto`), as `RUST_OUT` does for `../9front-rust`.
- `sys/src/build/host/tests/run.sh` categories: static, asm, syscall, build, guest, boot, gui, disk, iso, rio, usb (`sys/src/build/host/tests/README.md`). `sys/src/build/host/tests/iso/drive.py` boots the CD to rio and types into it; `sys/src/build/host/tests/vm.py` is the module for guest tests on the CD (rio, usb): boot to rio, QMP keys/mouse, an rc on the serial line.

## Debugging a hung guest
- `sys/src/build/host/debug/qemu-ctl.py DIR type|ser|shot|hmp`: keys via QMP, screendumps, monitor commands, and a second rc on the serial line (QEMU needs `-qmp unix:DIR/qmp.sock,...` and the serial line as `DIR/serial.sock`; socket paths < 108 bytes).
- `sys/src/build/host/debug/thread-dump.py DIR PID BINARY`: libthread procs and threads of a guest process with stacks (needs the unstripped binary from `/$objtype/bin`).
- Kernel: `qemu-ctl.py DIR hmp 'gdbserver tcp::1234'`, then `gdb amd64/bin/9pc64` and `target remote :1234`.

## Checks for gcc-vs-kencc differences (sys/src/build/host/check, sys/src/build/host/fix)
`cmp-struct-layout.sh` (struct sizes against a kencc reference: long 32 bit, pack honoured), `find-long-io.py` (long data in raw I/O), `find-spinloops.sh` (endless busy-waits in a kernel binary), `find-shortened-members.py` (member paths shorter than in 9front), `cmp-lp64.sh` (6c/6a/6l output under -m32 vs -m64), `fixulongmax.py`.

`.opencode/` at the repo root is unrelated tooling (opencode CLI agent), not part of the build.
