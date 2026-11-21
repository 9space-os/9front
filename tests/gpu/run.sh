#!/bin/bash
# gpu: the user-space GPU driver on virtio-gpu (QEMU -device
# virtio-gpu-pci, no other display). First the boots without a display
# driver: segment(3)'s revoked segments (segrevoke), the LinuxKPI's
# selftests (aux/i915 -T, with SMBIOS strings and an SSDT: nfst.py),
# i915's mock selftests (-M), its module parameters (-p, i915param.rc)
# and i915live.rc against a stand-in driver.
# Then boots the built pc64 kernel (#G,
# vga type gpu), starts aux/virtiogpu, draws with /tests/gputest (no
# rio), switches the mode through /shr/gpu/0, and checks QEMU's screen
# dump: size of the new mode, the three rectangles at their places.
# gputest -l keeps redrawing them: the kernel console also writes on
# the screen (see gputest.c).
# Second boot, if the host can render (/dev/dri/renderD128): virtio-gpu-gl
# with virgl and venus; /tests/gpuctxtest checks capsets, contexts,
# submissions with fences and a venus host blob mapped through the
# hostmem bar (gpu(4)); with Mesa built, vktest (Vulkan compute on the
# host's GPU), vkdraw (a swapchain on the screen, VK_PLAN9_surface,
# checked pixel by pixel in the guest) and vktri (a triangle drawn with
# shaders by an ordinary Vulkan program, through the static loader);
# Khronos' vulkaninfo and vkcube (60 frames, then the screen is read);
# vkspeed: the processor reads cached host memory fast (gpu(3)'s
# write-back segment), else every frame would crawl. The checks
# in the guest read the draw(3) screen, so those programs copy
# (WSI_PLAN9_COPY=1). Last, vkdraw without it: its frames go to the
# display directly (gpu(4) scanout); the host's display is read
# through VNC. Then the driver killed while vkcube draws (killdrv.rc),
# memory running out while it draws (drvnoswap.rc); wsys in several boots
# of mcp-guest.sh: vktri, ui, SDL and eframetest in its windows as GPU
# buffers (wsysvk.rc), memory running out (wsysnoswap.rc), the GPU
# composing (wsyscomp.rc), a Vulkan window outliving its wsys
# (wsysagain.rc), the frames' order (wsysorder.rc), the driver restarted
# under it (wsysback.rc). Last: vkcube in a rio window, resized, quit
# with q (riovk.rc).
# usage: bash tests/gpu/run.sh   (amd64 only; needs a build)
. "$(dirname "$0")/../lib.sh"
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$objtype" = amd64 ] || { skip "gpu tests" "objtype=$objtype: #G is in pc64 only"; done_tests; exit; }
[ -x "$BUILDEXE" ] || { skip "gpu tests" "no build tool $BUILDEXE"; done_tests; exit; }

buildtests(){ (cd "$HERE" && env PLAN9="$ROOT" ARCH=$objtype CC=gcc "$BUILDEXE" build.json); }
check "build tests/gpu" buildtests || { done_tests; exit; }
# the Vulkan tests need the cross compiler of toolchain/gcc (GCC_DIR in
# paths.env; scripts/mesa/common.sh): build.exe builds
# libvulkan.a from the tree's Mesa (sys/src/mesa/build.json, a minute once).
# Their programs stay from run to run (build-vktest.sh replaces a changed
# one only: the guest's root is kept by rsync); all of them gone when they
# could not be built (root/tests/vkprogs and .new: what build-vktest.sh
# put), so that no stale one runs
novk(){
	local l=$HERE/root/tests/vkprogs n
	cat "$l" "$l.new" 2>/dev/null | sort -u | while read -r n; do rm -f "$HERE/root/tests/$n"; done
	rm -f "$l" "$l.new"
}
cross=$(. "$ROOT/scripts/mesa/common.sh"; echo "$CROSS/$TRIPLE-gcc")
cross_strip=${cross%-gcc}-strip
if [ -x "$cross" ]; then
	check "build libvulkan.a (build.exe, sys/src/mesa) and the Vulkan tests" bash "$HERE/build-vktest.sh" || novk
else
	novk
	skip "Vulkan tests" "no cross compiler $cross (GCC_DIR)"
fi

# segment(3)'s alias and phys segments: removed or revoked, nobody keeps their
# pages (a freed GPU object stays visible to no one); a missing page faults
segrevoke(){
	MEM=1024 UFS_EXTRA="$HERE/root" "$ROOT/scripts/guest/boot-cli.sh" \
		'/tests/segrevoke' > "$TMPD/segrevoke" 2>&1
	grep -q '^segrevoke: ok' "$CLI_DIR/console.log" || { grep -E 'FAIL|ok ' "$CLI_DIR/console.log"; return 1; }
}
check "alias and phys segments revoked (segment(3)): freed objects stay visible to no one" segrevoke

