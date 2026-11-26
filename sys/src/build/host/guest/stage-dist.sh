#!/bin/bash
# Stage the 9front distribution tree on the build host: what
# sys/lib/dist/mkfile copies onto an install disk (disk/mkfs -s /n/src9
# with distproto and 9bootproto), after its binds:
#
#	binds:V:
#		test -d /n/src9/.git && bind /n/src9 /n/src9/dist/9front
#		bind cfg /n/src9/cfg
#		bind mail/lib /n/src9/mail/lib
#		bind ndb /n/src9/lib/ndb
#		bind -a adm/timezone /n/src9/adm/timezone
#		bind usr /n/src9/usr
#
# The binds become a view of symlinks (dist/9front, the repository
# itself, is left out: the disk gets the system, not a second source
# tree); /sys/src holds the repository's files only, not the gcc build's
# objects in the working tree; sys/src/build/host/guest/boot/gccproto adds /amd64/lib.gcc,
# the gcc build's archives (distproto ships /amd64/lib empty, as 9front
# does: libraries are built with mk on the installed system), and
# sys/src/build/host/guest/boot/gnuproto /amd64/gnu, the native gcc and binutils of
# toolchain/gcc (GCC_DIR in paths.env; its out/$objtype, GCC_OUT, if it
# exists), and sys/src/build/host/guest/boot/rustproto /amd64/rust, Rust of
# toolchain/rust (RUST_DIR; its out/$objtype from scripts/install.sh; only if
# RUST_OUT is set: an optional install, otherwise from its archives on the
# installed system) and sys/src/build/host/guest/boot/goproto /amd64/go, Go of
# toolchain/go (GO_DIR; likewise: its out/$objtype from scripts/install.sh,
# only if GO_OUT is set), and sys/src/build/host/guest/boot/clangproto /amd64/clang,
# clang and lld of toolchain/clang (CLANG_DIR; its out/$objtype from build.sh
# native and runtimes; only if CLANG_OUT is set); uiproto, vkproto, wlproto the ui
# and gpu groups' files;
# sys/src/build/host/guest/proto.py expands the protos. Our
# binaries carry DWARF (8l writes none) and are stripped. The guest's install script
# (sys/src/build/host/guest/boot/mkdisk.rc; mkiso.rc, mkstick.rc) reads the result as /n/src9.
#
# PROFILE (sys/src/build/host/guest/profiles: minimal, gpu, ui, full; default full) leaves the
# files of the groups it does not keep off; DEV (a list of gcc, rust, go, clang, e.g.
# DEV=gcc,rust; DEV=all; DEV= none) says which languages come along: gcc as /$objtype/gnu with
# /$objtype/lib.gcc, Rust from RUST_OUT (default its out/$objtype), Go from GO_OUT,
# clang from CLANG_OUT (likewise); Rust links with gcc, and clang with gcc's
# /$objtype/lib.gcc (APE's libraries), so rust and clang take gcc along. Without DEV,
# as before: gcc if it is built, Rust, Go and clang if RUST_OUT, GO_OUT, CLANG_OUT are set. MCP=1: the MCP server
# (MCP_DIR, tools/mcp) as /$objtype/bin/9front-mcp, built with 9space's Go. BROWSER=1:
# the browser (BROWSER_DIR: its out/$objtype, ./build.sh there) as /$objtype/bin/servoshell
# and webfs-rs, /rc/bin/browser (browser(1)) and /sys/lib/browser (its licences, the MPL's
# source offer); it needs the ui and gpu groups (PROFILE full: wsys and the fonts, a GPU
# driver for Servo's drawing).
# /lib/firmware: linux-firmware's WLAN and GPU firmware (sys/src/build/host/guest/firmware.spec,
# checked against firmware.list; the GPUs' in the gpu group); FIRMWARE=0: none
# (dist/bin/mkfirmware makes an archive of the firmware alone).
#
# REPLICA_DIR (with REPLICA_KEY, REPLICA_URL): the stage is published there for
# pull(1), updates over HTTP (sys/src/build/host/replica/publish.py; REPLICA_DIR is what a web
# server serves as REPLICA_URL, REPLICA_KEY the private key that signs its index,
# kept outside 9space), and the CD gets the client: /dist/replica (the URL, the
# public key, the replica's db and log as of this CD). dir.list: each path's
# mode and owners as the CD has them (sys/src/build/host/guest/proto.py -l).
#
# usage: [objtype=386] [PROFILE=p] [DEV=list] [MCP=1] [BROWSER=1] [FIRMWARE=0] [REPLICA_DIR=d REPLICA_KEY=k REPLICA_URL=u]
#        sys/src/build/host/guest/stage-dist.sh dir
set -e
ROOT=${PLAN9:-$(git -C "$(dirname "$0")" rev-parse --show-toplevel)}
. "$ROOT/sys/src/build/lib/paths.sh"	# GCC_DIR, RUST_DIR, GO_DIR, CLANG_DIR, MCP_DIR, DISTFILES (the distribution's paths.env)
OUT=${1:?usage: stage-dist.sh dir}
DIST=$ROOT/sys/lib/dist
VIEW=$(mktemp -d)
trap 'rm -rf "$VIEW"' EXIT

