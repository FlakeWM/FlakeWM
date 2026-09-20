# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

FlakeWM is a Wayland compositor built on **wlroots 0.20.2**, targeting the Deepin/GXDE desktop and its DTK2/5/6 applications. It vendors wlroots and Abseil (plus fallback wayland/wayland-protocols/libdrm/pixman/xkbcommon) under `libs/` — see `libs/README.md` for versions. Requires Qt 6.8 (used offscreen to render server-side decorations). Supported protocols are documented in `doc/protocols/protocols.md`.

## Build & run

- Primary build (Ninja): `cmake --build build --target flakewm -j$(nproc)`
  - `flakewm-xwayland` is a separate helper binary (its own target).
  - `build-sanitize/` is an ASan+UBSan Debug build (Makefiles); `build-ukui-sanitize/` is another variant.
- Format: `./format-code` (clang-format, Google base style, C++20). `CPPLINT.cfg` pins 80-column lines.
- Static analysis: `.clang-tidy` (clang-analyzer, bugprone, google, performance, portability; `WarningsAsErrors: '*'`), run via editor/`clang-tidy`, not wired into the build.
- No automated tests exist (`tests/manual/` is empty).

CLI flags (see `src/utils/args_handler/args_handler.cc`): `-n/--nested`, `-d/--debug`, `-w/--warning`, `-e/--error`, `-nx/--noxwayland`, `-s/--session <proc>`.

## Architecture

- **PIMPL core.** `src/core/compositor/compositor.{h,cc}` is a thin public facade (`Compositor` → `unique_ptr<CompositorPrivate>`). The real compositor is `src/core/compositor_private/compositor_private.{h,cc}`: one large `CompositorPrivate` class that owns the wlroots scene graph, all toplevels, outputs, input, protocols, and rendering. Most feature work lands here.
- **Windows.** `CompositorPrivate::Toplevel` (nested struct in `compositor_private.h`) is the window record: its scene-tree node, SSD/CSD state, `Ssd`, `ssd_clip`, `corner_radius`, `mapped`, etc. A CSD window's compositor-drawn shadow lives in the separate `csd_shadow`/`csd_shadow_enabled` pair, not in `Ssd`. Static `Toplevel::On*` handlers (map/unmap/commit/destroy/set_parent/request_fullscreen/…) connect wlroots signals to compositor logic.
- **Protocols.** `src/protocol/protocol_manager/protocol_manager.cc` aggregates per-family managers (gxde, kde, treeland, ukui) and is the entry point for Wayland globals. Deepin/DTK apps speak three protocols, all under `src/protocol/treeland/`: legacy `dde_shell` v2 (DTK2/DTK5), `treeland_personalization_manager_v1` (DTK6), and `treeland_dde_shell_v1`. These are where DTK requests window radius, no-titlebar, and shadow.
- **Rendering.** `src/render/backdrop_blur_renderer.cc` wraps wlroots' render pass to add per-surface rounded-corner clipping and backdrop blur (it matches textures to registered rounded surfaces). The corner mask is applied **at draw time**, which the scene graph does not know about — see "Rounded corners vs. scene visibility" below. SSD chrome (titlebar/shadow/menu/popups) is drawn with Qt Quick **offscreen/software** rendering (QPA setup in `main.cc`) into wlroots scene buffers under `src/view/ssd/`.
- **XWayland & DBus.** `src/xwayland/` is a separate `flakewm-xwayland` helper process. `src/dbus/wlcom/` implements the "wlcom" DBus desktop interface (output, screenshot, view, input, global-accel).
- **Wlroots wrappers.** `src/wlr_wrapper/` holds thin C++ convenience wrappers over raw wlroots headers (`wlroots.h`, `scene.h`, `color.h`, …).

## DTK / CSD specifics (non-obvious, read before touching)

DTK apps are client-side-decorated: they send `dde_shell` `NoTitleBar=1` rather than negotiating xdg-decoration. FlakeWM must therefore:

- Treat a `NoTitleBar` surface as CSD and never attach an SSD titlebar — including when an xdg-decoration arrives afterward and would otherwise default to server-side. See `SetNoTitlebarSurface`/`HasNoTitlebarSurface` + `XdgDecoration::ApplyMode` in `compositor_private.cc`.
- Draw the rounded corners and shadow compositor-side for those CSD windows: `RebuildSurfaceClip` (corner radius via the backdrop-blur renderer) and `SetCsdShadow`/`UpdateCsdShadow` (Chameleon black-radial-gradient shadow via `src/view/ssd/ssd_shadow/ssd_shadow.cc`).

The DTK2/DTK5 platform plugin forwards only `NoTitleBar` and `WindowRadius` (never shadow radius/offset/color), so the shadow is a compositor default with `effectNoShadow` as the sole opt-out.

## Rounded corners vs. scene visibility (the trap)

The corner mask is applied by the renderer at draw time, but the **scene graph still believes the whole surface rect is opaque**. For an opaque buffer (XRGB) `scene_node_opaque_region` returns the full node rect and never consults the mask — it only honors `scene_buffer->opaque_region` for non-opaque buffers. Two consequences, both gated on `wlr_scene->calculate_visibility`:

- `wlr_scene_output_build_state` culls the black background underneath the corner notches.
- `scene_node_update_iterator` subtracts the node's opaque region from `data->visible`, occluding nodes below.

