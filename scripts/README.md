# scripts

Helpers of the gcc port. Run them from the tree root; most take
`objtype=386|amd64|arm64` from the environment (default amd64).
The build itself is `CC=gcc ARCH=amd64 sys/src/cmd/build/build.exe`.

## lib/ — where 9space's other parts are

| File | What |
|---|---|
| `paths.sh` | sourced after `ROOT=`: a stub of the distribution's `dist/lib/9space-paths.sh` (dist beside the tree), which exports every variable of `paths.env` (`$NINESPACE_PATHS`, else `dist/paths.env`) as an absolute path: `GCC_DIR`, `RUST_DIR`, `GO_DIR`, `MCP_DIR`, `DISTFILES` (the releases' archives), `TREE_GCC`, `CACHE`, `CLI_DIR`, `LINUX`, ...; the environment wins; without dist or paths.env an error, no defaults |
| `paths.py` | the same for Python (its own reader of `paths.env`, also without defaults): `paths.get('GCC_DIR')`; `paths.archive(root, name)` resolves `$DISTFILES/x` of a VERSION or VENDOR.json |

## gen/ — manifest generators

They read 9front's mkfiles and write the checked-in JSON manifests.
Run them after a mkfile changes (e.g. after rebasing onto `front`);
`tests/static/run.sh` checks that the manifests match.

