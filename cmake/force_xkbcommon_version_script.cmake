# FlakeWM: applied as PATCH_COMMAND to the *copied* vendored xkbcommon source
# (never to libs/xkbcommon itself, which stays upstream-pristine).
#
# xkbcommon's meson.build decides whether to apply its symbol-version script
# by linking an *executable* with -Wl,--version-script. That probe returns a
# false negative on the GXDE riscv64 CI, so libxkbcommon is built there without
# symbol versions. Consumers that were linked against a versioned libxkbcommon
# (e.g. libQt6Gui.so.6.8.2) then fail to link with
#   undefined reference to `xkb_state_key_get_level@V_0.5.0'
# because an unversioned definition does not satisfy a versioned reference.
# On Linux with the GNU linker the version script is always supported, so force
# the flag instead of relying on the fragile probe.

if(NOT DEFINED XKBCOMMON_SRC)
  message(FATAL_ERROR "XKBCOMMON_SRC is not set")
endif()

set(_meson_build "${XKBCOMMON_SRC}/meson.build")
file(READ "${_meson_build}" _content)

set(_probe [=[
have_version_script = cc.links(
    'int main() { return 0; }',
    args: f'-Wl,--version-script=@meson_test_map@',
    name: '-Wl,--version-script',
)]=])

string(FIND "${_content}" "${_probe}" _idx)
if(_idx EQUAL -1)
  message(FATAL_ERROR
    "xkbcommon version-script probe block not found in ${_meson_build}; "
    "the vendored source changed and this patch needs updating")
endif()

string(REPLACE "${_probe}" "have_version_script = true" _content "${_content}")
file(WRITE "${_meson_build}" "${_content}")