Neither the background nor the window itself paints the notch, so the notch keeps whatever the previous frame left in that swapchain buffer. A window moving right by N px puts its trailing notch over pixels that one frame earlier held *window* content → the corner renders **solid**, while the leading notch lands over pixels that held background and still looks rounded. Symptom: "the 180° corners go square, the others go half-round, and it changes as you drag."

The fix is `RefreshRoundedCornerState()` in `compositor_private.cc`: while `BackdropBlurRenderer::HasRoundedCorners()` is true, `UpdateBackdropBlurState()` clears `scene_->WLR_PRIVATE.calculate_visibility`, so the backdrop survives under the mask. The guard is cached (`scene_rounded_corners_active_`) because `RebuildSurfaceClip` runs on every geometry change and `UpdateBackdropBlurState` damages every output. This is the same lever the blur path already uses, so it is known-safe here — it costs occlusion culling while any rounded window is mapped.

Do **not** try to fix this by setting the scene buffer's opaque region: `surface_reconfigure` (`libs/wlroots/types/scene/surface.c`) copies `surface->opaque_region` over it on every commit, and it is ignored for opaque buffers anyway.

## Nested test workflow

To run a throwaway nested session (isolated D-Bus/XDG/Wayland, presenting into the host compositor):

- `tools/nested-gxde-session.sh` — full session; env: `FLAKEWM_SESSION_CMD` (session process, default `/usr/bin/startdde`), `FLAKEWM_BIN`, `FLAKEWM_NESTED_ROOT` (default `.nested/`).
- `.nested/relaunch.sh` — kill + restart a nested session quickly; `.nested/ns-env.sh` re-exports the nested environment so you can launch apps (`source .nested/ns-env.sh && gxde-file-manager`) and capture the nested output with `grim` (read the `grim` caveat below before trusting what it shows).

Gotchas:

- The nested Wayland socket is `wayland-1` in the **host** `XDG_RUNTIME_DIR` (`/run/user/<uid>/`), not inside `.nested/` — launching an app with `WAYLAND_DISPLAY=wayland-0` drops it into the host compositor instead of the nested one.
- **`grim` cannot show draw-time or stale-buffer artifacts.** It renders into a fresh buffer, and `wlr_damage_ring_rotate_buffer` treats an untracked buffer as fully damaged (`pixman_region32_union_rect(damage, …, 0, 0, buffer->width, buffer->height)`), so the whole output is repainted and anything that depends on leftover buffer contents disappears. To see the real scanout, capture the **host**: `XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 grim /tmp/host.png`.
- `pkill -f <pattern>` matches the bash command line that runs it, so it kills its own shell (exit 144). Use a bracket pattern: `pkill -f 'flakewm .*--neste[d]'`.
- There is no input-injection tool on this box (`ydotool`/`wtype`/`wlrctl` are all absent), so pointer-driven behaviour (drags, grabs) needs a human at the keyboard — don't burn time building an automated repro.

## Vendored libraries (`libs/`) & packaging

`libs/` holds upstream-pristine vendored sources (wlroots, wayland, wayland-protocols, libdrm, pixman, xkbcommon, abseil, waylib) — versions in `libs/README.md`. **Never modify anything under `libs/`**: it is re-importable upstream source, so an edit there is lost on re-import. Fix the build glue in `cmake/` or the compositor instead.

Build glue:
- `cmake/wlroots.cmake` wires up every wlroots-side fallback. For each library (wayland, wayland-protocols, libdrm, pixman, xkbcommon) it probes the system package first (`pkg_check_modules(FLAKEWM_SYSTEM_*)`) and uses it when new enough, else builds the vendored copy with `ExternalProject_Add` + meson into `${CMAKE_BINARY_DIR}/_deps/`. wlroots itself is always built from source here (`WLROOTS_SOURCE_DIR` = `libs/wlroots`), so a patch to `libs/wlroots` *does* take effect — but prefer a compositor-side fix anyway; reach for `libs/` only when the behaviour is genuinely wrong for every consumer.
- `libs/abseil-cpp` is the exception: it is added with `add_subdirectory(libs/abseil-cpp EXCLUDE_FROM_ALL)`, not `ExternalProject_Add`.

Debian packaging (`debian/`):
- `debian/rules` drives the CMake/Ninja build via `dh`; `debian/bundle-vendored.sh` copies any from-source fallback shared libs (wayland, libdrm, pixman, xkbcommon) into a private `usr/lib/flakewm/`, rewrites flakewm's RUNPATH to `$ORIGIN/../lib/flakewm`, and emits `debian/shlibs.local` so `dh_shlibdeps` maps them to flakewm (then dropped via `-xflakewm`). `debian/control` Build-Depends includes `bison`, `meson`, `patchelf` — the vendored xkbcommon/wayland meson builds need them. `./build-deb [-d|-b|-c]` is the local convenience wrapper; the GXDE riscv64 CI runs `dpkg-buildpackage -b` in a trixie chroot.
- `.gitignore` has broad build-artifact rules (`*.map`, `*.so`, `*.a`) that also match some vendored *source* files (xkbcommon's symbol-version scripts are `*.map`, and are required for a versioned libxkbcommon). If a build references a file that "should" be in the checkout but isn't, run `git check-ignore -v <path>` before assuming the file is wrong.
