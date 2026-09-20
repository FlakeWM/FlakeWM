#!/bin/sh
# Bundle wlroots' vendored fallback libraries (wayland, libdrm, pixman) when
# they were built from source because the distro copies are too old.
#
#   bundle-vendored.sh <builddir> <destdir>
#
# - Copies the SONAME'd shared objects into <destdir>/usr/lib/flakewm/.
# - Rewrites the flakewm RUNPATH to $ORIGIN/../lib/flakewm.
# - Writes debian/shlibs.local so dh_shlibdeps does not map those private
#   copies to the (older) system packages.
set -eu

BUILDDIR=${1:?usage: bundle-vendored.sh <builddir> <destdir>}
DESTDIR=${2:?}

VENDOR_ROOT="$BUILDDIR/_deps/wlroots-dependencies"
PRIVATE_LIBDIR="$DESTDIR/usr/lib/flakewm"
SHLIBS_LOCAL="$(dirname "$DESTDIR")/shlibs.local"

: > "$SHLIBS_LOCAL"

bundle_soname() {
    subdir=$1
    soname=$2
    libdir="$VENDOR_ROOT/$subdir/lib"
    [ -e "$libdir/$soname" ] || return 0
    mkdir -p "$PRIVATE_LIBDIR"
    cp -a "$libdir/$soname" "$PRIVATE_LIBDIR/"
    # The SONAME is usually a symlink to the real versioned file; copy it too
    # so the symlink resolves after installation.
    target=$(readlink "$libdir/$soname" || true)
    if [ -n "$target" ]; then
        cp -a "$libdir/$target" "$PRIVATE_LIBDIR/"
    fi
    # shlibs.local maps the private copy to flakewm itself (see debian/rules).
    echo "${soname%%.so.*} ${soname#*.so.} flakewm" >> "$SHLIBS_LOCAL"
}

bundle_soname wayland libwayland-server.so.0
bundle_soname wayland libwayland-client.so.0
bundle_soname libdrm libdrm.so.2
bundle_soname pixman libpixman-1.so.0
bundle_soname xkbcommon libxkbcommon.so.0

if [ -d "$PRIVATE_LIBDIR" ]; then
    patchelf --set-rpath '$ORIGIN/../lib/flakewm' "$DESTDIR/usr/bin/flakewm"
fi
