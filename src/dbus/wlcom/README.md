# GXWM / gxde-wlcom D-Bus compatibility layer

This directory is the compositor-owned compatibility implementation for the
D-Bus ABI exported by `gxde-wlcom`.  It intentionally keeps the original bus
names, object paths, interface names, method and signal spelling, and D-Bus
signatures.  Existing GXDE, UKUI, KDE and Deepin clients must not need a FlakeWM
specific code path.

The contract was audited against the source vtables, `README.md`,
`README.zh.md`, `docs/gxde/manual/dbus.md`, and
`docs/gxde/manual/multitasking-launcher-interface.md` in the GXDE fork.

## Source layout

- `wlcom_dbus_manager.cc` owns registration, introspection, configuration and
  common dispatch.
- `wlcom_dbus_output.cc` implements output control; `wlcom_dbus_view.cc`
  implements window and show-desktop control.
- `wlcom_dbus_input.cc` implements devices, seats, input actions and UKUI
  shortcut integration.
- `wlcom_dbus_desktop.cc` implements theme, effect and plugin compatibility.
- `wlcom_dbus_screenshot.cc` implements capture and watermark rendering.
- `wlcom_dbus_kglobalaccel.cc` implements clipboard, KGlobalAccel and KDE
  property compatibility.
- `wlcom_dbus_manager_p.h` contains implementation-only D-Bus value types and
  shared helpers.  `wlcom_dbus_manager.h` remains the only public header.

## Owned session-bus names

| Name | Objects / compatibility purpose |
| --- | --- |
| `com.kylin.Wlcom` | Common, Output, View, Input, InputAction, Seat, Theme, Effect, Plugin and Watermark |
| `top.gxde.Wlcom` | GXDE Effect alias |
| `top.gxde.Wlcom.Screen` | GXDE screen-control alias |
| `top.gxde.Wlcom.Screenshot` | GXDE full-layout screenshot-to-clipboard alias |
| `top.gxde.Wlcom.Theme` | GXDE theme alias |
| `top.gxde.Wlcom.WindowBtn` | GTK decoration-button settings |
| `top.gxde.Wlcom.WindowCorner` | GXDE forced-corner settings |
| `top.gxde.Wlcom.MouseFinder` | GXDE mouse finder (`shake_cursor` effect) switch |
| `org.ukui.KWin` | UKUI Screenshot and Watermark compatibility |
| `org.kde.KWin` | KDE Screenshot, Clipboard and InputDevice compatibility |
| `org.kde.KWin.PresentWindows` | KWin PresentWindows plugin compatibility |
| `org.kde.kglobalaccel` | KGlobalAccel registry and dynamic Component objects |
| `com.deepin.wm` | Deepin WM action and show-desktop compatibility |

Dynamic KDE input objects are exported below
`/org/kde/KWin/InputDevice/<sysname>`.  Dynamic KGlobalAccel component objects
are exported below `/component/<escaped-component-name>`.

## Behavioural integration

- Output changes commit through wlroots, update the output layout and protocol
  advertisements, and persist the same per-output JSON fields used by GXWM:
  `uuid`, `enabled`, `width`, `height`, `refresh`, `scale`, `transform`, `lx`,
  `ly`, `brightness`, `color_temp`, and `primary`.
- Brightness and colour temperature are applied as an output colour transform,
  including render-path fallback when the backend has no hardware gamma LUT.
- Watermark calls create real non-interactive scene buffers on every enabled
  output.  All four GXWM expansion modes (positioned, repeated, stretched and
  aspect-preserving) and the `topmost` layer selection are implemented.
- Screenshot calls capture output, area or the complete multi-output layout,
  preserve the UKUI/KDE argument order, optionally include the software cursor,
  and return a PNG path.  The GXDE alias publishes and retains the PNG through
  a compositor-owned `image/png` Wayland selection, without an external
  `wl-copy` dependency.
- Input configuration is backed by libinput/xkb where the device supports it.
  Custom input actions and KGlobalAccel shortcuts use the compositor's real key
  binding manager.  Command, D-Bus, pointer-button and synthetic-key actions
  execute with GXWM's original field names, key-code table and press/release
  ordering.  Touchpad and touchscreen gestures implement the original
  `trigger`, `after` and `stop` stages, follow thresholds/directions, finger
  counts and touchscreen edge matching.  System defaults come from
  `/etc/flakewm/config.json` (shipped by `gxde-flake-session`), falling back
  to GXWM's `/etc/gxde-wlcom/config.json`; FlakeWM writes user overrides to
  `~/.config/flakewm/config.json`.  The former
  `~/.config/flakewm/dbus.json` and GXWM's
  `~/.config/gxde-wlcom/config.json` are read as migration fallbacks.  The
  original `InputAction`, `Inputs`, `Seats`, `Views`, `Effects`, `outputs`,
  `plugins`, and `theme` schema is retained, so replacing the compositor does
  not reset existing settings.
- View and show-desktop calls operate on compositor toplevel state.  Deepin and
  PresentWindows entry points retain their original ABI and forward the
  multitask request to the GXDE switch service used by this desktop stack.
- KDE clipboard properties and signals report the PID owning the corresponding
  Wayland selection.  Active-app changes are forwarded to GXDE AppBridge.
- The Effect interface mirrors GXWM's registry of 21 effects, in its
  `ListAllEffects` order.  `ListAllEffects` reports runtime state and
  `EnableEffect` changes it without persisting; `SetEffectOption` checks the
  option's type and each effect's `configure()` rules (e.g. `blur_strength`
  1-15, `noise_strength` 0-14), then persists it, and an `enabled` option only
  takes effect on the next start.  Blur enable/strength changes are applied
  immediately to existing and future KDE blur regions.  The other effects,
  and `noise_strength`, are stored for compatibility but have no FlakeWM
  rendering behind them.
- `shake_cursor` is implemented (`src/view/shake_cursor/`): shaking the pointer
  shows the theme's arrow at 4x size until it settles, with GXWM's detector
  and timings.  `MouseFinder.SetEnabled` switches it at runtime, persists its
  `enabled` option and mirrors it to `org.ukui.peripherals-mouse
  shake-cursor`; changes to that key are applied back the same way.
- The compositor publishes its Wayland/X11 desktop variables through both
  `org.freedesktop.DBus.UpdateActivationEnvironment` and systemd's user manager.
- UKUI settings-daemon services are discovered through D-Bus owner changes;
  their `blockShortcuts`/`unblockShortcuts` blacklists and whitelists are
  applied to the same typed shortcut registry and are undone when the service
  disappears, matching GXWM's Kylin desktop integration.

## Compatibility rule

Treat every exported name and signature here as ABI.  Adding aliases is safe;
renaming a bus, path, interface, member, property or error is not.  When an
upstream GXWM interface changes, update the virtual-object introspection XML and
the matching handler together, then verify it in an isolated session bus with a
headless output.
