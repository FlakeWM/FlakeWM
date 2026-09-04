# Third Party Library Vendor Information
## Wlroots
* **Upstream**: https://gitlab.freedesktop.org/wlroots/wlroots
* **Tag**: `0.20.2`
* **Commit ID**: `d783533489e1f75d6886c2ab5c5960090ef268f8`
* **Commit date**: Tue Jul 7 23:46:09 2026 +0200
* **Clone date**: Tue Aug 25 09:34:26 +0800
* **License**: [MIT License](./wlroots/LICENSE)

## Wayland
* **Upstream**: https://gitlab.freedesktop.org/wayland/wayland
* **Tag**: `1.26.0`
* **Commit ID**: `87cc8a8728a923fc57938faa81ba0e74f34ecdc7`
* **Commit date**: Thu Jul 16 17:24:51 2026 +0200
* **Clone date**: Tue Aug 25 12:32:11 +0800
* **License**: [MIT License](./wayland/COPYING)
* **Note**: This library provides `libwayland-server` 1.24+ for Wlroots. GXDE OS 25 (Trixie-based)'s `libwayland-server` is NOT new enough so we have to vendor it.

## Mesa/libdrm
* **Upstream**: https://gitlab.freedesktop.org/mesa/libdrm
* **Tag**: `libdrm-2.4.134`
* **Commit ID**: `e984d448b8b17aab853369e6c203e53719f46de1`
* **Commit date**: Fri May 29 10:49:42 2026 +0200
* **Clone date**: Tue Aug 25 12:55:05 +0800
* **License**: MIT License (Please refer to https://gitlab.freedesktop.org/mesa/libdrm/-/commit/82f74e7a5a7403392e91352af00210ae26a81f19, or the licensing information on each file.)

## Pixman
* **Upstream**: https://gitlab.freedesktop.org/pixman/pixman
* **Tag**: `pixman-0.46.4`
* **Commit ID**: `9cc163c9da0fb4da430641715313d95a6ec466d9`
* **Commit date**: Sun Jul 20 12:14:02 2025 -0400
* **Clone date**: Tue Aug 25 13:02:40 +0800
* **License**: [MIT License](./pixman/COPYING)

## Waylib
* **Upstream**: https://github.com/linuxdeepin/treeland
* **Tag**: N/A, pulling from main
* **Commit ID**: `2031e780f5c770a11843bc81b3cef5271e477dd0`
* **Commit date**: Tue Aug 25 15:24:14 2026 +0800
* **Clone date**: Wed Aug 26 14:58:32 +0800
* **License**: [GPL-3.0-only](https://github.com/linuxdeepin/treeland/blob/master/waylib/.reuse/dep5) for source files. Please refer to [treeland/waylib/.reuse/dep5](https://github.com/linuxdeepin/treeland/blob/master/waylib/.reuse/dep5).
**Notes**: Actually adapted from Deepin's Treeland, not the standalone repository.

## Abseil
* **Upstream**: https://github.com/abseil/abseil-cpp/
* **Tag**: `lts_2026_08_17`
* **Commit ID**: `commit 2065f4ded0558c6f89fee67c8e5228feb4eb960e`
* **Commit date**: Tue Aug 18 08:53:30 2026 -0400
* **Clone date**: Thu Sep 3 12:35:10 -0500
* **License**: [Apache License Version 2.0](./abseil-cpp/LICENSE)
