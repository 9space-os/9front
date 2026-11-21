# Rust crates changed for 9front (a generated mirror)

The crates.io crates 9front changed for its window systems, and our own
`p9win`, as full sources, so that the tree builds them offline, on Linux and
on 9front itself (`build-self.sh`, cargo on 9front). **Generated from
toolchain/rust** (RUST_DIR in paths.env; `patches/crates/<crate>.patch` over
crates.io's release, `plan9/p9win`; its README: "Window system crates") by
`scripts/rust/crates.sh`: **change them there, not here**, then

    scripts/rust/crates.sh        # this mirror again, from toolchain/rust
    scripts/rust/crates.sh -n     # only say what differs (tests/static does)

tests/static fails while this mirror differs from toolchain/rust.

`CRATES` names the crates (name-version); the layout is toolchain/rust's,
`crates/<crate>` and `plan9/p9win`, so the paths between them (winit and
softbuffer depend on p9win) are the same in both.

A crate of the tree uses them by `[patch.crates-io]` with paths here
(`tests/winit/Cargo.toml`) and vendors the rest (`scripts/rust/vendor.sh`).

- `raw-window-handle-0.6.2`: `RawWindowHandle::Plan9`, `RawDisplayHandle::Plan9`.
- `winit-0.30.13`: `src/platform_impl/plan9`, the process's window of wsys or rio.
- `softbuffer-0.4.8`: `src/backends/plan9.rs` (without its default features).
- `wgpu-28.0.0`, `wgpu-core-28.0.1`, `wgpu-hal-28.0.1`: Vulkan on plan9
  (the static libvulkan.a, `VK_PLAN9_surface`); programs link
  `-l draw -l 9 -l fmt -l utf -l stdc++`.
- `imaging_vello-0.0.1`: `VelloRenderer::with_pipeline_cache` (vello's
  `RendererOptions.pipeline_cache`), for ui's pipeline cache.
- `plan9/p9win` (MIT, ours): draw(3), `/dev/mouse`, `/dev/kbd`, `/dev/wctl`,
  `/dev/cursor`, label, wctl, snarf; a window made again in the next wsys
  when its wsys is gone (`wsys.rs`).

## The tree's own crates (not a copy)

Crates of the workspace whose master is this tree; crates.sh copies only
`crates/` and `plan9/p9win` and leaves them alone.

- `plan9/ninep` (MIT, ours): 9P2000 (intro(5)) for the file servers in
  Rust, wsys and ui: the T-messages parsed, the R-messages built, and a
  connection's reads cut into whole messages (`Frames`: a message bigger
  than a pipe keeps whole arrives in more than one read).
  tests/uiserver's uibig9p checks it against ui.
- `desk` (MIT, ours): what the desktop's programs (wsys, ui, term, wbar,
  wdock) share: the theme (wsys(4)'s file `theme`, `/lib/theme/*`), the
  windows of `/dev/wsys`, and with the feature `text` a TrueType font.

## Xilem's crates changed here (not a copy)

`xilem/` holds crates of Xilem's repository (a git dependency of `ui`, its
revision in `../cmd/ui/Cargo.toml`) changed for the tree's builds; the
workspace takes them by `[patch."https://github.com/linebender/xilem"]`.
Their master is this tree (crates.sh leaves `xilem/` alone).

- `masonry_imaging-0.4.0` (Apache-2.0): `vello::set_pipeline_cache`, the
  cache its target renderers' pipelines are made with (ui keeps its data
  across starts, `$home/lib/ui/cache`).
- `masonry_core-0.4.0` (Apache-2.0): damage regions (Xilem's issue 789,
  a TODO in its paint pass): the bounding boxes of the widgets painted
  again and of those moved (where they were and are), all of it when the
  layers change; `RenderRoot::take_damage` gives them to the renderer,
  which draws only that again (`ui`).
- `masonry-0.4.0` (Apache-2.0): ProgressBar's min-content is its label's
  width, not its default 400 pixels (its preferred width): a bar with flex
  in a row no longer keeps the row wider than a narrower window.
- `masonry_imaging-0.4.0` (Apache-2.0): wgpu and imaging_wgpu only with the
  GPU backends (feature `wgpu`, which `imaging_vello`, `imaging_vello_hybrid`
  and `imaging_skia` turn on): `imaging_vello_cpu` alone links no Vulkan, so
  `ui` without its feature `gpu` builds where there is no Mesa (386, arm64).
