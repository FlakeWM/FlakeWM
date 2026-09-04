# 集成的第三方库信息
## Wlroots
* **上游**: https://gitlab.freedesktop.org/wlroots/wlroots
* **标签**: `0.20.2`
* **提交ID**: `d783533489e1f75d6886c2ab5c5960090ef268f8`
* **提交日期**: Tue Jul 7 23:46:09 2026 +0200
* **拉取日期**: Tue Aug 25 09:34:26 +0800
* **许可证**: [MIT License](./wlroots/LICENSE)

## Wayland
* **上游**: https://gitlab.freedesktop.org/wayland/wayland
* **标签**: `1.26.0`
* **提交ID**: `87cc8a8728a923fc57938faa81ba0e74f34ecdc7`
* **提交日期**: Thu Jul 16 17:24:51 2026 +0200
* **拉取日期**: Tue Aug 25 12:32:11 +0800
* **许可证**: [MIT License](./wayland/COPYING)
* **备注**: 此库提供`libwayland-server`的1.24+版本供Wlroots使用。GXDE OS 25（基于trixie）系统里的`libwayland-server`依旧不够新，于是我们集成了这个库。

## Mesa/libdrm
* **上游**: https://gitlab.freedesktop.org/mesa/libdrm
* **标签**: `libdrm-2.4.134`
* **提交ID**: `e984d448b8b17aab853369e6c203e53719f46de1`
* **提交日期**: Fri May 29 10:49:42 2026 +0200
* **拉取日期**: Tue Aug 25 12:55:05 +0800
* **许可证**: MIT License (详见https://gitlab.freedesktop.org/mesa/libdrm/-/commit/82f74e7a5a7403392e91352af00210ae26a81f19, 或者每个文件头部的授权信息部分)

## Pixman
* **上游**: https://gitlab.freedesktop.org/pixman/pixman
* **标签**: `pixman-0.46.4`
* **提交ID**: `9cc163c9da0fb4da430641715313d95a6ec466d9`
* **提交日期**: Sun Jul 20 12:14:02 2025 -0400
* **拉取日期**: Tue Aug 25 13:02:40 +0800
* **许可证**: [MIT License](./pixman/COPYING)

## Waylib
* **上游**: https://github.com/linuxdeepin/treeland
* **标签**: N/A, pulling from main
* **提交ID**: `2031e780f5c770a11843bc81b3cef5271e477dd0`
* **提交日期**: Tue Aug 25 15:24:14 2026 +0800
* **拉取日期**: Wed Aug 26 14:58:32 +0800
* **许可证**: 代码以[GPL-3.0-only](https://github.com/linuxdeepin/treeland/blob/master/waylib/.reuse/dep5)协议获得许可，请参阅他们的[treeland/waylib/.reuse/dep5](https://github.com/linuxdeepin/treeland/blob/master/waylib/.reuse/dep5)文件
**备注**: 上游源自Treeland内置Waylib而非独立的Waylib库

## Abseil
* **上游**: https://github.com/abseil/abseil-cpp/
* **标签**: `lts_2026_08_17`
* **提交ID**: `commit 2065f4ded0558c6f89fee67c8e5228feb4eb960e`
* **提交日期**: Tue Aug 18 08:53:30 2026 -0400
* **拉取日期**: Thu Sep 3 12:35:10 -0500
* **许可证**: [Apache License Version 2.0](./abseil-cpp/LICENSE)
