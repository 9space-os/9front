# scripts/lib/paths.sh: where 9space's other parts are (the toolchains, the
# other worktrees, the archives, the caches).  Sourced by the scripts after
# they set ROOT (the tree): the distribution's paths, by
# dist/lib/9space-paths.sh (dist beside the tree), which exports each
# variable of dist/paths.env the environment does not set already.
[ -f "$ROOT/../dist/lib/9space-paths.sh" ] || { echo "paths.sh: no dist/ beside $ROOT (the tree belongs in 9space beside dist/: dist/bin/repos)" >&2; exit 1; }
. "$ROOT/../dist/lib/9space-paths.sh" || exit 1
