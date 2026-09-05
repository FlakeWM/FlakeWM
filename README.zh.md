# FlakeWM
FlakeWM是一个Wlroots系合成器，当前正在早期开发状态。

## 依赖
> 关于集成的第三方库的版本记录，请移步[libs/README.zh.md](./libs/README.zh.md).

### 核心依赖 (已集成)
* Wlroots 0.20.2
* Abseil lts_2026_08_17

### 其余依赖
我意识到Wlroots 0.20.2要求的`libdrm`、`pixman`和`wayland`对某些发行版来说新过头了，我们也集成了这三个库，在系统包版本不满足编译要求时CMake会自动使用集成的版本并静态链接。

不过要知道这不是什么好主意就是了。

本合成器采用Qt Quick离屏渲染标题栏，请自备Qt6.8。

## 致谢
本项目参考了以下项目：
* **GXDE Wayland Compositor**: https://github.com/GXDE-OS/gxde-wlcom
* **LabWC**: https://github.com/labwc/labwc
* **Wayfire**: https://github.com/wayfirewm/wayfire
* **Noctalia Umbriel**: https://github.com/noctalia-dev/umbriel
* **Hyprland 0.42.0**: https://github.com/hyprwm/Hyprland/tree/v0.42.0

## 许可证
FlakeWM以GNU GENERAL PUBLIC LICENSE Version 3协议获得许可，详情请移步[COPYING](./COPYING)。
