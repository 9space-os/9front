#!/bin/bash
# Run the gcc port's tests by category.
#
#   static   source checks, no build needed
#   asm      host unit tests of the GAS ports of 9front's assembly (libc
#            amd64, libmp and libsec amd64/386, lib387)
#   hid      host unit test of nusb/kb's hid.c: a precision touchpad's gestures
#   syscall  generated system call stubs (scripts/build/gen-9syscall.sh)
#   build    results of the build tool (libraries, commands, kernel)
#   guest    unit tests (libc, libthread, libmp, auth, APE, ...) built with the
#            port toolchain, run in 9front under QEMU (needs a build; boots once)
#   boot     CLI boot smoke test via scripts/guest/boot-cli.sh (boots once)
#   replica  updates over HTTP: replica/pull through replica/httpfs, signed index
#            (tests/replica, boots once)
#   pmc      performance counters, pmc(3): tests/pmc/pmctest.c on 4 cpus
#            (amd64 with KVM's virtual counters, then without; arm64)
#   acpi     ACPI's events (#P/acpiev, aux/acpi's acpievents, backlight,
#            aux/acpikeys): QEMU's power button, a hot-plugged device, an SSDT
#   wlan     etherwlan (an 802.11 interface whose radio is a program) over
#            simulated access points
#   gpu      the GPU stack on QEMU's virtio-gpu: LinuxKPI selftests, aux/virtiogpu,
#            gpu(4); with a render node venus, Vulkan, GL, wsys on GPU buffers
#   gui      terminal boot: VESA via aux/realemu, rio (boots once;
#            GUI_SCREENSHOT=file.png keeps the screen dump)
#   ui       wsys (sys/src/cmd/wsys) on VESA: windows, shared segments, focus,
#            input, its scripts' tests; then in a rio window and through rcpu
#   draw     drawfs (draw(3) in user space) against the kernel's devdraw: the same
#            operations, the same pixels (tests/draw/drawcmp.c, VESA, boots once)
#   disk     install image with hjfs (scripts/guest/mkdisk.sh, minutes), booted
#            on its own (DISKIMG=image skips building it)
#   iso      install/live CD (scripts/guest/mkiso.sh): booted as CD and as disk
#            (hybrid), each with BIOS and UEFI, up to the desktop (wsys or rio;
#            ISO=image skips building it)
#   rio      mouse and keyboard on the CD's desktop (rio, or wsys where the CD
#            has it): select, type, New + sweep (ISO=image, else the last CD
#            made by scripts/guest/mkiso.sh)
#   usb      USB stick on xHCI, EHCI, UHCI, OHCI: root hub, /dev/sdU*,
#            sector 0 (same CD; USB_CTLRS="xhci ehci" for fewer)
#   workflow the reference workflow on a CD with wsys (dist/bin/mkcd ui, or ISO=):
#            term, sam, paint, uitodo, foot typed into by the host; the files
#            over 9P from the host; files (the file manager); nothing Broken
#            (boots once)
#   gcc      the native gcc on the CD (/$objtype/gnu, toolchain/gcc: GCC_DIR in
#            paths.env): a POSIX and a native Plan 9 program compiled, linked
#            and run, gccmk (same CD)
#   go       Go on the CD (/$objtype/go, toolchain/go: GO_DIR, GO_OUT for
#            mkiso.sh): go version, GOROOT, go run, go build, cgo, a module
#            (same CD; skipped without Go)
#   rust     Rust on the CD (/$objtype/rust, toolchain/rust, mkcd -dev rust): rustc,
#            cargo build/run/test offline, rustfmt, rustdoc, clippy (same CD;
#            skipped without Rust)
#   mcp      the MCP server on the CD (mkcd -mcp): initialize, rc through it, its
#            deny list (same CD; skipped without it)
#   hw       the hardware test stick (tests/hw) in QEMU: boots from USB,
#            runs the configurations with resets, HANG after a power-off
#            (makes the stick image, minutes)
#
# Not in the default list, run by name: sdl, glfw, winit, uiserver, wayland
# (guests of scripts/guest/mcp-guest.sh; they print ok/FAIL lines themselves).
#
# usage: tests/run.sh [category...]     (default: the list above, in its order)
cd "$(dirname "$0")"
cats=${*:-static asm hid syscall build guest boot replica pmc acpi wlan gpu gui ui draw disk iso rio usb workflow gcc go rust mcp hw}
failed=()
for c in $cats; do
	[ -x "$c/run.sh" ] || { echo "unknown category $c"; exit 2; }
	echo "== $c"
	./"$c"/run.sh || failed+=("$c")
done
echo
if [ ${#failed[@]} -eq 0 ]; then
	echo "all categories passed: $cats"
else
	echo "failed categories: ${failed[*]}"
	exit 1
fi
