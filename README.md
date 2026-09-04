# FlakeWM
FlakeWM is a Wlroots based Wayland compositor. Currently, it's under early development stage.

## Dependencies
> Please refer to [libs/README.md](./libs/README.md) for the version and licensing info of those vendored third-party libraries.

### Core Dependencies (Vendored)
* Wlroots 0.20.2
* Abseil lts_2026_08_17

### Other Dependencies
I realized that the version(s) of `libdrm`, `pixman` and `wayland` required by Wlroots 0.20.2 may be too high for some distros, and we have also vendored those libraries. When the system packages cannot satisify the version requirement, CMake will fall back to vendored library. But keep in mind, **THIS IS NOT A GOOD PRACTICE**.

## Acknowledgement
We refrenced the following projects when developing FlakeWM:
* **GXDE Wayland Compositor**: https://github.com/GXDE-OS/gxde-wlcom
* **LabWC**: https://github.com/labwc/labwc
* **Wayfire**: https://github.com/wayfirewm/wayfire
* **Noctalia Umbriel**: https://github.com/noctalia-dev/umbriel

## License
FlakeWM is licensed under GNU GENERAL PUBLIC LICENSE Version 3. Please read [COPYING](./COPYING) for more information.