| script | what it does |
|---|---|
| `genlibc.py` | libc manifest (`sys/src/libc/libc.json`, `<arch>/build.json`). `objtype=386 scripts/gen/genlibc.py` |
| `genlibs.py` | library manifests `sys/src/lib*/lib*.json`; `--list` prints the build order. `scripts/gen/genlibs.py libdraw` |
| `gencmds.py` | program manifests `cmds.json`; `--list` build order, `--probe` rebuilds `cmds-broken*.txt`. `scripts/gen/gencmds.py cmd/rio` |
| `genkernel.py` | kernel manifest from mkfile + CONF (pc, arm64; pc64's is hand-made). `scripts/gen/genkernel.py pc` |
| `genbuild.py` | top-level `build.json` and `build.<arch>.json`. `scripts/gen/genbuild.py` |
| `apeprogs.py` | APE's parts made by the cross compiler (`scripts/build/ape.sh`): the libraries' manifests in order, the programs (wlsys, foot, cxxwidgets) and their link arguments, whether the cross compiler of `toolchain/gcc` (GCC_DIR) is there; for genbuild.py and check-install.py. `scripts/gen/apeprogs.py` |
| `rustcrates.py` | the Rust crates of the tree (sys/src/cmd/*/ with rust.json, their programs, and Cargo.toml), for genbuild.py and check-install.py. `scripts/gen/rustcrates.py` |
| `genmusl.py` | manifests of APE's musl part `sys/src/ape/lib/musl/musl[.386\|.arm64].json` (gcc only; sources per arch in `musl/<objtype>.list`); `--list` for genbuild. `scripts/gen/genmusl.py` |
| `genclean.py` | module: the files a manifest's Pre steps generate (its `Clean` key) |
| `arch.py` | module and tool: objtype, O, TOOLPREFIX, kernel conf, QEMU, manifest suffix. `objtype=386 python3 scripts/gen/arch.py` |
| `cmds-broken*.txt` | programs that do not build yet, per arch (written by `gencmds.py --probe`) |
| `i915-regs.py` | aux/intelgpu's register excerpts: the definitions of Linux's i915 the driver uses, verbatim from a Linux tree into excerpts of their headers (`sys/src/cmd/aux/gpu/intel/UPSTREAM`, lines with `*`). `scripts/gen/i915-regs.py [LINUXTREE]` (default LINUX_OLD of paths.env) |

## ape/ — libraries for APE from their releases

| Script | What |
|---|---|
| `vendor.sh` | a library into the tree from its release as `sys/src/<lib>/VENDOR.json` describes it (version, archive and its sha256, the sources, the public headers): the sources and what they include (the cross gcc's -MM) into `sys/src/<lib>/vendor`, `plan9/` first in the include path, `build.json` for build.exe (step `ape`). `scripts/ape/vendor.sh freetype` |

## build/ — steps the manifests run

Called by `build.exe` (Pre/Post steps); rarely run by hand.

| script | what it does |
|---|---|
| `prep-objtype.sh` | install directories and gcc's `libgcc.a` for `$objtype` |
| `gen-rebootcode-i.sh`, `gen-rebootcode-pc-i.sh`, `gen-apbootstrap-i.sh`, `gen-etherm10g-i.sh`, `gen-sd53c8xx-i.sh`, `gen-initcode-i.sh` | the `.i` files the kernel includes, as the kernel mkfiles make them |
| `kheader.sh` | a kernel header that only includes another dir's (bcm64's mkfile rules) |
| `gen-bootfs.sh` | `/boot/boot` and `$CONF.bootfs.paq` embedded in the kernel. `scripts/build/gen-bootfs.sh pc64` |
| `gen-src.sh` | generated sources of single programs (Pre step in their directory) |
| `gen-9syscall.sh` | libc's system call stubs (GAS). `objtype=386 scripts/build/gen-9syscall.sh` |
| `libp9.sh` | `$objtype/lib.gcc/libp9.a`: the native libc with its symbols renamed `__p9_*`, for programs with APE in them too (Rust). libc's Post step |
| `musl-localize.sh` | in libap's musl members, the names libap's own members define too become local (i386 floor.s: ceil). Post step of the musl manifest |
| `gen-musl-bits.sh` | musl's `bits/alltypes.h` and `bits/syscall.h` for APE's musl part, as musl's Makefile makes them. `scripts/build/gen-musl-bits.sh x86_64` |
| `gen-mpc.sh`, `gen-runetype.sh`, `gen-latin1-h.sh`, `gen-jpg-tables.sh`, `gen-awk-proctab.sh`, `gen-upas-yacc.sh` | sources made by host tools (mpc, mkrunetype, mklatin, ...) as the mkfiles do |
| `yacc.sh`, `lex.sh` | 9front's yacc/lex templates with plan9port's tools |
| `mkinstall.py` | the install rules of the mkfiles (scripts, data files) |
| `elf2aout.py` | Plan 9 a.out of the ELF kernel, where 9boot loads it. `scripts/build/elf2aout.py amd64/bin/9pc64 amd64/9pc64` |
| `check-install.py` | is everything `mk install` installs there? `-r` removes stale files, `-i x.iso` checks a CD. `scripts/build/check-install.py` |
| `clean.sh` | remove the build's files from the source dirs (`-i`: installed files too). `scripts/build/clean.sh` |
| `cargo.sh` | a Rust crate of the tree (its programs in `rust.json`, `scripts/gen/rustcrates.py`; no network: `vendor/`), built with the stage-1 rustc of `toolchain/rust` (RUST_DIR) and linked against this tree; programs to `/$objtype/bin` (amd64, 386, arm64; `-n`: without the default features, `gpu`, where there is no Mesa); without Rust it skips (check-install: norust). build.exe's step `rust`; rc twin `rc/cargo.rc` (native cargo, rustc, gcc). `scripts/build/cargo.sh sys/src/cmd/wsys wsys` |
| `cargostd.sh` | sourced: `cargostd dir target` empties cargo's target directory for a target when RUST_DIR's std was built again (cargo looks at rustc's version only), and the host's part when the host's std changed; cargo.sh and the tests that run cargo (tests/winit, tests/uiserver, tests/gpu) |
| `cargorelink.sh` | sourced: `cargorelink rel lib p...` makes cargo link a program again when it is older than one of the archives below lib (cargo has no fingerprint of the C libraries it links: libvulkan.a, APE's); cargo.sh and browser/build.sh |
| `ape.sh` | APE's parts made with the cross compiler of `toolchain/gcc` (GCC_DIR): build.exe -D on the libraries of `scripts/gen/apeprogs.py` (sys/src/mesa: libvulkan.a, libffi, wayland, sdl3, glfw, freetype, pixman, expat, xkbcommon, fontconfig, fcft, foot), then the programs of them (wlsys by `scripts/wayland/build.sh`, foot, cxxwidgets) into `/$objtype/bin`; without the compiler it skips (check-install: nocross). build.exe's step `ape`; rc twin `rc/ape.rc` (the native gcc). `scripts/build/ape.sh` |
| `wsys-shaders.sh` | wsys' shaders (`sys/src/cmd/wsys/shaders`, the GPU's blending of layers with alpha) to SPIR-V with the host's glslc, checked in. `scripts/build/wsys-shaders.sh` |
| `rc/*.rc` | the same steps in rc, for `build.exe` running on 9front itself (self-hosting): `build.exe` maps `bash …/X.sh` and `python3 …/X.py` to `rc …/rc/X.rc`. `lib.rc` holds the shared helpers (`hostcc`: helper programs with the native gcc, `bin2c`, `mkvar`) |

## hw/ — the hardware test stick (tests/hw/README.md)

| script | what it does |
|---|---|
| `mkstick.sh` | build the stick image (9fat with logs, hjfs root with the test runner and test programs) in a QEMU guest; `PACK=1`: a hand-off dir (packed image, collect.py, HOWTO.txt with the dd line). `scripts/hw/mkstick.sh [image]` |
| `collect.py` | copy `hwtest/` from the stick's 9fat (or the image) and print the summaries. `scripts/hw/collect.py /dev/sdX [dir]` |
| `pxe.sh` | boot a machine of the LAN by PXE from this host: proxy DHCP and TFTP by dnsmasq (9boot, the kernel, `/cfg/pxe/<mac>`), the root by ufs; `tests/hw/machines/<machine>`: its plan9.ini lines and firmware; `QEMU=1`: a rehearsal in QEMU. `scripts/hw/pxe.sh mac [machine]` |
| `ufs/` | the 9P file server for the PXE laptops' and the QEMU guests' root: Harvey's ufs (`github.com/Harvey-OS/ninep`, BSD-3-Clause, vendored) with a main of our own that turns TCP keepalive off, so a laptop asleep for more than ~150 s keeps its root; `build.sh OUT` builds it (no network) when OUT is missing or older than a source and prints OUT, the default `UFS` of `pxe.sh` and `scripts/guest/boot-cli.sh`. `scripts/hw/ufs/build.sh $CLI_DIR/ufs` |
| `soak.py` | the laptop's working day as a test on a machine booted by `pxe.sh`: `tests/hw/soak.rc` started over MCP (rounds of ui, Vulkan, draw(3), term and Wayland programs in windows, each round checked), watched from here (pings, its log in the PXE root, WLAN transfers, a hang); out in `$CLI_DIR/soak/<date>/`. `scripts/hw/soak.py [-m minutes] [-r seconds] [-w wlanaddr] [-H addr] [-n]` |
| `termcpu.rc` | installed by pxe.sh as a test machine's `/rc/bin/termcpu`: the terminal as its own auth and cpu server (authsrv, rcpu) for drawterm; asks for the keys once, then termrc.local starts it at boot. For testing only |
| `linux-rtw89-dump.sh` | on a Linux live system (as root): what Linux's rtw89 does with the RTL8852CE (system, IOMMU, lspci of card and bridge, rtw89 reloaded with `debug_mask=0x1000`, BAR2 registers after the firmware download), to compare with aux/rtw89; reads only, writes its report. `sudo sh linux-rtw89-dump.sh` |

## guest/ — QEMU guest, install media, boot loaders

| script | what it does |
|---|---|
| `boot-cli.sh` | boot the built kernel in QEMU; with arguments, type them and print the output. `scripts/guest/boot-cli.sh 'ls /bin'` |
| `vfio-gpu.sh` | the host's GPU for a guest (single-GPU VFIO): `sudo scripts/guest/vfio-gpu.sh bind [PID]` (GPU and HDMI audio to vfio-pci, /dev/vfio/N for the user, memlock of PID unlimited), `release` (back to amdgpu), `status`. `scripts/guest/vfio-gpu.sh` |
| `vfio-test.sh` | boots the build's kernel with the card passed through (KVM, `-cpu host`) and lists what the guest sees of it (`pci -bv`, configuration space). `scripts/guest/vfio-test.sh [cmds]` |
| `guest-cmd.sh` | one command line into a guest kept running (`HOLD=1 boot-cli.sh`). `scripts/guest/guest-cmd.sh 'ps'` |
| `mcp-guest.sh` | a guest (`HOLD=1 boot-cli.sh`) running the MCP server (`tools/mcp`, MCP_DIR), reachable from the host on 127.0.0.1:18091 (second NIC); `-disks`, no deny list. `scripts/guest/mcp-guest.sh aux/virtiogpu` |
| `mcp.py` | commands and files in that guest over HTTP, much faster than typing on the serial line. `scripts/guest/mcp.py wait; scripts/guest/mcp.py rc 'ps'` |
| `build-boot.sh` | build 9front's boot loaders in the guest with its own compilers (mbr, pbs, 9boot*, efi). `scripts/guest/build-boot.sh` |
| `build-kencc.sh` | check that the tree still builds with 9front's own compilers: in the guest `mk install` of `/sys/src` and the kernel pc64 with 6c/6a/6l (`boot/mkkencc.rc`). `scripts/guest/build-kencc.sh [kernel]` |
| `build-self.sh` | self-hosting: the guest builds the whole tree with `build.exe` and the native gcc of `toolchain/gcc` (GCC_DIR; `boot/mkself.rc`) on an ext4 disk made from the copy in `$CLI_DIR/self.$objtype` (`mkfs.ext4 -d`, served by `ext4srv`, read back with `debugfs rdump`), then compares every object with the Linux build (`$CLI_DIR/self.diff`). `KEEP=1` reuses the copy. `[objtype=386] scripts/guest/build-self.sh [x.json ...]` |
| `build-uboot.sh` | u-boot for QEMU arm64 (used by `boot-cli.sh`) |
| `mkiso.sh` | install/live CD, made in the guest. `objtype=386 scripts/guest/mkiso.sh`; `PLAN9INI='monitor=i915'` sets lines of the CD's plan9.ini (a GPU driver as the screen from the start) |
| `mkdisk.sh` | installed disk image (hjfs). `scripts/guest/mkdisk.sh` |
| `firmware-used.py` | `firmware.used`: the firmware the tree's GPU and WLAN drivers can load (their sources' MODULE_FIRMWARE, preprocessed as the manifests compile them; rtw89's older formats too); `stage-firmware.py -u` keeps only those of firmware.spec's globs. Run after the drivers change, then `dist/bin/mkfirmware -u`. `scripts/guest/firmware-used.py` |
| `stage-dist.sh` | the distribution tree the guest gets for mkiso/mkdisk; `PROFILE` (minimal, gpu, ui, full) leaves the other groups' files off, `DEV` (gcc, rust, go) picks the languages (`dist/bin/mkcd`); `REPLICA_DIR`, `REPLICA_KEY`, `REPLICA_URL` publish it for pull(1) and put the client on the CD; `FIRMWARE=0` leaves /lib/firmware empty (the firmware alone: `dist/bin/mkfirmware`) |
| `stage-firmware.py` | the CD's /lib/firmware: linux-firmware's WLAN and GPU firmware under the drivers' names (`firmware.spec`), each file checked against `firmware.list` (sha256); `-u` makes the list anew from `FWDIR` (default /usr/lib/firmware). `scripts/guest/stage-firmware.py dir` |
| `profiles` | the CD's installation profiles and the files of their groups (gpu, ui) |
| `gen-ufs-root.sh`, `proto.py` | root tree for the guest; files of a 9front proto (`proto.py -l`: each path's mode and owners as mkfs gives them) |
| `boot/` | rc scripts run inside the guest (`mkboot.rc`, `mkiso.rc`, `mkdisk.rc`, `mkkencc.rc`, `mkself.rc`, `mkstick.rc` for `scripts/hw/mkstick.sh`), `gccproto` (`/$objtype/lib.gcc`), `uiproto` (the ui group's `/lib/font/ttf`, `/lib/theme`, `/lib/wbar`), `gnuproto` (`/$objtype/gnu`, the native gcc of `toolchain/gcc`, GCC_DIR), `rustproto` (`/$objtype/rust`, Rust of `toolchain/rust`, RUST_DIR, `scripts/install.sh` there; only with `RUST_OUT`), `goproto` (`/$objtype/go`, Go of `toolchain/go`, GO_DIR, `scripts/install.sh` there; only with `GO_OUT`), `vkproto` (`/$objtype/vk`, tests/gpu's Vulkan programs), `wlproto` (`/tests`, tests/wayland's programs) and `replicaproto` (`/dist/replica`, the pull(1) client of `replica/publish.py`) |

## replica/ — updates over HTTP (pull(1))

| File | What |
|---|---|
| `publish.py` | a staged tree as a replica replica/httpfs serves to replica/pull: db, log, objects by SHA-256, the index signed with RSA (openssl); `-c URL` puts the client side into the stage (`/dist/replica`), `-g key.pem` makes a key; objects neither the new nor the previous index names are removed. `dist/bin/mkcd` runs it for every CD. `scripts/replica/publish.py -k key.pem -c URL stage stage.list pubdir` |

## check/ — gcc vs. 9front's compilers

| script | what it does |
|---|---|
| `cmp-struct-layout.sh` | struct sizes against kencc's (long 32 bit, `#pragma pack`); no argument: all known file/wire formats. `scripts/check/cmp-struct-layout.sh` |
| `find-long-io.py` | candidates: `long` data written to or read from files, wire, devices. `scripts/check/find-long-io.py sys/src/cmd/cwfs` |
| `find-spinloops.sh` | busy-wait loops gcc made endless, in a kernel binary. `scripts/check/find-spinloops.sh amd64/bin/9pc64` |
| `find-dropped-volatile.py` | where a kernel drops vmap's volatile from a device register's pointer, casts too (`-Wdiscarded-qualifiers -Wcast-qual` on every kernel source); in `tests/static`. `scripts/check/find-dropped-volatile.py [pc64 pc arm64]` |
| `find-narrow-mmio.py` | volatile loads gcc made narrower than their type (a register read as a byte), from the GIMPLE dump's volatile loads and the objects' reads per line; x86 kernels; in `tests/static`. `scripts/check/find-narrow-mmio.py [manifest...]` |
| `find-clobbered.py` | kernel locals assigned after a `waserror()` and read in its handler (indeterminate after `error()`'s longjmp unless volatile: devmnt.c's mntwalk leaked a chan per failed walk); a heuristic that does not see `poperror`, its hits are to be reviewed; not in `tests/static`. `scripts/check/find-clobbered.py file.c...` |
| `find-clobbered.py` | locals a jump back to waserror() can clobber (gcc's `-Wclobbered`; kencc keeps them in memory), in a kernel manifest's sources: changed between waserror() and error() and read after it, they need `volatile`. `scripts/check/find-clobbered.py [sys/src/9/pc64/build.json]` |
| `find-shortened-members.py` | member paths shorter than in 9front (`&mc->Mouse` → `&mc`) |
| `find-promotions.py` | where a uchar/ushort shifted to the top bit (`b<<24`, `s<<16`) reaches a 64-bit or unsigned type through an `int` (kencc zero-extends, gcc sign-extends): clang-query's AST matcher on the C files of the last build (their `.d` files); 0 places since 2026-11-16. `scripts/check/find-promotions.py [dir...]` |
| `list-incompatible-pointers.py` | gcc's `-Wincompatible-pointer-types` places in a build log (`LC_ALL=C`), grouped by kind; `incompatible-pointers.txt` keeps the known ones (the class stays a warning). `scripts/check/list-incompatible-pointers.py LOG` |
| `find-added-casts.py`, `find-embedded-casts.py` | casts the port added, casts to embedded member types |
| `cmp-lp64.sh` | 6c/6a/6l output built as -m32 vs. -m64 host programs. `scripts/check/cmp-lp64.sh 6c file.c` |
| `i915-upstream.py` | aux/intelgpu and Linux's i915 (`UPSTREAM`): the hunks between two Linux trees that touch our functions, `-c` names still there, `-n` NOTICE's Linux part. `scripts/check/i915-upstream.py ~/Devel/linux-6.6 ~/Devel/linux-6.12` |

## fix/ — source rewriters for new upstream code

| script | what it does |
|---|---|
| `fixdesig.py` | add the `=` kencc lets designated initializers omit. `scripts/fix/fixdesig.py -n file.c` |
| `fixidents.py` | rename identifiers with runes gcc rejects. `scripts/fix/fixidents.py -n file.c` |
| `fixabsincludes.py` | `#include "/sys/..."` → relative path |
| `fixulongmax.py` | `x == ~0UL` with 32-bit ulong → `(ulong)~0UL` |
| `localize-pc-sources.py` | pc64 manifest: compile `../pc/*.c` as local copies |

## debug/ — looking into a running or hung guest

| script | what it does |
|---|---|
| `qemu-ctl.py` | keys, screendumps, monitor commands, and an rc on the serial line of a guest. `scripts/debug/qemu-ctl.py DIR ser 'ps -a'` |
| `thread-dump.py` | libthread procs and threads of a guest process with stacks. `scripts/debug/thread-dump.py DIR 307 amd64/bin/rio` |
| `kstack.py` | the kernel stacks a guest printed with `tests/gpu/kstacks.rc` (`acid -k` reads PC, SP and the stack words through `/proc/N/mem`; the guest has no kernel symbols), resolved against the ELF kernel with addr2line. `scripts/debug/kstack.py [-e amd64/bin/9pc64] log` |
| `shot2png.py` | the picture `tests/gpu/screenshot` prints in a guest (a passed-through GPU's screen, which QEMU cannot dump), out of a console log, as a PNG. `scripts/debug/shot2png.py $CLI_DIR/console.log shot.png` |
| `vncshot.py` | one frame from QEMU's VNC server as PNG, and colours at points: what the host shows, also with a GL display (virtio-gpu-gl), where QMP's screendump has no surface; `boot-cli.sh` uses it with `VNC=1`. `scripts/debug/vncshot.py SOCK out.png 200,450` |

## mesa/ — Mesa (Vulkan: venus, hasvk, RADV; OpenGL on zink)

Host-side tools for `sys/src/mesa` (release in `VERSION`, signing key in
`keys/`, new files in `overlay/`, changes as `patches/`, what APE lacks in
`plan9/`). Work directory `$MESA_WORK` (default `$CACHE/mesa`, `9space/cache/mesa`);
cross compilers from `toolchain/gcc` (GCC_DIR).

| script | what it does |
|---|---|
| `fetch.sh` | downloads the release tarball, checks sha256 and signature; a venv with meson and Mesa's python modules |
| `prepare.sh` | unpacks it as a git repository (tag `pristine`), commits the overlay (tag `overlay`), applies the patches (`git am`) |
| `patches.sh` | writes `sys/src/mesa/patches` from the commits after `overlay`, copies new files back to the overlay |
| `clc.sh` | `mesa_clc` and `vtn_bindgen2` for the build machine (a native meson build in `$WORK/build-native-clc`): the Intel drivers' OpenCL C shaders; build.sh runs it when needed |
| `build.sh` | `libmesaplan9.a`, `libdrm.a` (`plan9/drm`, with pkg-config files for libdrm and libdrm_amdgpu), pkg-config files for libwayland/libffi/wayland-protocols (Wayland platform: Vulkan WSI, EGL; libwayland weakly via `plan9/include/plan9_wlweak.h`), the meson cross file (a changed one sets meson up anew), meson and ninja; the drivers (venus, hasvk, RADV) as one static archive `libvulkan_mesa.a`; the SDK `sdk/` (headers, `libvulkan.a`: the static loader of `sys/src/mesa/plan9/loader` with the drivers) |
| `vendor.sh` | Mesa's drivers (venus, hasvk, RADV) into the tree from the meson build (`vendor.py`): `sys/src/mesa/vendor` (sources, headers, meson's generated files), the Vulkan headers in `sys/include/ape`, one `mesa.json` per directory with meson's flags, `sys/src/mesa/build.json`: `build.exe` then builds `/$objtype/lib.gcc/ape/libvulkan.a` without meson or python, here with the cross gcc (`CC=x86_64-pc-plan9-gcc`) and on 9front with the native one |
| `members.py` | the objects the ICDs and GL libraries of the meson build are made of, in link order, each once (for build.sh and vendor.py) |
| `vktools.sh` | Khronos' Vulkan-Tools (`sys/src/vktools`: version, patches): `prepare`, `patches`, `build` vkcube and vulkaninfo against the SDK |
| `common.sh` | settings the others source (also ape/, wayland/, sdl3/, glfw/, libffi/): the work directory, `$CROSS`: the cross tools with this tree as sysroot |

`tests/gpu/build-vktest.sh` links `tests/gpu/vktest.c` against it.

## linux/ — Linux's DRM drivers on the LinuxKPI for 9front

`sys/src/linux`: Linux's DRM code unchanged in `vendor/` (what `IMPORT` names, MIT-like files
only; `REJECTED` lists the others, `NOTICE` the notices), our layer in `kpi/` and `replace/`,
the kernel configuration in `kconfig`. The layer follows OpenBSD's LinuxKPI (`openbsd/`, unchanged,
at the commit in `OPENBSD`): `kpi/include` are OpenBSD's headers adapted to the unchanged Linux code
(changes marked `9front`), `kpi/obsd` the OpenBSD kernel API they build on (mutex, rwlock,
tsleep, timeout, task, pool, pci) on 9front, `kpi/linux/drm_linux.c` OpenBSD's; the rest of
`kpi/linux` are the 9front backends (pages, PCI on #G, IRQ proc, files, RCU).

| script | what it does |
|---|---|
| `import.py` | a Linux tree into `vendor/`, with the licence check, `VERSION`, `REJECTED`, `NOTICE`, then the backports of `patches/`. `scripts/linux/import.py [LINUXTREE]` (default LINUX of paths.env) |
| `import-openbsd.py` | OpenBSD's LinuxKPI (its own headers, `drm_linux.c`, `linux_*.c`, its public-domain versions of GPL drm files, `sys/tree.h`, `sys/queue.h`) at the commit in `sys/src/linux/OPENBSD` (its drm at Linux 6.18) unchanged into `sys/src/linux/openbsd/`, the template of `kpi/`; licence check, `openbsd/NOTICE`. `scripts/linux/import-openbsd.py [OPENBSDTREE]` (default OPENBSD_SRC of paths.env) |
| `import-freebsd.py` | FreeBSD's LinuxKPI (`sys/compat/linuxkpi/common`), its rtw89 (BSD choice) and net80211 headers, and drm-kmod's BSD parts (`linuxkpi/bsd`, its own `drm_sysfs.c`, `drm_lease.c`, dma-buf) at the commits in `sys/src/linux/FREEBSD` unchanged into `sys/src/linux/freebsd/`, the second template of `kpi/` (the GPL cleanup); licence check, `freebsd/NOTICE`. `scripts/linux/import-freebsd.py [FREEBSDTREE [DRMKMODTREE]]` (default FREEBSD_SRC, DRMKMOD_SRC of paths.env) |
| `kbuild.py` | the files Linux's Makefiles build with `kconfig` (the GPL ones, listed in `REJECTED`, from the Linux tree: LINUX of paths.env); `--autoconf` makes `kpi/include/generated/autoconf.h`. `scripts/linux/kbuild.py sys/src/linux/vendor drivers/gpu/drm` |
| `census.py` | compiles every chosen file (without those `OMIT` names) against the layer and counts what is missing: the measure of progress. `scripts/linux/census.py [-v]` |
| `depth.py` | past census' first error: stubs missing headers round after round, then counts every missing function, macro and type over all files (register names apart: rejected headers, files to OMIT); the measure of a new Linux version's KPI work. `scripts/linux/depth.py` |
| `undef.py` | compiles all (vendor, replace, kpi/linux, kpi/plan9), links them relocatably and lists the symbols nothing defines: what the layer still lacks. `scripts/linux/undef.py [outdir]` |
| `genbuild.py` | the build.exe manifests: `sys/src/linux/build.json` (libkpi9.a, libkpi.a, libkpitest.a, liblinux.a, libamdgpu.a, one object per directory, -O2 as Linux), `sys/src/cmd/aux/gpu/i915/build.json`, `sys/src/cmd/aux/gpu/amdgpu/build.json` and `sys/src/cmd/aux/rtw89/build.json`; `-n` compares (tests/static). `scripts/linux/genbuild.py` |
| `provenance.py` | lines of our LinuxKPI (`kpi/`, `replace/`, also `kpi/linux/*.c`) that are text of a GPL-only or unlicensed Linux file (all of LINUX and LINUX_OLD of paths.env, Linux 7.3.1 and 6.6, not only the same name; fails without them) and of no permissive source (Linux MIT/BSD files, `vendor/`, `openbsd/`, `freebsd/`); `--check` fails above 2 lines or the file's entry in `sys/src/linux/PROVENANCE`. `scripts/linux/provenance.py [-v] [--linux TREE]` |
| `gentrace.py` | no-op trace point headers (Linux's are GPL) for the `trace_*` the vendor code calls, with the originals' includes, into replace/include (i915's, drm's, intel_display's, intel_uncore's: its HEADERS). `scripts/linux/gentrace.py [LINUXTREE]` (default LINUX of paths.env) |

## sdl3/, glfw/, libffi/, wayland/ — libraries for APE from their releases

The releases' archives are in `distfiles` (DISTFILES of paths.env); the cross gcc as `scripts/mesa/common.sh` sets it up.

| script | what it does |
|---|---|
| `sdl3/vendor.sh` | SDL3 into the tree (sys/src/sdl3): the release with sys/src/sdl3/overlay over it, the files SOURCES names, our plan9/ sources and their headers into vendor/, the public headers into /sys/include/ape/SDL3, build.json (libSDL3.a). `scripts/sdl3/vendor.sh` |
| `glfw/vendor.sh` | GLFW into the tree (sys/src/glfw): the release zip with sys/src/glfw/overlay over it, the files SOURCES names, our plan9/ sources and their headers into vendor/, the public headers into /sys/include/ape/GLFW, build.json (libglfw3.a). `scripts/glfw/vendor.sh` |
| `libffi/vendor.sh` | libffi into the tree (sys/src/libffi): the release tarball, the files SOURCES names and their headers into vendor/, ffi.h (from ffi.h.in, as configure for amd64) and ffitarget.h into /sys/include/ape, build.json (libffi.a). `scripts/libffi/vendor.sh` |
| `wayland/vendor.sh` | libwayland into the tree (sys/src/wayland): wayland and wayland-protocols, the files SOURCES names into vendor/, the protocols' code from the host's wayland-scanner into gen/, the public headers into /sys/include/ape, build.json (libwayland-client.a, libwayland-server.a, both with the protocol code SOURCES names; libwayland-egl.a, libwayland-cursor.a). `scripts/wayland/vendor.sh` |
| `wayland/build.sh` | libffi and libwayland (build.exe -D) and wlsys (sys/src/cmd/wlsys, the Wayland server in wsys) with the cross gcc; wlsys into the given directory (scripts/build/ape.sh runs it in the full build). `scripts/wayland/build.sh outdir` |
| `wayland/weston.sh` | weston's example clients weston-simple-shm and weston-simple-egl (`sys/src/weston`: version, patches) against the tree's libwayland and Mesa: `prepare`, `patches`, `build [outdir]` (default `$BUILD/weston`); for tests/wayland. `scripts/wayland/weston.sh build` |

## rust/ — the Rust crates of the tree

| script | what it does |
|---|---|
| `vendor.sh` | The crates.io crates of a Rust crate or workspace of the tree into its vendor/ (cargo vendor) for a build without network; only those the plan9 targets (amd64, 386, arm64) build keep their sources (no tests, benches, examples), the others become stubs. `scripts/rust/vendor.sh [dir]` (default sys/src/rust, the tree's workspace) |
| `crates.sh` | The Rust crates 9front changed (winit, softbuffer, raw-window-handle, wgpu, ...) and p9win into sys/src/rust, copied from `toolchain/rust` (RUST_DIR; its patches/crates are the master; `-n`: only compare, as tests/static does). `scripts/rust/crates.sh` |
