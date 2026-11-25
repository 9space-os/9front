# tests

Tests of the gcc port, by category. Run them from the tree root.

	bash sys/src/build/host/tests/run.sh                  # the default categories (sys/src/build/host/tests/run.sh's list and order)
	bash sys/src/build/host/tests/run.sh static build     # some of them
	objtype=386 bash sys/src/build/host/tests/run.sh iso  # another architecture (default amd64)

Each category prints `PASS`/`FAIL`/`SKIP` lines and a summary; the
script exits non-zero if a category failed. A category whose
prerequisite is missing (no kernel, no CD, no OVMF) is skipped.
`draw`, `sdl`, `glfw`, `winit`, `uiserver` and `wayland` run in a guest of
`sys/src/build/host/guest/mcp-guest.sh` and print `ok`/`FAIL` lines of their own;
`sdl`, `glfw`, `winit`, `uiserver` and `wayland` are not in the default
list: name them.

## Before testing

1. Build everything for the architecture:

		cd sys/src/cmd/build && GO111MODULE=off go build -o build.exe . && cd -
		CC=gcc ARCH=amd64 sys/src/cmd/build/build.exe

2. For `iso`, `disk`, `rio` and `usb` also 9front's boot loaders (built in a QEMU guest) and, for `rio`/`usb`, a CD (`sys/src/build/host/guest/mkiso.sh`):

		sys/src/build/host/guest/build-boot.sh