# the LinuxKPI on 9front: aux/i915 -T (segments in procs forked before,
# vmap through alias segments, Linux's dma-buf selftests on our fences)
# with 4 processors: races show (dma_fence_get_rcu revived a freed fence:
# 2 of 10 runs failed so, 1 in many with one)
# with SMBIOS strings of our own (QEMU -smbios): DMI, and the GPU's ACPI
# node by _ADR through aux/acpi (the one interpreter: the driver starts
# it, a second aux/acpi mounts it instead of starting another), and an
# SSDT of our own (nfst.py): a store through Index into a named buffer
kpitest(){
	python3 "$HERE/nfst.py" "$TMPD/nfst.aml" || return 1
	# UFS_EXTRA as in every boot here, though unused: one root for all of
	# them, which gen-ufs-root.sh keeps up to date (without it the tests'
	# GBs were deleted from the root and written again by the next boot)
	SMP=4 MEM=2048 WAITMAX=500 UFS_EXTRA="$HERE/root" QEMU_EXTRA="-vga none -device virtio-gpu-pci -smbios type=1,manufacturer=9front,product=gputest -smbios type=0,vendor=testbios,version=1.2 -acpitable file=$TMPD/nfst.aml" \
		"$ROOT/scripts/guest/boot-cli.sh" 'aux/i915 -T' 'aux/acpi' 'ls /dev/aml /dev/pmctl' > "$TMPD/kpitest" 2>&1
	tr -d '\r' < "$CLI_DIR/console.log" > "$TMPD/kpitest.log"
	grep -q "dma-buf selftests: ok" "$CLI_DIR/console.log" && return
	# keep the console of a failure
	cp "$CLI_DIR/console.log" "$CLI_DIR/kpitest-fail.$(date +%s).log"
	grep -E 'selftest|failed|not seen' "$CLI_DIR/console.log"
	return 1
}
check "LinuxKPI selftests (aux/i915 -T: segments, vmap, Linux's dma-buf selftests)" kpitest
check "LinuxKPI: nth_page by frame, past the end of a chunk" grep -q 'selftest: nth_page by frame, past the chunk: ok' "$TMPD/kpitest.log"
check "LinuxKPI: the page pool's status line" grep -qE 'selftest: pool chunks [1-9][0-9]* \(contiguous [0-9]+, scattered [0-9]+\), [0-9]+ MB, pages free [0-9]+, chunks wholly free [0-9]+, released 0, deferred 0' "$TMPD/kpitest.log"
check "LinuxKPI: the pool gives back wholly free chunks while pages are looked up" grep -qE 'selftest: pool gives back wholly free chunks: .*, 0 wrong: ok' "$TMPD/kpitest.log"
check "/dev/swap's free line (the pager's thresholds), as the pool's watcher reads it" grep -qE 'selftest: /dev/swap.s free line: [0-9]+ free, high water [0-9]+, headroom [0-9]+: ok' "$TMPD/kpitest.log"
check "LinuxKPI: the pool when memory is short (its watcher's check)" grep -qE 'selftest: pool when memory is short: .*: ok' "$TMPD/kpitest.log"
check "DMI from SMBIOS (#P/smbios): QEMU's strings" grep -q 'selftest: dmi: sys vendor "9front", product "gputest", bios "testbios" "1.2"' "$TMPD/kpitest.log"
check "ACPI: the GPU's node by _ADR (aux/acpi's aml)" grep -qE 'selftest: acpi: GPU 0000:00:0[0-9a-f]\.0 is .*, _ADR 0x[0-9a-f]+: ok' "$TMPD/kpitest.log"
check "ACPI: Store to an Index of a named buffer changes the buffer (libaml)" grep -q 'selftest: acpi: Store to an Index of a named buffer: ok' "$TMPD/kpitest.log"
check "one AML interpreter: a second aux/acpi mounts the first" bash -c "grep -q 'aux/acpi: runs already: /srv/acpi mounted on /dev' '$TMPD/kpitest.log' && grep -q '^/dev/pmctl' '$TMPD/kpitest.log'"

# i915's mock selftests (no GPU: a mock device): GEM, GTT, vma, requests, ...
mocktest(){
	MEM=4096 CMDWAIT=3300 UFS_EXTRA="$HERE/root" QEMU_EXTRA="-vga none -device virtio-gpu-pci" \
		"$ROOT/scripts/guest/boot-cli.sh" 'aux/i915 -M' > "$TMPD/mocktest" 2>&1
	grep -q "mock selftests: ok" "$CLI_DIR/console.log" || { grep -E 'failed|<3>|mock selftests' "$CLI_DIR/console.log"; return 1; }
}
check "i915's mock selftests (aux/i915 -M)" mocktest

# Linux's module parameters (aux/i915 -p name=value, i915param.rc):
# refusals, and set ones take effect in the mock selftests
i915param(){
	cp "$HERE/i915param.rc" "$HERE/root/tests/i915param"
	MEM=4096 CMDWAIT=900 UFS_EXTRA="$HERE/root" QEMU_EXTRA="-vga none -device virtio-gpu-pci" \
		"$ROOT/scripts/guest/boot-cli.sh" 'rc /tests/i915param' > "$TMPD/i915param" 2>&1
	tr -d '\r' < "$CLI_DIR/console.log" | awk '
		/^>>/ { cmd = $0; n++ }
		/^<</ { st[n] = $0 }
		/st_timeout=2000/ && /Performing/ { timeout = 1 }
		/Running/ { run[n] = run[n] " " $NF }
		END {
			bad = 0
			for (i = 1; i <= 4; i++)
				if (st[i] !~ /error (2|22)$/) { print "case " i " not refused: " st[i]; bad = 1 }
			if (st[5] !~ /^<< status *$/ || !timeout || run[5] != " sanitycheck") { print "case 5: " st[5] " ran:" run[5]; bad = 1 }
			if (st[6] !~ /^<< status *$/ || run[6] != " vma i915_vma_mock_selftests/igt_vma_pin1") { print "case 6: " st[6] " ran:" run[6]; bad = 1 }
			if (n != 6) { print n " cases ran"; bad = 1 }
			exit bad
		}'
}
check "Linux's module parameters (aux/i915 -p)" i915param

# rc/bin/i915live (one run of aux/i915 -L for all groups, a new run only
# after a failure or a hang) against a stand-in driver (i915live.rc)
i915live(){
	cp "$HERE/i915live.rc" "$HERE/root/tests/i915live"
	MEM=1024 CMDWAIT=300 UFS_EXTRA="$HERE/root" \
		"$ROOT/scripts/guest/boot-cli.sh" 'rc /tests/i915live' > "$TMPD/i915live" 2>&1
	tr -d '\r' < "$CLI_DIR/console.log" | grep -q '^i915live: 33 ok, 4 failed: gt_contexts gt_engines gem gt_lrc (3 runs)$' ||
		{ tr -d '\r' < "$CLI_DIR/console.log" | grep '^i915live:'; return 1; }
}
check "i915live: the live selftests in as few runs as failures allow" i915live

shot=$TMPD/screen.png
boot(){
	MEM=1024 UFS_EXTRA="$HERE/root" SCREENSHOT="$shot" \
	QEMU_EXTRA="-vga none -device virtio-gpu-pci" \
	"$ROOT/scripts/guest/boot-cli.sh" \
		"cat '#G/0/ctl'" \
		'aux/virtiogpu' \
		'cat /shr/gpu/0/status' \
		'cat /shr/gpu/0/outputs/Virtual-1/modes' \
		'echo 1024x768 >/shr/gpu/0/outputs/Virtual-1/mode' \
		'cat /shr/gpu/0/outputs/Virtual-1/mode' \
		"bind -a '#i' /dev" \
		'/tests/gputest -l &' \
		'sleep 2' \
		"cat '#G/0/ctl'" \
		"ls -ld '#σc/gpu' '#σc/gpu/0'" \
		"bind -q '#σ' /shr; /tests/sharetest" \
		'sleep 1' > "$TMPD/console" 2>&1
}
check "boot with virtio-gpu, start aux/virtiogpu, draw" boot
tr -d '\r' < "$TMPD/console" > "$TMPD/log"
cp "$TMPD/log" "$HERE/console.log"
[ -f "$shot" ] && cp "$shot" "$HERE/screen.png"

