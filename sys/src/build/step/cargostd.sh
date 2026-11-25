# cargostd dir target: cargo's target directory dir loses what it built for
# target when the std of toolchain/rust (RUST_DIR in paths.env, the sysroot
# of $RUSTC) is another than then (cargo looks at
# rustc's version only: a std built again by the same rustc leaves crates
# built with the old one, which rustc then refuses, "can't find crate").
# The same for the host: what dir/release holds (build scripts, proc macros
# and their crates, built for the host with --target) goes when the host's
# std is another (a rustc built again: sys/src/build/host/tests/gpu's eframetest kept build
# scripts' crates of an older stage 1, refused once cargo built a build
# script again).
# Sourced by sys/src/build/step/cargo.sh and the tests that run cargo (sys/src/build/host/tests/winit,
# sys/src/build/host/tests/uiserver, sys/src/build/host/tests/gpu); needs $RUSTC.
cargostd(){
	local std sysroot host
	sysroot=$("$RUSTC" --print sysroot)
	std=$(ls -l --time-style=+%s "$sysroot"/lib/rustlib/$2/lib/libstd-*.rlib 2>/dev/null)
	if [ "$(cat "$1/$2.std" 2>/dev/null)" != "$std" ]; then
		rm -rf "${1:?}/$2"
		mkdir -p "$1"
		printf '%s\n' "$std" > "$1/$2.std"
	fi
	host=$("$RUSTC" -vV | sed -n 's/^host: //p')
	std=$(ls -l --time-style=+%s "$sysroot"/lib/rustlib/$host/lib/libstd-*.rlib 2>/dev/null)
	if [ "$(cat "$1/host.std" 2>/dev/null)" != "$std" ]; then
		rm -rf "${1:?}/release"
		mkdir -p "$1"
		printf '%s\n' "$std" > "$1/host.std"
	fi
}
