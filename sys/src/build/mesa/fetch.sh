#!/bin/bash
# Download Mesa's release tarball (sys/src/mesa/VERSION) to $WORK/dl and
# check its sha256 and signature (key: sys/src/mesa/keys/mesa-release.asc,
# in a keyring of its own). Also sets up $WORK/venv (meson, mako, pyyaml,
# packaging, pinned) for Mesa's build.
set -e
. "$(dirname "$0")/common.sh"
mkdir -p "$WORK/dl"
cd "$WORK/dl"
f=$(basename "$MESA_URL")
[ -f "$f" ] || curl -fsSLO "$MESA_URL"
[ -f "$f.sig" ] || curl -fsSLO "$MESA_URL.sig"
echo "$MESA_SHA256  $f" | sha256sum -c -
rm -rf gnupg
mkdir -m 700 gnupg
gpg -q --homedir gnupg --import "$MESA/keys/mesa-release.asc"
gpg --homedir gnupg --status-fd 1 --verify "$f.sig" "$f" 2>/dev/null | grep -q "VALIDSIG $MESA_KEY" ||
	{ echo "fetch: bad signature on $f" >&2; exit 1; }
echo "fetch: $f ok"
# the versions that made sys/src/mesa (meson's output is the tree's): pinned; a
# venv whose meson does not run (WORK moved: its scripts name the old python
# by its path; another python) is made again
PYPKGS="meson==1.12.1 mako==1.4.3 pyyaml==6.0.3 packaging==26.3"
if ! "$VENV/bin/meson" --version >/dev/null 2>&1; then
	rm -rf "$VENV"
	python3 -m venv "$VENV"
	"$VENV/bin/pip" install -q $PYPKGS
fi