# view: every top-level entry of the tree, with the bound ones replaced
for f in "$ROOT"/*; do
	ln -s "$f" "$VIEW/$(basename "$f")"
done
rm -f "$VIEW/cfg" "$VIEW/usr" "$VIEW/mail" "$VIEW/lib" "$VIEW/adm"
ln -s "$DIST/cfg" "$VIEW/cfg"
ln -s "$DIST/usr" "$VIEW/usr"
mkdir "$VIEW/mail" "$VIEW/lib" "$VIEW/adm" "$VIEW/adm/timezone"
for f in "$ROOT"/mail/*; do [ -e "$f" ] && ln -s "$f" "$VIEW/mail/$(basename "$f")"; done
rm -f "$VIEW/mail/lib"; ln -s "$DIST/mail/lib" "$VIEW/mail/lib"
for f in "$ROOT"/lib/*; do ln -s "$f" "$VIEW/lib/$(basename "$f")"; done
rm -f "$VIEW/lib/ndb"; ln -s "$DIST/ndb" "$VIEW/lib/ndb"
for f in "$ROOT"/adm/*; do [ "$(basename "$f")" = timezone ] || ln -s "$f" "$VIEW/adm/$(basename "$f")"; done
# bind -a: the tree's zones first, then dist's
for f in "$DIST"/adm/timezone/* "$ROOT"/adm/timezone/*; do
	ln -sfn "$f" "$VIEW/adm/timezone/$(basename "$f")"
done
# /sys/src: the sources as the repository has them -- the working tree
# also holds the gcc build's objects, archives and generated files --
# without sys/src/build/host (the host's scripts, tests and log: Linux
# only, not what a build on the installed system needs)
rm "$VIEW/sys"
mkdir "$VIEW/sys"
for f in "$ROOT"/sys/*; do
	[ "$(basename "$f")" = src ] || ln -s "$f" "$VIEW/sys/$(basename "$f")"
done
git -C "$ROOT" ls-files -z sys/src ':!sys/src/build/host' | (cd "$VIEW" && xargs -0 -n 500 sh -c '
	for f; do mkdir -p "$(dirname "$f")"; ln -s "'"$ROOT"'/$f" "$f"; done' sh)
# generated files a 9front release tree has: libc/ucd's tables, which mk
# would make with a helper linked against a Plan 9 libc.a (the native
# /amd64/lib; the gcc build's archives are in /amd64/lib.gcc)
for f in "$ROOT"/sys/src/libc/ucd/rune*data; do
	ln -s "$f" "$VIEW/sys/src/libc/ucd/$(basename "$f")"
done

# the kernel where 9front installs it: amd64/9pc64 (386/9pc), the a.out
# the build's install step makes (sys/src/build/step/elf2aout.py)
A=${objtype:-amd64}
case $A in amd64) K=9pc64 ;; 386) K=9pc ;; *) echo "stage-dist: objtype=$A unknown" >&2; exit 1 ;; esac
# the profile: the groups it keeps, the paths of the others (omit)
PROFILE=${PROFILE:-full}
PROFILES=$ROOT/sys/src/build/host/guest/profiles
keep=$(awk -v p="$PROFILE" '$1 == "profile" && $2 == p { f = 1; for(i = 3; i <= NF; i++) print $i } END { exit !f }' "$PROFILES") ||
	{ echo "stage-dist: no profile $PROFILE (sys/src/build/host/guest/profiles: $(awk '$1 == "profile" { printf " %s", $2 }' "$PROFILES"))" >&2; exit 1; }
has(){ case " "$keep" " in *" $1 "*) return 0;; esac; return 1; }
keep=$(echo $keep)
mapfile -t omit < <(awk -v keep=" $keep " '/^[a-z]/ && $1 != "profile" && index(keep, " " $1 " ") == 0 { print $2 }' "$PROFILES" | sed "s/\$objtype/$A/")
# the languages: DEV given, exactly those; else as before
if [ -n "${DEV+set}" ]; then
	dev=" $(echo "$DEV" | tr , ' ') "
	for l in $dev; do case $l in gcc|rust|go|clang) ;; all) dev=" gcc rust go clang " ;; *) echo "stage-dist: DEV: no language $l (gcc, rust, go, clang)" >&2; exit 1 ;; esac; done
	for l in rust clang; do
		case $dev in *" $l "*) case $dev in *" gcc "*) ;; *) dev="$dev gcc "; echo "stage-dist: $l links with gcc's files, gcc comes along" >&2 ;; esac ;; esac
	done
	case $dev in *" rust "*) RUST_OUT=${RUST_OUT:-$RUST_DIR/out/$A} ;; *) RUST_OUT= ;; esac
	case $dev in *" go "*) GO_OUT=${GO_OUT:-$GO_DIR/out/$A} ;; *) GO_OUT= ;; esac
	case $dev in *" clang "*) CLANG_OUT=${CLANG_OUT:-$CLANG_DIR/out/$A} ;; *) CLANG_OUT= ;; esac
fi
lang(){ [ -z "${DEV+set}" ] || case $dev in *" $1 "*) return 0;; *) return 1;; esac; }
langs="as built"
[ -z "${DEV+set}" ] || { langs=$(echo $dev); langs=${langs:-none}; }
echo "stage-dist: profile $PROFILE (groups: ${keep:-none}), languages: $langs" >&2
[ -s "$ROOT/$A/$K" ] || { echo "stage-dist: no $A/$K, run CC=gcc ARCH=$A sys/src/cmd/build/build.exe" >&2; exit 1; }
mkdir -p "$VIEW/.$A"
for f in "$ROOT"/$A/*; do ln -s "$f" "$VIEW/.$A/$(basename "$f")"; done
rm "$VIEW/$A"; mv "$VIEW/.$A" "$VIEW/$A"
# the native toolchain (GCC_DIR: build.sh native) as /$objtype/gnu
GCC_OUT=${GCC_OUT:-$GCC_DIR/out/$A}
# apefresh FILE HOW: a native program linked over APE statically, its
# objects compiled before a change of APE's headers, may be linked again
# with the new libap: old struct layouts against new code (2026-11-26:
# toolchain/gcc's ld with the old struct stat crashed when it overwrote a
# file). No CD or update when FILE is older than APE's headers; HOW names
# the rebuild. gcc and binutils (build.sh native relinks with the tree's
# libap); clang and lld (likewise). Not Rust's rustc: its APE is its own
# renamed copy (toolchain/rust's ape9), compiled and linked with it
apefresh(){
	local stale
	stale=$(find "$ROOT/sys/include/ape" "$ROOT/$A/include/ape" -type f -newer "$1" -print -quit 2>/dev/null)
	[ -z "$stale" ] || { echo "stage-dist: $1 is older than APE's ${stale#$ROOT/}: build it again ($2)" >&2; exit 1; }
}
# gccproto: /$objtype/lib.gcc, gcc's archives (with gcc); uiproto: the desktop's files
# of the main branch (TrueType fonts, themes, wbar's menu; the ui group)
PROTOS=()
lang gcc && PROTOS+=("$ROOT/sys/src/build/host/guest/boot/gccproto") || omit+=("$A/lib.gcc")
has ui && PROTOS+=("$ROOT/sys/src/build/host/guest/boot/uiproto")
# gccmk (toolchain/gcc build.sh plan9: its wrappers from GCC_DIR/plan9, its flags
# from the tree's manifests) made anew when one of those is newer than what
# it made: a commit to them alone changes nothing in GCC_OUT
gccmk_stale(){
	local stamp=$GCC_OUT/$A/gnu/plan9/lib.rc
	[ -e "$stamp" ] || return 0
	[ -n "$(find "$GCC_DIR/plan9" "$ROOT/$A/cflags.json" "$ROOT/$A/gcc.json" "$ROOT/$A/ape.json" \
		"$ROOT/sys/src/cmd/cmds.json" "$ROOT/sys/src/ape/cmd/cmds.json" "$ROOT"/sys/src/9/*/build.json \
		-newer "$stamp" -print -quit 2>/dev/null)" ]
}
if lang gcc && [ -d "$GCC_OUT/$A/gnu" ] && [ "$GCC_OUT" = "$GCC_DIR/out/$A" ] && gccmk_stale; then
	echo "stage-dist: gccmk older than toolchain/gcc/plan9 or the tree's flags: build.sh plan9" >&2
	(cd "$GCC_DIR" && objtype=$A PLAN9=$ROOT ./build.sh plan9) >&2
