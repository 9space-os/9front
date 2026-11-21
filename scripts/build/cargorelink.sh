# cargorelink rel lib p...: cargo does not know the C libraries it links
# (libvulkan.a, APE's, lib9's: #[link(name = ...)] and -l from the linker's
# search path have no fingerprint): a program p in cargo's release directory
# rel older than one of the archives below lib (the tree's $objtype/lib.gcc)
# goes, release/p and the deps/p-hash it is a link of (cargo looks at that
# one; - in p as _), so that cargo links it again.
# Sourced by scripts/build/cargo.sh and browser/build.sh (through $TREE).
cargorelink(){
	local rel=${1:?} lib=${2:?} p
	shift 2
	for p in "$@"; do
		[ -e "$rel/$p" ] || continue
		[ -n "$(find "$lib" -name '*.a' -newer "$rel/$p" -print -quit 2>/dev/null)" ] || continue
		echo "cargorelink: $p is older than the libraries of $lib: linked again"
		find "$rel/deps" -maxdepth 1 -type f -perm -u+x -regex ".*/${p//-/_}-[0-9a-f]+" -delete
		rm -f "${rel:?}/${p:?}"
	done
}
