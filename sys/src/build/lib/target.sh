# sys/src/build/lib/target.sh: sourced by the build steps that compile and
# link code for the machine themselves, not through a manifest
# (gen-initcode-i.sh, gen-rebootcode-i.sh, gen-rebootcode-pc-i.sh,
# gen-apbootstrap-i.sh, gen-bootfs.sh): their code is the system's, so it is
# made by the compiler build.exe makes the rest with.
#
# Source it before the step sets its gcc commands (it keeps build.exe's CC);
# call target_tools after: with TOOLSET=clang (build.exe's: CC is a clang)
# CC becomes that clang with the step's flags (its target by its name on
# Linux, x86_64-unknown-plan9-clang, by $objtype on 9front), LD ld.lld with
# the step's arguments (and --image-base=0), OBJCOPY llvm-objcopy; with gcc
# they stay as the step set them. RT: the compiler's runtime library for the link,
# /$objtype/lib.gcc/$CCRT (libgcc.a, libclang_rt.builtins.a), or empty.
#
# target_binutils (the steps that work on archives and programs: libp9.sh,
# musl-localize.sh, gen-bootfs.sh): AR, NM, RANLIB, STRIP, OBJCOPY of the
# toolset, as build.exe picks its own: with clang LLVM's (llvm-ar, ...);
# with gcc the GNU tools with TOOLPREFIX in front (arm64's default
# aarch64-linux-musl-), AR, NM, RANLIB as the environment names them
# (dist/lib/tree-build.sh: gcc-ar, gcc-nm, gcc-ranlib on arm64).
TARGET_CC=${CC:-}
target_tools(){
	if [ "${TOOLSET:-gcc}" = clang ]; then
		CC="$TARGET_CC${CC#*gcc}"
		# its default image base 0x400000 is above the steps' -Ttext
		LD="ld.lld --image-base=0${LD#*ld}"
		OBJCOPY=llvm-objcopy
	fi
	RT=$ROOT/$objtype/lib.gcc/${CCRT:-libgcc.a}
	[ -f "$RT" ] || RT=
}
target_binutils(){
	if [ "${TOOLSET:-gcc}" = clang ]; then
		AR=llvm-ar NM=llvm-nm RANLIB=llvm-ranlib STRIP=llvm-strip OBJCOPY=llvm-objcopy
	else
		local p
		case $objtype in arm64) p=${TOOLPREFIX-aarch64-linux-musl-} ;; *) p=${TOOLPREFIX-} ;; esac
		AR=$p${AR:-ar} NM=$p${NM:-nm} RANLIB=$p${RANLIB:-ranlib} STRIP=${p}strip OBJCOPY=${p}objcopy
	fi
}