fi
if ! lang gcc; then
	:
elif [ -d "$GCC_OUT/$A/gnu" ]; then
	# its ld is the oldest part (binutils first)
	apefresh "$GCC_OUT/$A/gnu/bin/ld" "toolchain/gcc: objtype=$A ./build.sh native"
	ln -s "$GCC_OUT/$A/gnu" "$VIEW/$A/gnu"
	PROTOS+=("$ROOT/sys/src/build/host/guest/boot/gnuproto")
elif [ -n "${DEV+set}" ]; then
	echo "stage-dist: no $GCC_OUT/$A/gnu (DEV has gcc): build it, toolchain/gcc build.sh native" >&2; exit 1
else
	echo "stage-dist: no $GCC_OUT/$A/gnu, the distribution has no gcc" >&2
fi
# Rust only if asked (RUST_OUT=$RUST_DIR/out/$objtype, its scripts/install.sh): an
# optional install, else from its archives (rust-T.tgz, rust-tools-T.tgz) on the installed
# system; as /$objtype/rust, linking with the gcc above
if [ -n "$RUST_OUT" ]; then
	[ -d "$RUST_OUT/$A/rust" ] || { echo "stage-dist: no $RUST_OUT/$A/rust" >&2; exit 1; }
	ln -s "$RUST_OUT/$A/rust" "$VIEW/$A/rust"
	PROTOS+=("$ROOT/sys/src/build/host/guest/boot/rustproto")