Host tools: gcc (and `aarch64-linux-musl-gcc` for arm64), Go, binutils,
plan9port, bison, bwrap, QEMU with KVM (x86 guests need a writable `/dev/kvm`; `KVM=0` emulates; arm64 guests are emulated on an x86 host),
grub-mkrescue, xorriso, OVMF for the UEFI cases, ufs (root file system of
the guest: Harvey's, without TCP keepalive, built from `sys/src/build/host/hw/ufs` with Go into `$CLI_DIR/ufs`; `UFS=` another).

## Categories

| category | what it tests | needs | time |
|---|---|---|---|
| `static` | source checks: manifests match their generators, no GPL text of Linux in the LinuxKPI (`sys/src/build/linux/provenance.py --check` against LINUX and LINUX_OLD of the distribution's paths.env; fails without them), no `ulong == ~0UL`, no unreviewed casts, member paths as in 9front, struct layouts of file/wire formats as kencc's (`sys/src/build/host/check/cmp-struct-layout.sh`), GAS ports beside 9front's `.s`, the rc build steps parse (plan9port rc) | nothing | seconds |
| `asm` | the GAS ports of 9front's assembly (libc amd64, libmp and libsec amd64/386, lib387) as host unit tests, against glibc or the portable C | gcc | seconds |
| `hid` | `sys/src/cmd/nusb/kb/hid.c` (nusb/kb's and aux/i2chid's HID code) compiled on the host (`inc/`: Plan 9's types) and fed a precision touchpad's reports: the Input Mode feature report, one finger's motion (speed-dependent), taps with one to three fingers, two fingers' scrolling (also natural), the pad's button by the fingers on it, a drag, a palm, hybrid reports | the host's cc | seconds |
| `syscall` | the generated amd64 system call stubs (`sys/src/build/step/gen-9syscall.sh`), whatever objtype | gcc | seconds |
| `build` | the build's results: libraries, programs, kernel; kernel without endless busy-waits (`sys/src/build/host/check/find-spinloops.sh`) | a build | seconds |
| `guest` | unit tests built with the port and run inside 9front: libc, libthread, libmp, auth, pak, APE (`sys/src/build/host/tests/guest/*.c`) | a build, QEMU | ~1 min |
| `boot` | boot to the console rc (cpu service), run commands | a build, QEMU | ~1 min |
| `pmc` | performance counters (`#ε`, pmc(3), pmc(2)): `sys/src/build/host/tests/pmc/pmctest.c` wired to cpu1 on 4 CPUs (counts over a loop, user/os filters, RDPMC or `PMEVCNTR0_EL0` in user mode, another core's counter, two readers at once, `#ε/core`); x86 boots with `PMU=1` (KVM's virtual counters), then without (no counters); arm64 (TCG) has PMUv3 (`cycles` only) | a build, QEMU | ~2 min |
| `acpi` | ACPI's events: the kernel's `#P/acpiev` (the SCI, GPE and PM1 blocks) and aux/acpi's `acpievents` in QEMU (i440fx): the power button (QMP `system_powerdown`, PM1), a PCI device hot-plugged (QMP `device_add`: GPE 1, `\_GPE._E01`, its `Notify`), an SSDT of our own (`sys/src/build/host/tests/acpi/ssdt.py`, `-acpitable`): a display output's `Notify` 0x86 as `key brightnessup`, a lid's as `lid open`, aux/acpi's `backlight` through `_BCL`, `_BCM`, `_BQC`, and aux/acpikeys moving it on the key (no GPU driver) | a build, QEMU | ~1 min |
| `wlan` | etherwlan (`ether1=type=wlan`: wifi.c over a radio that is a program, #ω, wlan(3)) with `wlansim.c` as radio and access points (9space on channels 6, 11 and 40, 9space5 on 36, 9spacewpa on 100 with WPA2-PSK): scan of 2.4 and 5 GHz (passive on DFS channels), authentication, association, the station's address by the radio's ctl, ARP, ping on each; aux/wpa's 4-way handshake and CCMP; gpu(3)'s `*userpci=` | QEMU, the build | a minute |
| `hw` | the hardware test stick (`sys/src/build/host/tests/hw/README.md`) in QEMU: boots from a USB stick on xHCI, runs its configurations with resets, logs `HANG` after a power-off, logs read back from its 9fat | a build, boot loaders, QEMU | minutes |
| `gpu` | user-space GPU driver PoC on QEMU's virtio-gpu (`-vga none`): `#G`, vga type `gpu`, `aux/virtiogpu`, `/shr/gpu/0` (shr(3)), mode switch, drawing checked in the screen dump; with a render node also venus (Vulkan, GL through zink, a buffer between two processes: vkshare) and vktri in a window of wsys through GPU buffers (amd64 only) | a build, QEMU | ~1 min, ~15 with venus |
| `gui` | terminal boot: VESA via aux/realemu, rio, typing into a window | a build, QEMU | ~1 min |
| `ui` | wsys (sys/src/cmd/wsys), the window system, on VESA without a GPU driver: wsystest (C) makes windows, fills their segments, checks the screen, focus, stacking, click, key, resize, move and sweep by the mouse; then the same in a rio window (amd64) | a build, the stage-1 rustc of `toolchain/rust` (RUST_DIR in paths.env), QEMU | ~1 min |
| `draw` | drawfs (sys/src/cmd/drawfs: devdraw as a file server in user space) against the kernel's devdraw on VESA, in a guest of `sys/src/build/host/guest/mcp-guest.sh`: sys/src/build/host/tests/draw/drawcmp draws fills, masks, alpha, all operators, text, curves, channels and windows on a screen of an image, each result's SHA-1 the same on both | a build, QEMU | ~1 min |
| `sdl` (`sys/src/build/host/tests/sdl/run.sh`) | SDL3 (sys/src/sdl3) in a window of wsys on VESA, in a guest of `sys/src/build/host/guest/mcp-guest.sh`: sys/src/build/host/tests/sdl/sdltest (driver, window size and title, surface and software renderer read back from /dev/window, clipboard, events, audio if any), then its events: the mouse through /dev/mousein (window coordinates, buttons, wheel), keys the host sends through QMP (down, up, shift, text) | a build, libSDL3.a, QEMU | ~2 min |
| `glfw` (`sys/src/build/host/tests/glfw/run.sh`) | GLFW (sys/src/glfw) in a window of wsys on VESA, in a guest of `sys/src/build/host/guest/mcp-guest.sh`: sys/src/build/host/tests/glfw/glfwtest (platform, monitor, window size, title, place, resize with callback, clipboard, key names, cursors read back from /dev/cursor, waiting with a time and woken from another thread), then its events: the mouse through /dev/mousein, keys the host sends through QMP | a build, libglfw3.a, QEMU | ~2 min |
| `winit` (`sys/src/build/host/tests/winit/run.sh`) | winit, softbuffer and raw-window-handle with plan9 backends (sys/src/rust: a copy of `toolchain/rust`'s) in windows of wsys on VESA: sys/src/build/host/tests/winit winittest (size, title, softbuffer pixels read back, a resize and its Resized, cursors read back from /dev/cursor) and eguitest (egui drawn by hdrshot-egui-backend (egui_software_backend for egui 0.37): its panel and text in the window), then their events: mouse and keys from the host (keys with text, modifiers), a click on egui's button, text typed into its field; eframetest (eframe on wgpu's Vulkan) runs on the card: sys/src/build/host/tests/gpu/amdeframe.rc | a build, `toolchain/rust`, QEMU | ~3 min |
| `uiserver` (`sys/src/build/host/tests/uiserver/run.sh`) | ui (sys/src/cmd/ui, ui(4): a window of Masonry widgets a client describes through files) in a window of wsys on VESA, the client in rc: scene through ctl (transactions), tree, nodes/, a refused transaction, props, pixels, a click and typing (nodes/id/rect, uipeek), a flushed read of event, the window deleted | a build, `toolchain/rust`, QEMU | ~3 min |
| `wayland` (`sys/src/build/host/tests/wayland/run.sh`) | libffi, libwayland and wlsys (sys/src/cmd/wlsys, sys/src/build/wayland/build.sh) in a guest of `sys/src/build/host/guest/mcp-guest.sh` on VESA: wltest (server and client, wl_shm), wlclient under wlsys in wsys (buffers, a click, keys, resize, close), weston's simple-shm outliving its wsys, a nested wsys, HiDPI scales, foot and its libraries if built; with a render node wlvk (zwp_linux_dmabuf_v1, Mesa's Wayland WSI), vkcube `--wsi wayland`, wlgl and simple-egl on venus | a build with wsys, the cross gcc, QEMU | minutes |
| `replica` | updates over HTTP (pull(1)): `sys/src/build/host/replica/publish.py` publishes versions of a small tree, a guest pulls them with replica/pull through replica/httpfs onto gefs: a new client, a system installed from version 1 (no conflict), a file its user changed (kept, reported), an object altered on the server and an index not as signed (both refused), pull(1)'s snapshot of main holding the old version | a build, QEMU, openssl | ~1 min |
| `disk` | install image with hjfs (`sys/src/build/host/guest/mkdisk.sh`, amd64 only, as 9front's dist mkfile), booted on its own | a build, boot loaders | minutes |
| `iso` | install/live CD (`sys/src/build/host/guest/mkiso.sh`) booted as CD and as USB stick, BIOS and UEFI, up to the desktop, then typing into its first window: the desktop is wsys on a CD that has it (the profiles ui and full), else rio, checked by its process; a profile's CD is checked against what the profile leaves out (`/sys/lib/dist/omitted`) | a build, boot loaders, OVMF | minutes |
| `rio` | mouse and keyboard on the CD's desktop: select text, type, New + sweep a window, type into it; rio on a CD without wsys (minimal, gpu), wsys on ui and full (each check names which) | a CD | ~1 min |
| `usb` | a USB stick on xHCI, EHCI, UHCI and OHCI: root hub in `#u`, the stick as `/dev/sdU*`, sector 0 read back (`USB_CTLRS` for fewer) | a CD | ~1 min per controller |
| `workflow` | the reference workflow (`dist/docs/roadmap.md`, phase 1) on the CD's desktop, keys and mouse from the host through QMP (`sys/src/build/host/tests/workflow/workflow.py`): term typed into (its screen by `TERMDEBUG=screen`), sam (`a/.../` on a new file from the command window, the file's window swept, a line typed into it, `w`), paint (a stroke, `w file`: draw(3)), uitodo (its text box found by ui(4)'s `nodes/id/rect`, an item typed), foot in wlsys (Wayland) typed into; the session's files read by the host over 9P (exportfs behind aux/listen1, QEMU's hostfwd, plan9port's `9p`), one written; files, the file manager (ui(4): a file selected, renamed in its box, plumbed by a second click; a directory made and gone into; a file made from outside shows); at the end all of them running, nothing Broken. Skipped on rio | a CD with wsys (`dist/bin/mkcd ui`; the newest `9space-ui`/`9space-full` in `CLI_DIR`, or `ISO=`), plan9port | ~3 min |
| `gcc` | the native gcc and binutils on the CD (`/$objtype/gnu`, built in `toolchain/gcc`): `gcc --version`, a POSIX program over APE and a native Plan 9 program (`/sys/src/cmd/echo.c`, libc from `/$objtype/lib.gcc`) compiled, linked and run, and `gccmk` builds hoc from its mkfile | a CD with the toolchain | ~2 min |
| `go` | Go on the CD (`/$objtype/go`, built in `toolchain/go`, `GO_OUT` for `mkiso.sh`): `go version`, GOROOT found without `$GOROOT`, `go run` (std compiled on the CD) and `go build` of a program with goroutines, cgo with the CD's gcc, a module from the proxy over https (the CD's CA roots), Go's ELF test data not stripped by `stage-dist.sh`; skipped for a CD without Go | a CD with Go | ~3 min |
| `rust` | Rust on the CD (`/$objtype/rust`, built in `toolchain/rust`; `dist/bin/mkcd -dev rust`, or `RUST_OUT` for `mkiso.sh`): `rustc --version`, a program with threads, a channel and files built by rustc and run (linked with the CD's gcc), `cargo new`, `build`, `run`, `test` offline, a crate from crates.io over https (the CD's CA roots), `rustfmt`, `rustdoc`, `cargo clippy` finding a lint; skipped for a CD without Rust | a CD with Rust | ~5 min |
| `mcp` | the MCP server on the CD (`/$objtype/bin/9front-mcp` from `tools/mcp`; `dist/bin/mkcd -mcp`, `MCP=1` for `stage-dist.sh`): `9front-mcp -stdio` with the CD's deny list answers `initialize`, runs an rc command (`tools/call rc`), refuses `fshalt`; skipped for a CD without it | a CD with the MCP server | ~1 min |

Options (environment):

- `ISO=file.iso` — `iso` tests this CD instead of building one; `rio` and `usb` use it, else the last CD `sys/src/build/host/guest/mkiso.sh` made (`$CLI_DIR/9front.$objtype.iso`; CLI_DIR is `cache/cli` in paths.env). `objtype=386 ISO=$CLI_DIR/9front.386.iso bash sys/src/build/host/tests/run.sh iso`
- `INSTANCE=1` — `gcc`, `go`, `rust`, `mcp`, `rio` and `workflow` run on the instance instead of a CD: an installed 9space in QEMU at `INSTANCE_DIR` (the environment, else `dist/local.env`, not in git), which `dist/bin/instance up` first brings up to date with this build; it keeps running, and each test tidies up after itself (`vm.workdir`: its directory emptied before, removed after when every check passed; `sys/src/build/host/tests/vm.py` `machine()`, `sys/src/build/host/tests/lib.sh` `cdimage`; the wiki's Testing page). `INSTANCE=1 bash sys/src/build/host/tests/run.sh gcc go rust mcp rio workflow`
- `USB_CTLRS="xhci ehci"` — `usb` tests only these controllers.
- `CPU`, `SMP`, `MEM` — QEMU's `-cpu`, `-smp`, `-m` for `rio` and `usb` (e.g. `CPU=n270 SMP=2` for an Atom netbook).
- `DISKIMG=file.img` — `disk` boots this image instead of building one.
- `GUI_SCREENSHOT=file.png` — `gui` keeps the screen dump.
- `KVM=0` — QEMU without KVM (TCG; slower, but checks instructions against the emulated CPU).

`static` is the quick check before every commit; it needs no build.

## By hand

- Boot the built kernel and get a shell on the serial console:

		sys/src/build/host/guest/boot-cli.sh                 # interactive, Ctrl-A x quits
		sys/src/build/host/guest/boot-cli.sh 'ls /bin' 'ps'  # type the lines, print the console

- The guest unit tests in a running system: `/tests/run [test...]`.
- `sys/src/build/host/tests/vm.py`: the Python module `rio`, `usb`, `workflow`, `gcc`, `go`, `rust` and `mcp` use — boots a CD to rio, keys and mouse through QMP, an rc on the serial line; a base for new guest tests.
- Boot a CD in QEMU like `iso` does and see where it stops: `sys/src/build/host/tests/iso/drive.py`
  (`python3 sys/src/build/host/tests/iso/drive.py file.iso cd bios workdir`; the screen dumps
  `screen.ppm` and `final.ppm` end up in `workdir`).
- A guest that hangs: `sys/src/build/host/debug/qemu-ctl.py` and
  `sys/src/build/host/debug/thread-dump.py` (see `sys/src/build/README.md`).
- Mesa is built as a release (`b_ndebug=true`: its asserts off, sys/src/build/mesa/build.sh). Before a release, once: `MESA_ASSERTS=1 bash sys/src/build/mesa/build.sh && bash sys/src/build/mesa/vendor.sh`, the tree's build and `sys/src/build/host/tests/gpu/run.sh` with the asserts on, then the release build again: what only an assert would catch.
- The host's AMD card in a guest (VFIO; bound to vfio-pci, `sudo sys/src/build/host/guest/vfio-gpu.sh bind PID`):
  `sys/src/build/host/guest/vfio-test.sh 'rc /tests/amdall.rc'` with `WAITFOR=TESTEND`; the scripts are
  `sys/src/build/host/tests/gpu/amd*.rc`: `amdall` (contexts, scanout, a submission through the requests as text: amdcs, RADV, vkcube), `amdtext` (each request
  of gpu(4)'s text interface, its reply and errors), `amdhighva` and `amdfault` (amdcs high in the address space, a
  page fault), `amdgl` (zink),
  `amdflip` (the screen as two objects: devdraw's changes into the one not shown, then a flip, one a vblank: no tearing; flips while drawing and after a client's picture, none refused), `amdshare` (an object between two contexts: export, import), `amdvkshare` (RADV's
  dma-buf and a timeline semaphore between two processes), `amdwsys` (vktri in a window
  of wsys, its frames as GPU buffers).
- Real hardware: write the CD to a USB stick (`dd if=9front.amd64.iso of=/dev/sdX bs=1M`)
  or boot it as a CD.