check "#G lists the device" grep -q '^id 1af4/1050' "$TMPD/log"
check "driver started" grep -q '^aux/virtiogpu: [0-9]*x[0-9]*, fb ' "$TMPD/log"
check "status served under /shr/gpu/0" grep -q '^device virtio-gpu' "$TMPD/log"
check "mode switched to 1024x768" grep -q '^1024x768$' "$TMPD/log"
check "gputest drew on the whole screen" grep -q 'gputest: \[0 0\] \[1024 768\] chan x8r8g8b8' "$TMPD/log"
check "interrupts arrive" grep -Eq '^irq [0-9]+ on [1-9][0-9]*' "$TMPD/log"
# 9space: the driver in shr(3)'s share gpu (gpu(4)); the users' shares
# (sharetest: aux/gpu's usersrv.c, a grant, a squatter, a restart, off)
check "the share gpu is the hostowner's, 0700" grep -Eq "^d-?rwx------ .*/gpu'?\$" "$TMPD/log"
check "sharetest: grant, squatter taken over, restart, off: none failed" grep -q '^sharetest: done, 0 failed' "$TMPD/log"
grep -E '^(ok|FAIL) ' "$TMPD/log" | sed 's/^/  /'

pixels(){
	python3 - "$shot" <<'PY'
import sys, zlib, struct
d = open(sys.argv[1], 'rb').read()
assert d[:8] == b'\x89PNG\r\n\x1a\n'
pos, idat, w = 8, b'', 0
while pos < len(d):
	n, t = struct.unpack('>I4s', d[pos:pos+8])
	c = d[pos+8:pos+8+n]
	if t == b'IHDR':
		w, h, bd, ct = struct.unpack('>IIBB', c[:10])
		assert bd == 8 and ct in (2, 6), (bd, ct)
		bpp = 3 if ct == 2 else 4
	elif t == b'IDAT':
		idat += c
	pos += 12+n
raw = zlib.decompress(idat)
stride = w*bpp
rows, prev = [], bytearray(stride)
for y in range(h):	# undo the PNG filters
	f, line = raw[y*(stride+1)], bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
	for i in range(stride):
		a = line[i-bpp] if i >= bpp else 0
		b, c = prev[i], prev[i-bpp] if i >= bpp else 0
		if f == 1: line[i] = (line[i]+a) & 255
		elif f == 2: line[i] = (line[i]+b) & 255
		elif f == 3: line[i] = (line[i]+(a+b)//2) & 255
		elif f == 4:
			p = a+b-c; pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
			line[i] = (line[i]+(a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
	rows.append(line); prev = line
px = lambda x, y: tuple(rows[y][x*bpp:x*bpp+3])
want = {(200, 450): (255, 0, 0), (500, 450): (0, 255, 0), (800, 450): (0, 0, 255), (700, 200): (0, 0, 0)}
print('size', w, h)
ok = (w, h) == (1024, 768)
for p, c in want.items():
	print(p, px(*p), 'want', c)
	ok &= px(*p) == c
sys.exit(0 if ok else 1)
PY
}
if [ -f "$shot" ]; then
	check "screen dump: 1024x768, red/green/blue rectangles" pixels
else
	fail "screen dump (no $shot)"
fi

if [ -r /dev/dri/renderD128 ] && [ -w /dev/dri/renderD128 ] &&
   $QEMU -device help 2>/dev/null | grep -q '"virtio-gpu-gl-pci"'; then
	boot3d(){
		# CPU=host: SSE4.1's streaming loads read the host visible memory
		# fast (venus: write-combined on the host); venus needs KVM anyway
		# VNC: at the end, what the host shows while vkdraw runs
		# (QMP's screendump has no surface with a GL display)
		CPU=host MEM=1024 UFS_EXTRA="$HERE/root" VNC=1 SCREENSHOT="$TMPD/shot3d.png" \
		SHOTPOINTS="200,450 500,450 900,100 50,700" \
		QEMU_EXTRA="-vga none -display egl-headless -device virtio-gpu-gl-pci,blob=true,venus=true,hostmem=256M -object memory-backend-memfd,id=mem1,size=1024M,share=on -machine memory-backend=mem1" \
		"$ROOT/scripts/guest/boot-cli.sh" \
			'aux/virtiogpu' \
			'cat /shr/gpu/0/status' \
			'/tests/gpuctxtest' \
			'cat /shr/gpu/0/status' \
			"test ! -x /tests/futexstress || /tests/futexstress" \
			"test ! -x /tests/vktest || /tests/vktest" \
			"bind -a '#i' /dev" \
			"test ! -x /tests/vkdraw || WSI_PLAN9_COPY=1 /tests/vkdraw 3" \
			"test ! -x /tests/vktri || WSI_PLAN9_COPY=1 /tests/vktri 2" \
			"test ! -x /tests/rsvk || WSI_PLAN9_COPY=1 /tests/rsvk 2" \
			"test ! -x /tests/vkshare || {rm -f /tmp/vks /tmp/vks.done; /tests/vkshare export /tmp/vks >/tmp/vksexp.log >[2=1] & /tests/vkshare import /tmp/vks >/tmp/vksimp.log >[2=1]; wait; cat /tmp/vksexp.log /tmp/vksimp.log}" \
			"test ! -x /tests/vkspeed || /tests/vkspeed" \
			"test ! -x /tests/vulkaninfo || /tests/vulkaninfo --summary" \
			"test ! -x /tests/vulkaninfo || /tests/vulkaninfo | grep -c 'Surface type *= VK_PLAN9_surface'" \
			"test ! -x /tests/vkcube || {WSI_PLAN9_COPY=1 /tests/vkcube --c 60 >/dev/null >[2=1]; /tests/screenpx 1270,790 640,400}" \
			"test ! -x /tests/vkdraw || {/tests/vkdraw 3 >/tmp/zc >[2=1]; grep frames /tmp/zc | sed 's/^ok/ok zero-copy:/'}" \
			"test ! -x /tests/gltest || /tests/gltest >[2=1]" \
			"test ! -x /tests/gltest || /tests/gltest -s >[2=1] | sed 's/^/surfaceless: /'" \
			"test ! -x /tests/gldesk || /tests/gldesk >[2=1]" \
			"test ! -x /tests/glwin || /tests/glwin 60 >[2=1]" \
			"test ! -x /tests/glage || /tests/glage 300 >[2=1]" \
			"test ! -x /tests/vkdraw || /tests/vkdraw 30 >/dev/null >[2=1] &" \
			"sleep 8" \
			"grep '^scanout' /shr/gpu/0/status" > "$TMPD/console3d" 2>&1
	}
	check "boot with virtio-gpu-gl (virgl, venus), run gpuctxtest" boot3d
	tr -d '\r' < "$TMPD/console3d" > "$TMPD/log3d"
	cp "$TMPD/log3d" "$HERE/console3d.log"
	check "features virgl, blob, context init" grep -q '^features virgl edid blob ctxinit' "$TMPD/log3d"
	check "host visible memory found" grep -q '^hostmem gpu0bar' "$TMPD/log3d"
	while read -r st rest; do
		case $st in
		ok)	pass "3d: $rest" ;;
		FAIL)	fail "3d: $rest" ;;
		esac
	done < <(grep -E '^(ok|FAIL) ' "$TMPD/log3d")
	check "gpuctxtest: all passed" grep -q '^gpuctxtest: all passed' "$TMPD/log3d"
	if [ -x "$HERE/root/tests/futexstress" ]; then
		check "futexstress (Mesa's futex for 9front: a wake reaches its address's waiter, timeouts, a futex mutex): all passed" grep -q '^futexstress: all passed' "$TMPD/log3d"
	fi
	if [ -x "$HERE/root/tests/vktest" ]; then
		check "vktest (Vulkan through venus): all passed" grep -q '^vktest: all passed' "$TMPD/log3d"
		check "vkdraw (VK_PLAN9_surface, swapchain on the screen): all passed" grep -q '^vkdraw: all passed' "$TMPD/log3d"
		check "vktri (shaders, libvulkan's loader): all passed" grep -q '^vktri: all passed' "$TMPD/log3d"
		if [ -x "$HERE/root/tests/rsvk" ]; then
			check "rsvk (vktri in Rust: ash, libvulkan.a): all passed" grep -q '^rsvk: all passed' "$TMPD/log3d"
		else
			skip "rsvk" "not built (no stage-1 rustc of RUST_DIR)"
		fi
		check "vkshare (a GPU buffer exported by one process, imported by another): all passed" \
			bash -c "grep -q '^vkshare export: all passed' '$TMPD/log3d' && grep -q '^vkshare import: all passed' '$TMPD/log3d'"
		check "vkspeed: cached host memory read at 1 GB/s or more" \
			bash -c "grep '^vkspeed: cached streaming read' '$TMPD/log3d' | awk '{ exit !(\$5 >= 1000) }'"
		check "vulkaninfo --summary: the venus device" grep -q 'deviceName *= Virtio-GPU Venus' "$TMPD/log3d"
		check "vulkaninfo: a VK_PLAN9_surface section" bash -c "grep -A1 \"vulkaninfo | grep -c\" '$TMPD/log3d' | sed -n 2p | grep -q '^[1-9]'"
		check "vkcube: clear colour at the edge" grep -q '^1270,790 333333' "$TMPD/log3d"
		check "vkcube: the cube in the middle" bash -c "grep '^640,400 ' '$TMPD/log3d' | grep -vq '333333\$'"
		cp "$TMPD/shot3d.png" "$HERE/screen-vk.png" 2>/dev/null
		check "vkdraw's frames on the display without a copy (ctx scanout)" grep -q '^scanout ctx [0-9]* res [0-9]' "$TMPD/log3d"
		check "display (host side, VNC): vkdraw's red square" grep -q '^200,450 ff0000' "$TMPD/shot3d.txt"
		check "display (host side, VNC): vkdraw's green square" grep -q '^500,450 00ff00' "$TMPD/shot3d.txt"
		check "display (host side, VNC): blue background" bash -c "grep -q '^900,100 000066' '$TMPD/shot3d.txt' && grep -q '^50,700 000066' '$TMPD/shot3d.txt'"
	fi
	if [ -x "$HERE/root/tests/gltest" ]; then
		check "gltest (GLES through zink on venus, a pbuffer): all passed" grep -q '^gltest: all passed' "$TMPD/log3d"
		check "gltest -s (the same on EGL's surfaceless platform): all passed" grep -q '^surfaceless: gltest: all passed' "$TMPD/log3d"
		check "gldesk (desktop GL 4.6: compatibility and core profile): all passed" grep -q '^gldesk: all passed' "$TMPD/log3d"
		check "glwin (EGL's plan9 platform, the frames on the display): all passed" grep -q '^glwin: all passed' "$TMPD/log3d"
		check "glage (buffer age and partial redraw, as WebRender; on the screen): all passed" grep -q '^glage: all passed' "$TMPD/log3d"
		grep -E '^(ages|glage: [0-9])' "$TMPD/log3d" | sed 's/^/  /'
	fi
	check "contexts gone after the test" bash -c "! sed -n '/gpuctxtest: /,\$p' '$TMPD/log3d' | grep -q '^ctx '"

	# 9space: the driver killed while vkcube draws (killdrv.rc): the
	# device reset at its end (gpu(3) onexit), its #g segments gone
	# with it (ORCLOSE), the memory back, a new driver draws; then a
	# reset that never finishes: bus mastering off for good
	if [ -x "$HERE/root/tests/vkcube" ]; then
		cp "$HERE/killdrv.rc" "$HERE/root/tests/killdrv"
		chmod +x "$HERE/root/tests/killdrv"
		bootkill(){
			CPU=host MEM=1024 UFS_EXTRA="$HERE/root" \
			QEMU_EXTRA="-vga none -display egl-headless -device virtio-gpu-gl-pci,blob=true,venus=true,hostmem=256M -object memory-backend-memfd,id=mem1,size=1024M,share=on -machine memory-backend=mem1" \
			"$ROOT/scripts/guest/boot-cli.sh" '/tests/killdrv' > "$TMPD/consolekill" 2>&1
		}
		check "boot with virtio-gpu-gl, kill aux/virtiogpu while vkcube draws" bootkill
		tr -d '\r' < "$TMPD/consolekill" | grep '^killdrv: ' > "$TMPD/logkill"
		sed 's/^/  /' "$TMPD/logkill"
		kv(){ sed -n "s/^killdrv: $1 //p" "$TMPD/logkill" | head -1; }
		check "killdrv: the driver's procs all gone" bash -c "[ -z \"\$(sed -n 's/^killdrv: driver left//p' '$TMPD/logkill' | tr -d ' ')\" ] && grep -q '^killdrv: driver left' '$TMPD/logkill'"
		check "killdrv: vkcube's segments while drawing (more than the driver's queue)" bash -c "[ \"\$(sed -n 's/^killdrv: segs drawing //p' '$TMPD/logkill')\" -gt 1 ]"
		check "killdrv: no gpu0dma/blob/ctx segment left in #g (ORCLOSE)" grep -q '^killdrv: segs after 0$' "$TMPD/logkill"
		check "killdrv: the reset finished (no onexit message, no fault)" bash -c "! grep -q '^killdrv: kmesg: ' '$TMPD/logkill'"
		check "killdrv: the memory back (drawing > before + 2 MB, after <= before + 2 MB)" bash -c "b=\$(sed -n 's/^killdrv: user before //p' '$TMPD/logkill'); a=\$(sed -n 's/^killdrv: user after //p' '$TMPD/logkill'); d=\$(sed -n 's/^killdrv: user drawing //p' '$TMPD/logkill'); echo before \$b drawing \$d after \$a; [ -n \"\$a\" ] && [ \$a -le \$((b + 512)) ] && [ \$d -gt \$((b + 512)) ]"
		check "killdrv: a new driver serves the device and draws" bash -c "grep -q '^killdrv: restart status device virtio-gpu' '$TMPD/logkill' && grep -q '^killdrv: restart drew$' '$TMPD/logkill'"
		check "killdrv: a reset that never finishes: the kernel says so" grep -q '^killdrv: stuck: .*engine did not stop, bus mastering off' "$TMPD/logkill"
		check "killdrv: ... and refuses bme on afterwards" grep -q '^killdrv: bme on refused$' "$TMPD/logkill"
		check "killdrv: nothing hung" grep -q '^killdrv: done$' "$TMPD/logkill"

		# 9space: memory runs out while vkcube draws (drvnoswap.rc):
		# the pager kills memhog's children, never the driver (noswap,
		# gpu(4)); each child is smaller than the driver, so without
		# noswap the driver would go first
		cp "$HERE/drvnoswap.rc" "$HERE/root/tests/drvnoswap"
		chmod +x "$HERE/root/tests/drvnoswap"
		bootnoswap(){
			CPU=host MEM=1024 CMDWAIT=600 UFS_EXTRA="$HERE/root" \
			QEMU_EXTRA="-vga none -display egl-headless -device virtio-gpu-gl-pci,blob=true,venus=true,hostmem=256M -object memory-backend-memfd,id=mem1,size=1024M,share=on -machine memory-backend=mem1" \
			"$ROOT/scripts/guest/boot-cli.sh" '/tests/drvnoswap' > "$TMPD/consolenoswap" 2>&1
		}
		check "boot with virtio-gpu-gl, memory runs out while vkcube draws" bootnoswap
		tr -d '\r' < "$TMPD/consolenoswap" | grep '^drvnoswap: ' > "$TMPD/lognoswap"
		sed 's/^/  /' "$TMPD/lognoswap"
		nv(){ sed -n "s/^drvnoswap: $1 //p" "$TMPD/lognoswap" | head -1; }
		check "drvnoswap: the driver is bigger than memhog's children (so a child killed shows the driver passed over)" bash -c "d=\$(sed -n 's/^drvnoswap: kb driver \\([0-9]*\\) vkcube.*/\\1/p' '$TMPD/lognoswap'); h=\$(sed -n 's/^drvnoswap: child kb //p' '$TMPD/lognoswap'); echo driver \$d child \$h; [ -n \"\$h\" ] && [ \$d -gt \$h ]"
		check "drvnoswap: the pager killed a child of memhog" bash -c "grep -q '^drvnoswap: memhog: [0-9]* children of [0-9]* KB, child [0-9]* gone' '$TMPD/lognoswap' && grep -q '^drvnoswap: killed: memhog ' '$TMPD/lognoswap'"
		check "drvnoswap: ... and no other (memhog's or vkcube's, a client: never the driver)" bash -c "! grep '^drvnoswap: killed: ' '$TMPD/lognoswap' | grep -Evq '^drvnoswap: killed: (memhog|vkcube) '"
		check "drvnoswap: the driver's procs all still there" bash -c "b=\$(sed -n 's/^drvnoswap: driver //p' '$TMPD/lognoswap' | head -1); a=\" \$(sed -n 's/^drvnoswap: driver after //p' '$TMPD/lognoswap') \"; [ -n \"\$b\" ] || exit 1; for p in \$b; do case \"\$a\" in *\" \$p \"*) ;; *) exit 1;; esac; done"
		check "drvnoswap: the driver still serves gpu(4) after it" grep -q '^drvnoswap: serves device ' "$TMPD/lognoswap"
		check "drvnoswap: nothing hung" grep -q '^drvnoswap: done$' "$TMPD/lognoswap"
	fi

	# wsys (sys/src/cmd/wsys): vktri in a window of wsys hands its
	# frames over as GPU buffers (gpu(4) export, wsys' gpubuffer; Mesa's
	# plan9 WSI), then again by copy for comparison (wsysvk.rc)
	if [ -x "$HERE/root/tests/vktri" ] && [ -x "$ROOT/$objtype/bin/wsys" ]; then
		bash "$HERE/putstripped.sh" "$cross_strip" "$ROOT/$objtype/bin/wsys" "$HERE/root/tests/wsys"
		cp "$HERE/wsysvk.rc" "$HERE/root/tests/wsysvk"
		# ui (ui(4), Rust: build.exe's step rust) with its fonts
		if [ -x "$ROOT/$objtype/bin/ui" ]; then
			bash "$HERE/putstripped.sh" "$cross_strip" "$ROOT/$objtype/bin/ui" "$HERE/root/tests/ui"
			mkdir -p "$HERE/root/lib/font/ttf"
			cp -p "$ROOT/lib/font/ttf/"*.ttf "$HERE/root/lib/font/ttf/"
		else
			rm -f "$HERE/root/tests/ui"
		fi
		cp "$HERE/wsysvktri.rc" "$HERE/root/tests/wsysvktri"
		cp "$HERE/wsysovl.rc" "$HERE/root/tests/wsysovl"
		cp "$HERE/wsyscomp.rc" "$HERE/root/tests/wsyscomp"
		cp "$HERE/wsysnoswap.rc" "$HERE/root/tests/wsysnoswap"
		cp "$HERE/wsyssup.rc" "$HERE/root/tests/wsyssup"
		# a hang's kernel stacks (bootwsys): the procs' and the kernel run
		cp "$HERE/kstacks.rc" "$HERE/root/tests/kstacks"
		cmp -s "$ROOT/$KBIN" "$HERE/root/tests/9pc64.elf" || cp "$ROOT/$KBIN" "$HERE/root/tests/9pc64.elf"
		chmod +x "$HERE/root/tests/wsysnoswap" "$HERE/root/tests/wsyssup" "$HERE/root/tests/kstacks"
		chmod +x "$HERE/root/tests/wsysvk" "$HERE/root/tests/wsysvktri" "$HERE/root/tests/wsysovl" "$HERE/root/tests/wsyscomp"
		# in a guest of mcp-guest.sh: wsys in the background there, waited
		# for from here; one not done in 300 s is looked at while it hangs
		# (ps, the logs' last lines, the kernel stacks of the procs waiting
		# in the kernel: kstacks.rc, resolved by scripts/debug/kstack.py),
		# then the guest goes
		bootwsys(){ # [wsys' options and -i script; its log in $TMPD/consolewsys]
			local cli=$CLI_DIR g n ok=0
			local cmd=${1:-/tests/wsys ${WSYSFLAGS:-} -p -x -i /tests/wsysvk}
			local m="python3 $ROOT/scripts/guest/mcp.py"
			CPU=host MEM=1024 UFS_EXTRA="$HERE/root" \
			QEMU_EXTRA="-vga none -display egl-headless -device virtio-gpu-gl-pci,blob=true,venus=true,hostmem=256M -object memory-backend-memfd,id=mem1,size=1024M,share=on -machine memory-backend=mem1" \
				setsid bash "$ROOT/scripts/guest/mcp-guest.sh" 'aux/virtiogpu' > "$cli/gpuwsysguest.log" 2>&1 &
			g=$!
			: > "$TMPD/consolewsys"
			if timeout 660 $m wait 600 >/dev/null; then
				timeout 60 $m rc "bind -a '#i' /dev; bind -a '#m' /dev; rm -f /tmp/wsys.done; {$cmd >/tmp/wsys.log >[2=1]; touch /tmp/wsys.done} &" 50 >/dev/null 2>&1
				for n in $(seq 60); do
					sleep 5
					if timeout 60 $m rc "test -e /tmp/wsys.done" 50 >/dev/null 2>&1; then
						ok=1
						break
					fi
				done
				if [ $ok = 1 ]; then
					timeout 60 $m rc "cat /tmp/wsys.log" 50 > "$TMPD/consolewsys" 2>&1
				else
					{ echo "wsys: not done in 300 s; what runs, the logs:"
					  timeout 60 $m rc "ps; for(f in /tmp/wsys.log /tmp/wvk.log /tmp/wvkcopy.log /tmp/eframe.log /tmp/uivk.log /tmp/wca.log /tmp/wcb.log /tmp/wcc.log) { echo file $f; tail -20 $f }" 50
					  echo "the kernel stacks:"
					  timeout 120 $m rc "/tests/kstacks" 110 > "$TMPD/kstacks" 2>&1
					  python3 "$ROOT/scripts/debug/kstack.py" "$TMPD/kstacks"; } > "$TMPD/consolewsys" 2>&1
				fi
			fi
			kill -TERM -- -$g 2>/dev/null || kill $g 2>/dev/null
			wait $g 2>/dev/null
			[ $ok = 1 ]
		}
		check "boot with virtio-gpu-gl, vktri in a window of wsys" bootwsys
		tr -d '\r' < "$TMPD/consolewsys" > "$TMPD/logwsys"
		cp "$TMPD/logwsys" "$HERE/console-wsys.log"
		check "wsys: the WSI hands vktri's frames to wsys as GPU buffers" grep -q '^gpu: wsi plan9: wsys has the buffers' "$TMPD/logwsys"
		check "wsys: vktri through GPU buffers, its pixels in the window: all passed" grep -q '^gpu: vktri: all passed' "$TMPD/logwsys"
		check "wsys: vktri by copy (WSI_PLAN9_COPY=1): all passed" grep -q '^copy: vktri: all passed' "$TMPD/logwsys"
		check "wsys: wsys gives every buffer back (b n; the WSI took none back after 2 s)" bash -c "! grep -q 'wsys kept buffer' '$TMPD/logwsys'"
		grep -E '^(gpu|copy): ok [0-9]+ frames' "$TMPD/logwsys" | sed 's/^/  /'
		if [ -x "$HERE/root/tests/ui" ]; then
			check "wsys: ui (ui(4)) draws with the GPU (vello on wgpu)" grep -q '^ui: renderer gpu ' "$TMPD/logwsys"
			check "wsys: ui laid out its button after a frame" grep -q '^ui: rect [0-9]' "$TMPD/logwsys"
			check "wsys: ui rendered without errors" bash -c "! grep -q '^ui: log' '$TMPD/logwsys'"
			check "wsys: ui's frames go to wsys as GPU buffers (not copied)" grep -qE '^ui: wsi plan9: [^ ]+: wsys$' "$TMPD/logwsys"
			grep '^ui: renderer' "$TMPD/logwsys" | sed 's/^/  /'
		fi
		for p in sdlvk sdlgpu; do
			[ -x "$HERE/root/tests/$p" ] && check "wsys: $p (SDL3 on Vulkan) in a window: all passed" grep -q "^$p: $p: all passed" "$TMPD/logwsys"
			# the way its frames go: GPU buffers, not the WSI's quiet copy through
			# draw(3) (the same pixels, so only this tells; SDL's renderer asked for
			# more images than the WSI gave wsys and was copied unnoticed)
			[ -x "$HERE/root/tests/$p" ] && check "wsys: $p's frames go to wsys as GPU buffers (not copied)" grep -qE "^$p: wsi plan9: [^ ]+: wsys\$" "$TMPD/logwsys"
		done
		grep -E '^sdlgpu: (ok|FAIL)' "$TMPD/logwsys" | sed 's/^/  /'
		if [ -x "$HERE/root/tests/eframetest" ]; then
			check "wsys: eframetest (egui's eframe) on wgpu's Vulkan, not the processor: all passed" grep -q '^eframe: eframetest: all passed' "$TMPD/logwsys"
			check "wsys: eframe's frames go to wsys as GPU buffers (not copied)" grep -qE '^eframe: wsi plan9: [^ ]+: wsys$' "$TMPD/logwsys"
			grep -E '^eframe: ok (wgpu adapter|[0-9]+ frames)' "$TMPD/logwsys" | sed 's/^/  /'
		fi
		# 9space: memory runs out in a window of wsys (wsysnoswap.rc): the
		# pager kills memhog's children (a window's program), never wsys
		# (noswap, wsys(4)); each child is half wsys' size
		check "boot with virtio-gpu-gl, wsys: memory runs out in a window" bootwsys "/tests/wsys ${WSYSFLAGS:-} -x -i /tests/wsysnoswap"
		tr -d '\r' < "$TMPD/consolewsys" | grep '^noswap: ' > "$TMPD/lognoswapw"
		sed 's/^/  /' "$TMPD/lognoswapw"
		check "wsys noswap: wsys is bigger than memhog's children (so a child killed shows wsys passed over)" bash -c "set -- \$(sed -n 's/^noswap: kb wsys \\([0-9]*\\) child \\([0-9]*\\)$/\\1 \\2/p' '$TMPD/lognoswapw'); [ -n \"\$2\" ] && [ \$1 -gt \$2 ]"
		check "wsys noswap: the pager killed a child of memhog, the program of a window (it did not inherit noswap)" bash -c "grep -q '^noswap: memhog: [0-9]* children of [0-9]* KB, child [0-9]* gone' '$TMPD/lognoswapw' && grep -q '^noswap: killed: memhog ' '$TMPD/lognoswapw'"
		check "wsys noswap: ... and not wsys (its procs all still there)" bash -c "! grep -q '^noswap: killed: wsys ' '$TMPD/lognoswapw' || exit 1; b=\$(sed -n 's/^noswap: wsys \\([0-9 ]*\\)$/\\1/p' '$TMPD/lognoswapw'); a=\" \$(sed -n 's/^noswap: wsys after //p' '$TMPD/lognoswapw') \"; [ -n \"\$b\" ] || exit 1; for p in \$b; do case \"\$a\" in *\" \$p \"*) ;; *) exit 1;; esac; done"
		check "wsys noswap: nothing hung" grep -q '^noswap: done$' "$TMPD/lognoswapw"
		# 9space: wsys keeps its helpers going (wsyssup.rc, wsys(4)): a window
		# -restart's program killed by the pager comes back in the same
		# window; a helper too; a crashing one 5 times, then given up, and
		# restart by hand
		check "boot with virtio-gpu-gl, wsys: its helpers kept going" bootwsys "/tests/wsys ${WSYSFLAGS:-} -x -i /tests/wsyssup"
		tr -d '\r' < "$TMPD/consolewsys" > "$TMPD/logsup"
		grep -E '^(sup: |wsys: .*(killed for memory|times in 10 minutes))' "$TMPD/logsup" | sed 's/^/  /'
		check "wsys sup: a window -restart's program killed by the pager twice, started again each time" bash -c "grep -q '^sup: memhog killed by the pager [2-9]' '$TMPD/logsup' && grep -q 'killed for memory: again in' '$TMPD/logsup'"
		check "wsys sup: its window stayed; deleted, its program not started again" bash -c "grep -q '^sup: window [0-9]* still there' '$TMPD/logsup' && grep -q '^sup: memhog running after the delete: 0' '$TMPD/logsup'"
		check "wsys sup: a helper -restart killed for memory started again (3 runs, then its own end)" grep -q '^sup: helper killed for memory ran 3 times' "$TMPD/logsup"
		check "wsys sup: a crashing helper given up after 5 runs, said so" bash -c "grep -q '^sup: crashing helper ran 5 times' '$TMPD/logsup' && grep -q 'times in 10 minutes: not started again' '$TMPD/logsup'"
		check "wsys sup: restart by wctl starts it once more" grep -q '^sup: after restart it ran 6 times' "$TMPD/logsup"
		check "wsys sup: a key through a helper's kbdtap reaches the window" bash -c "grep -q '^sup: key a in the window' '$TMPD/logsup' && grep -q '^sup: key a through the tap' '$TMPD/logsup'"
		check "wsys sup: while the tap's reader is gone a key still reaches the window (not held back)" bash -c "grep -q '^sup: key b in the window while' '$TMPD/logsup' && grep -q '^sup: key b not through a tap' '$TMPD/logsup'"
		check "wsys sup: after the helper's restart keys go through its tap again" bash -c "grep -q '^sup: key c in the window after' '$TMPD/logsup' && grep -q '^sup: key c through the tap again' '$TMPD/logsup' && grep -q '^sup: tap helper runs 2' '$TMPD/logsup'"
		check "wsys sup: nothing hung" grep -q '^sup: done$' "$TMPD/logsup"
		# the screen composed on the GPU, wsys' default (-g says so; here
		# WSYSDEBUG=comp, so that the default is what runs) (wsyscomp.rc)
		check "boot with virtio-gpu-gl, wsys (the GPU composes by default)" bootwsys "WSI_PLAN9_STATS=1 WSYSDEBUG=comp /tests/wsys ${WSYSFLAGS:-} -x -i /tests/wsyscomp"
		tr -d '\r' < "$TMPD/consolewsys" > "$TMPD/logcomp"
		cp "$TMPD/logcomp" "$HERE/console-wsyscomp.log"
		check "wsys, GPU: composing on the GPU" grep -q '^wsys: composing on the GPU' "$TMPD/logcomp"
		check "wsys, GPU: its frames go to the display (scanout)" grep -q '^wsi plan9: (window): scanout' "$TMPD/logcomp"
		for w in a b c; do
			check "wsys, GPU: vktri $w through GPU buffers, its pixels as composed: all passed" grep -q "^$w: vktri: all passed" "$TMPD/logcomp"
		done
		check "wsys, GPU: the GPU composes the buffers (none refused)" bash -c "! grep -q 'the GPU composes it not\|composes no longer' '$TMPD/logcomp'"
		check "wsys, GPU: wsys gives every buffer back (b n; the WSI took none back after 2 s)" bash -c "! grep -q 'wsys kept buffer' '$TMPD/logcomp'"
		check "wsys, GPU: vktri seen where nothing covers it" grep -q '^uncovered: ff0000' "$TMPD/logcomp"
		check "wsys, GPU: the window over it, not vktri, where it covers it" bash -c "grep '^covered: ' '$TMPD/logcomp' | grep -vq ' \(ff0000\|000066\)\$'"
		grep -E '^(covered|uncovered): |^[abc]: ok [0-9]+ frames' "$TMPD/logcomp" | sed 's/^/  /'
		# a Vulkan window outlives its wsys (Mesa's WSI, libdraw's wsysagain):
		# vktri in wsys A, A killed, its window in B, B killed, in C (wsysagain.rc)
		cp "$HERE/wsysagain.rc" "$HERE/root/tests/wsysagain"
		chmod +x "$HERE/root/tests/wsysagain"
		check "boot with virtio-gpu-gl, vktri in wsys A, B and C" bootwsys "/tests/wsysagain /tmp/wagain.log >/tmp/wagain.out >[2=1]; cat /tmp/wagain.out /tmp/wagain.log"
		tr -d '\r' < "$TMPD/consolewsys" > "$TMPD/logagain"
		cp "$TMPD/logagain" "$HERE/console-wsysagain.log"
		check "wsys gone: vktri goes on in the next wsys, twice, its triangle in the last: all passed" grep -q '^wsysagain: all passed' "$TMPD/logagain"
		grep -E '^(ok|FAIL) ' "$TMPD/logagain" | sed 's/^/  /'
		# the order of the frames on the screen as composed: never an older
		# frame after a newer one (vktri's VKTRI_COUNTER; wsysorder.rc)
		cp "$HERE/wsysorder.rc" "$HERE/root/tests/wsysorder"
		chmod +x "$HERE/root/tests/wsysorder"
		check "boot with virtio-gpu-gl, wsys -g: the frames' order on the screen" bootwsys "WSYSDEBUG=comp,damage /tests/wsys ${WSYSFLAGS:-} -x -i /tests/wsysorder"
		tr -d '\r' < "$TMPD/consolewsys" > "$TMPD/logorder"
		cp "$TMPD/logorder" "$HERE/console-wsysorder.log"
		check "wsys order: every sample a frame of vktri, never an older one after a newer one" grep -Eq '^order: ([5-9][0-9]|[1-9][0-9][0-9]) samples, 0 back, 0 not a frame' "$TMPD/logorder"
		check "glage in a window of wsys -g (buffer age, partial redraw: no older content): all passed" grep -q '^order: glage: all passed' "$TMPD/logorder"
		# incremental present: glage's frames change a stripe of its window
		# (480x360): wsys composes about that, not the window (WSYSDEBUG=damage)
		check "wsys -g composes only what changed (glage's damage: under 40000 pixels a frame)" \
			bash -c "grep -a '^wsys: damage: ' '$TMPD/logorder' | awk '{ if(\$5 + 0 > 1000 && \$5 + 0 < 40000) ok = 1 } END { exit !ok }'"
		grep -a '^wsys: damage: ' "$TMPD/logorder" | tail -3 | sed 's/^/  /'
		grep '^order: ' "$TMPD/logorder" | sed 's/^/  /'
		# the driver killed while wsys composes on the GPU and started again
		# (wsysback.rc, as killdrv.rc): venus reports the device lost (Mesa
		# 0045), wsys composes on the processor meanwhile, on the GPU again
		cp "$HERE/wsysback.rc" "$HERE/root/tests/wsysback"
		chmod +x "$HERE/root/tests/wsysback"
		check "boot with virtio-gpu-gl, wsys while its driver is killed and started again" bootwsys "WSI_PLAN9_STATS=1 WSYSDEBUG=comp /tests/wsys ${WSYSFLAGS:-} -x -i /tests/wsysback"
		tr -d '\r' < "$TMPD/consolewsys" > "$TMPD/logback"
		cp "$TMPD/logback" "$HERE/console-wsysback.log"
		check "wsys back: vktri composed on the GPU before" grep -q '^back: before: vktri: all passed' "$TMPD/logback"
		check "wsys back: the driver killed, wsys lives and the processor composes" grep -q '^back: fell back to the processor' "$TMPD/logback"
		check "wsys back: the driver started again, the GPU composes again" grep -q '^back: the GPU composes again' "$TMPD/logback"
		check "wsys back: vktri after it, its frames GPU buffers wsys composes: all passed" grep -q '^back: after: vktri: all passed' "$TMPD/logback"
		check "wsys back: a name space made before the restart sees the new driver (shr)" grep -q '^back: an old name space sees the new driver' "$TMPD/logback"
		check "wsys back: a vktri across the restart meets the lost device (a call reports it: Mesa 0048) and goes on with a new one" grep -Eq '^back: across: ok device lost at the (acquire|submit|present|fence) ' "$TMPD/logback"
		check "wsys back: that vktri: all passed (its frames on the screen after)" grep -q '^back: across: vktri: all passed' "$TMPD/logback"
		check "wsys back: vktri's new buffers composed by the GPU (none refused after)" bash -c "! sed -n '/the GPU composes again/,\$p' '$TMPD/logback' | grep -q 'the GPU composes it not'"
		grep -E '^back: |^wsys: the GPU compose' "$TMPD/logback" | grep -vE '^back: (before|after): ' | sed 's/^/  /'
	else
		skip "wsys with GPU buffers" "no vktri or no $objtype/bin/wsys"
	fi

	# the last boot: vkcube in a rio window (riovk.rc, riocube.rc), the window
	# resized after 5 s; then the host's display through VNC, and q typed
	# on the serial line, which kbdfs gives rio, rio the window.
	# After rio, rc gets nothing from the serial line: the guest's
	# scripts log to $home/riovk.log, which is on the host (ufsroot).
	if [ -x "$HERE/root/tests/vkcube" ]; then
		cp "$HERE/riovk.rc" "$HERE/root/tests/riovk"
		cp "$HERE/riocube.rc" "$HERE/root/tests/riocube"
		cp "$HERE/riorsvk.rc" "$HERE/root/tests/riorsvk"
		chmod +x "$HERE/root/tests/riovk" "$HERE/root/tests/riocube" "$HERE/root/tests/riorsvk"
		riolog=$CLI_DIR/ufsroot/usr/glenda/riovk.log
		rm -f "$riolog"
		CMDWAIT=5 WAITFOR='never shown' WAITMAX=300 VNC=1 CPU=host MEM=1024 UFS_EXTRA="$HERE/root" \
		QEMU_EXTRA="-vga none -display egl-headless -device virtio-gpu-gl-pci,blob=true,venus=true,hostmem=256M -object memory-backend-memfd,id=mem1,size=1024M,share=on -machine memory-backend=mem1" \
		"$ROOT/scripts/guest/boot-cli.sh" 'aux/virtiogpu' "bind -a '#i' /dev" "bind -a '#m' /dev" \
			'aux/mouse ps2' 'rio -i /tests/riovk' > "$TMPD/consolerio" 2>&1 &
		riopid=$!
		for _ in $(seq 150); do
			grep -q '^riovk: resized' "$riolog" 2>/dev/null && break
			sleep 2
		done
		python3 "$ROOT/scripts/debug/vncshot.py" "$CLI_DIR/vnc.sock" "$TMPD/shotrio.png" \
			150,400 210,400 500,400 850,400 > "$TMPD/shotrio.txt" 2>&1
		cp "$TMPD/shotrio.png" "$HERE/screen-rio.png" 2>/dev/null
		pts=$(grep -o '/dev/pts/[0-9]*' "$CLI_DIR/qemu.log" | head -1)
		[ -n "$pts" ] && printf q > "$pts"
		for _ in $(seq 30); do
			grep -q '^riovk: vkcube exit' "$riolog" 2>/dev/null && break
			sleep 1
		done
		kill $riopid 2>/dev/null
		wait $riopid 2>/dev/null
		cp "$riolog" "$HERE/riovk.log" 2>/dev/null
		if [ -x "$HERE/root/tests/rsvk" ]; then
			check "rio: rsvk (Rust, ash) in a window: all passed" grep -q '^rsvk: all passed' "$riolog"
		fi
		check "rio: vkcube in a window" grep -q '^Selected WSI platform: plan9' "$riolog"
		check "rio: window resized" grep -q '^riovk: resized' "$riolog"
		check "rio (VNC): the cube in the resized window" bash -c "grep -q '^210,400 333333' '$TMPD/shotrio.txt' && grep '^500,400 ' '$TMPD/shotrio.txt' | grep -vq '333333\$'"
		check "rio (VNC): rio's background where the window was" bash -c "grep -q '^150,400 777777' '$TMPD/shotrio.txt' && grep -q '^850,400 777777' '$TMPD/shotrio.txt'"
		check "rio: q quits vkcube (status empty)" grep -qx 'riovk: vkcube exit *' "$riolog"
	fi
	# Mesa 0038: a waiter found a venus ring idle with commands in it and
	# no notify for 50 ms (a lost wake-up, its cause not known): it says so
	check "venus: no lost ring notify" bash -c "! cat '$TMPD'/console* '$TMPD'/log* '${riolog:-/dev/null}' 2>/dev/null | grep 'venus: ring .* notified again'"
else
	skip "3d tests" "no render node or no virtio-gpu-gl-pci in $QEMU"
fi
done_tests