fi
# Go only if asked (GO_OUT=$GO_DIR/out/$objtype, its scripts/install.sh): an optional
# install, else from its archive (go-plan9-$objtype.tgz) on the installed system; as
# /$objtype/go. Its programs are a.out; the strip below leaves the tree alone
if [ -n "$GO_OUT" ]; then
	[ -d "$GO_OUT/$A/go" ] || { echo "stage-dist: no $GO_OUT/$A/go" >&2; exit 1; }
	ln -s "$GO_OUT/$A/go" "$VIEW/$A/go"
	PROTOS+=("$ROOT/sys/src/build/host/guest/boot/goproto")
fi
# clang only if asked (CLANG_OUT=$CLANG_DIR/out/$objtype: build.sh native and
# runtimes there), as /$objtype/clang: bin (clang, lld and their rc scripts), lib
# (libc++, libc++abi, libunwind, compiler-rt in lib/clang/N) and include (libc++'s
# headers). The path is fixed: clang's resource directory is compiled in
# (/$cputype/clang/lib/clang/N). Without lib/libc++.modules.json: it names a share/
# that does not come along.
# The other machines (CLANG_OBJTYPES, default 386 and arm64 but this one): clang
# builds their programs with objtype= (each native clang has every backend), so
# each gets /X/clang/{lib,include} (CLANG_DIR/out/X: build.sh runtimes there),
# /X/plan9.ld and /X/lib.gcc from the tree's build of X (/X/include is
# distproto's): its archives without the gpu group's (Mesa's libvulkan.a,
# the LinuxKPI's: not for other machines' programs) and without the kernel's
# (klib*.a). A machine without its runtimes or its build is said and left out
clanglib(){	# dir to: its lib/ but libc++.modules.json, as links
	mkdir -p "$2"
	for f in "$1"/*; do
		case $f in *.modules.json) ;; *) ln -s "$f" "$2/$(basename "$f")" ;; esac
	done
}
if [ -n "$CLANG_OUT" ]; then
	C=$CLANG_OUT/$A/clang
	[ -x "$C/bin/clang" ] || { echo "stage-dist: no $C/bin/clang (toolchain/clang: objtype=$A ./build.sh native)" >&2; exit 1; }
	[ -f "$C/lib/libc++.a" ] && [ -d "$C/include" ] || { echo "stage-dist: no $C/lib or include (toolchain/clang: objtype=$A ./build.sh runtimes)" >&2; exit 1; }
	apefresh "$C/bin/clang" "toolchain/clang: objtype=$A ./build.sh native"
	apefresh "$C/lib/libc++.a" "toolchain/clang: objtype=$A ./build.sh runtimes"
	mkdir -p "$VIEW/$A/clang"
	ln -s "$C/bin" "$VIEW/$A/clang/bin"
	ln -s "$C/include" "$VIEW/$A/clang/include"
	clanglib "$C/lib" "$VIEW/$A/clang/lib"
	PROTOS+=("$ROOT/sys/src/build/host/guest/boot/clangproto")
	gpulibs=$(echo $(awk '$1 == "gpu" && $2 ~ /^\$objtype\/lib.gcc\// { sub(/^\$objtype\/lib.gcc\//, "", $2); print $2 }' "$PROFILES"))
	others=
	for X in ${CLANG_OBJTYPES-386 arm64}; do
		[ "$X" != "$A" ] || continue
		R=$CLANG_DIR/out/$X/$X/clang
		[ -f "$R/lib/libc++.a" ] || { echo "stage-dist: clang for $X left out: no $R/lib (toolchain/clang: objtype=$X ./build.sh runtimes)" >&2; continue; }
		[ -f "$ROOT/$X/lib.gcc/libc.a" ] && [ -f "$ROOT/$X/plan9.ld" ] || { echo "stage-dist: clang for $X left out: no $X/lib.gcc or plan9.ld (CC=gcc ARCH=$X build.exe)" >&2; continue; }
		apefresh "$R/lib/libc++.a" "toolchain/clang: objtype=$X ./build.sh runtimes"
		rm "$VIEW/$X"
		mkdir -p "$VIEW/$X/clang" "$VIEW/$X/lib.gcc/ape"
		for f in "$ROOT"/$X/*; do
			case ${f##*/} in lib.gcc) ;; *) ln -s "$f" "$VIEW/$X/${f##*/}" ;; esac
		done
		for f in "$ROOT"/$X/lib.gcc/*.a "$ROOT"/$X/lib.gcc/ape/*.a; do
			l=${f#$ROOT/$X/lib.gcc/}
			case " "$gpulibs" " in *" $l "*) continue ;; esac
			case $l in klib*) continue ;; esac
			ln -s "$f" "$VIEW/$X/lib.gcc/$l"
		done
		ln -s "$R/include" "$VIEW/$X/clang/include"
		clanglib "$R/lib" "$VIEW/$X/clang/lib"
		printf '%s\td775\n\tplan9.ld\n\tlib.gcc\td775\n\t\t+\n\tclang\td775\n\t\t+\n' $X > "$VIEW/clangproto.$X"
		PROTOS+=("$VIEW/clangproto.$X")
		others="$others $X"
	done
	echo "stage-dist: clang for $A${others:+, and objtype=${others# }}" >&2
fi
# the Vulkan programs sys/src/build/host/tests/gpu/build-vktest.sh built (vulkaninfo, vkcube,
# vktri; statically linked with Mesa's drivers) and drawspeed as /$objtype/vk
VK=$ROOT/sys/src/build/host/tests/gpu/root/tests
if has gpu && [ -x "$VK/vulkaninfo" ]; then
	mkdir -p "$VIEW/$A/vk"
	for p in vulkaninfo vkcube vktri drawspeed; do
		[ -x "$VK/$p" ] && ln -s "$VK/$p" "$VIEW/$A/vk/$p"
	done
	PROTOS+=("$ROOT/sys/src/build/host/guest/boot/vkproto")
fi
# sys/src/build/host/tests/wayland's programs and scripts (its run.sh built them: wlsys,
# weston's examples, the Wayland tests) as /tests, for rc/bin/wlrun on a
# machine booted from the CD
WLT=$ROOT/sys/src/build/host/tests/wayland/root/tests
if has ui && [ -x "$WLT/wlsys" ]; then
	rm -f "$VIEW/tests"
	mkdir "$VIEW/tests"
	for p in "$WLT"/*; do
		ln -s "$p" "$VIEW/tests/$(basename "$p")"
	done
	# the system's wsys, not the copy of the last sys/src/build/host/tests/wayland run; uipeek
	# (sys/src/build/host/tests/ui) for rc/bin/deskrun
	[ -x "$ROOT/$A/bin/wsys" ] && ln -sf "$ROOT/$A/bin/wsys" "$VIEW/tests/wsys"
	[ -x "$ROOT/sys/src/build/host/tests/ui/root/tests/uipeek" ] && ln -s "$ROOT/sys/src/build/host/tests/ui/root/tests/uipeek" "$VIEW/tests/uipeek"
	PROTOS+=("$ROOT/sys/src/build/host/guest/boot/wlproto")
fi

rm -rf "$OUT"
mkdir -p "$OUT"
export objtype=$A
python3 "$ROOT/sys/src/build/host/guest/proto.py" -s "$VIEW" -d "$OUT" -l "$OUT.list" \
	-e tmp -e lib/firmware -e n -e mnt \
	"$ROOT/sys/lib/sysconfig/proto/distproto" "$ROOT/sys/lib/sysconfig/proto/9bootproto" \
	"${PROTOS[@]}" \
	2> "$OUT.warnings" || { cat "$OUT.warnings"; exit 1; }
# the firmware (sys/src/build/host/guest/firmware.spec, firmware.list): linux-firmware's
# files under the names the drivers read, each checked against its sha256
# (stage-firmware.py: FWDIR, the host's /usr/lib/firmware by default), with
# their licences; FIRMWARE=0: none, as 9front's CD (dist/bin/mkfirmware makes an
# archive of the firmware alone). The GPUs' are in the profiles' gpu group
if [ "${FIRMWARE:-1}" = 1 ]; then
	python3 "$ROOT/sys/src/build/host/guest/stage-firmware.py" "$OUT/lib/firmware" || exit 1
	(cd "$OUT" && find lib/firmware -type d -printf '%p\t20000000775\tsys\tsys\n' \
		-o -type f -printf '%p\t664\tsys\tsys\n') >> "$OUT.list"
	LC_ALL=C sort -u -o "$OUT.list" "$OUT.list"
fi
# the profile's omitted groups (sys/src/build/host/guest/profiles), for check-install -x too
: > "$OUT.omitted"
for p in "${omit[@]}"; do
	echo "/$p" >> "$OUT.omitted"
	rm -rf "${OUT:?}/$p"
done
# and on the CD: what its profile left out (sys/src/build/host/tests/iso checks a CD against
# it, whatever profile made it)
mkdir -p "$OUT/sys/lib/dist"
cp "$OUT.omitted" "$OUT/sys/lib/dist/omitted"
# the MCP server (MCP=1; MCP_DIR, tools/mcp): an agent on another machine works on the
# installed system over HTTP (tools/mcp/README.md). Built here with 9space's Go (GO_DIR)
# for objtype, with its example deny list as /sys/lib/mcp/deny (its README's -deny)
if [ "$MCP" = 1 ]; then
	go=${GO:-$GO_DIR/go/bin/go}
	[ -x "$go" ] || { echo "stage-dist: MCP=1: no Go ($go: toolchain/go sys/src/build/step.sh)" >&2; exit 1; }
	[ -f "$MCP_DIR/main.go" ] || { echo "stage-dist: MCP=1: no MCP server in $MCP_DIR (dist/bin/repos mcp)" >&2; exit 1; }
	# -s -w: without Go's symbol table and DWARF (Go optimizes always; the
	# strip below takes ELF only, Go's plan9 programs are a.out)
	(cd "$MCP_DIR" && GOOS=plan9 GOARCH=$A CGO_ENABLED=0 "$go" build -trimpath "-ldflags=-s -w" -o "$OUT/$A/bin/9front-mcp" .)
	mkdir -p "$OUT/sys/lib/mcp"
	install -m 644 "$MCP_DIR/deny.example" "$OUT/sys/lib/mcp/deny"
	# made after proto.py: dir.list (the replica, the CD's modes) learns them here
	printf '%s\t775\tsys\tsys\n' "$A/bin/9front-mcp" >> "$OUT.list"
	printf 'sys/lib/mcp\t20000000775\tsys\tsys\nsys/lib/mcp/deny\t664\tsys\tsys\n' >> "$OUT.list"
	LC_ALL=C sort -u -o "$OUT.list" "$OUT.list"
	echo "stage-dist: the MCP server as /$A/bin/9front-mcp" >&2
fi
# the browser (BROWSER=1; BROWSER_DIR, Servo for 9space): its scripts/cd-files.sh puts its
# programs, /rc/bin/browser and /sys/lib/browser into the stage and names them; servoshell
# runs in wsys with the ui group's fonts (lib/font/ttf, lib/fontconfig) and draws through
# Mesa on a GPU driver of the gpu group (gpustart, aux/i915, amdgpu, virtiogpu)
if [ "${BROWSER:-0}" = 1 ]; then
	has ui && has gpu || { echo "stage-dist: BROWSER=1 needs the groups ui and gpu (PROFILE full): servoshell runs in wsys, with its fonts, on a GPU driver" >&2; exit 1; }
	bash "$BROWSER_DIR/scripts/cd-files.sh" "$OUT" "$A" > "$OUT.browser" || exit 1
	{
		echo "sys/lib/browser	20000000775	sys	sys"
		sed 's#^/##' "$OUT.browser" | while read -r p; do
			case $p in */bin/*) m=775 ;; *) m=664 ;; esac
			printf '%s\t%s\tsys\tsys\n' "$p" "$m"
		done
	} >> "$OUT.list"
	LC_ALL=C sort -u -o "$OUT.list" "$OUT.list"
	echo "stage-dist: the browser: $(tr '\n' ' ' < "$OUT.browser")" >&2
fi
# the expected files of the tree's build (check-install.py -l) for
# check-install.rc on the installed system, which builds itself (the
# mkfile parser is python's): its build's step install checks against it
objtype=$A python3 "$ROOT/sys/src/build/step/check-install.py" -l > "$OUT/sys/src/build/check-install.$A"
printf 'sys/src/build/check-install.%s\t664\tsys\tsys\n' "$A" >> "$OUT.list"
LC_ALL=C sort -u -o "$OUT.list" "$OUT.list"
# the CA roots, with Rust, Go or the browser: /sys/lib/tls/ca.pem, where Go's crypto/x509
# and our crates (rustls-native-certs, curl-sys; webfs-rs' rustls) look; cargo and go reach
# crates.io and the module proxy over https with them, the browser the web. Mozilla's
# list as curl extracts it (MPL-2.0):
# like firmware a file of its own, not in the tree (DISTFILES, dist/distfiles.list)
if [ -n "$RUST_OUT" ] || [ -n "$GO_OUT" ] || [ "${BROWSER:-0}" = 1 ]; then
	ca=$(ls "$DISTFILES"/cacert-*.pem 2>/dev/null | sort | tail -1)
	if [ -n "$ca" ]; then
		mkdir -p "$OUT/sys/lib/tls"
		install -m 644 "$ca" "$OUT/sys/lib/tls/ca.pem"
		# made after proto.py: the list (the update archives, the replica) learns it here
		printf 'sys/lib/tls\t20000000775\tsys\tsys\nsys/lib/tls/ca.pem\t664\tsys\tsys\n' >> "$OUT.list"
		LC_ALL=C sort -u -o "$OUT.list" "$OUT.list"
		echo "stage-dist: CA roots $(basename "$ca") as /sys/lib/tls/ca.pem" >&2
	else
		echo "stage-dist: no CA roots ($DISTFILES/cacert-*.pem, dist/bin/fetch): cargo and go cannot reach https" >&2
	fi
fi
# the extra protos in one file, for the guest's disk/mkfs and disk/mk9660
# (sys/src/build/host/guest/boot/mkdisk.rc, mkiso.rc, mkstick.rc; REPLICA_DIR adds
# replicaproto below); no proto lists it, so it stays off the disk
cat /dev/null "${PROTOS[@]}" > "$OUT/gccproto"
# not in /$objtype/go: its programs are a.out, its ELF files are test data of Go's std
# (debug/elf, runtime/pprof), which strip would change
find "$OUT" -path "$OUT/$A/go" -prune -o -type f -perm -u+x -exec sh -c 'file -b "$1" | grep -q ^ELF && strip "$1"' _ {} \;
# Mesa's libvulkan.a and the Linux code's archives (sys/src/linux:
# libamdgpu.a 340 MB, liblinux.a) without their debug information: with
# it disk/mk9660 fails, and the CD outgrows mkiso.sh's scratch disk
for f in "$OUT"/$A/lib.gcc/ape/libvulkan.a "$OUT"/$A/lib.gcc/libamdgpu.a "$OUT"/$A/lib.gcc/liblinux.a "$OUT"/$A/lib.gcc/libkpi*.a; do
	[ -f "$f" ] && strip -g "$f"
done
# the replica, last: what it publishes is the CD's tree as it is now
if [ -n "$REPLICA_DIR" ]; then
	[ -n "$REPLICA_KEY" ] && [ -n "$REPLICA_URL" ] || { echo "stage-dist: REPLICA_DIR needs REPLICA_KEY and REPLICA_URL" >&2; exit 1; }
	python3 "$ROOT/sys/src/build/host/replica/publish.py" -k "$REPLICA_KEY" -c "$REPLICA_URL" "$OUT" "$OUT.list" "$REPLICA_DIR"
	cat "$ROOT/sys/src/build/host/guest/boot/replicaproto" >> "$OUT/gccproto"
	echo "stage-dist: published in $REPLICA_DIR for $REPLICA_URL (pull(1))" >&2
fi
echo "$(grep -c . "$OUT.warnings") proto warnings ($OUT.warnings)"
# everything mk install installs made it into the distribution
python3 "$ROOT/sys/src/build/step/check-install.py" -d "$OUT" -x "$OUT.omitted" | grep -v '^broken' | tail -20
[ "${PIPESTATUS[0]}" = 0 ] || { echo "stage-dist: distribution incomplete" >&2; exit 1; }
du -sh "$OUT"
