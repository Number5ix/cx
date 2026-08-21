#!/bin/sh
#
# Replaces 3rdparty/mbedtls with a freshly vendored copy of the upstream
# release pinned in upstream.txt, then re-applies cx-local.patch.
#
# Maintainer tooling, run by hand at upgrade time. It is never a build step.
# Needs a POSIX shell with curl, sha256sum, tar and git (git-bash is fine on
# Windows). No Python, no Perl.
#
# Usage:
#   ./vendor.sh              download, verify, extract, prune, apply patches
#   ./vendor.sh --pristine   same, but skip the patch step
#
# Environment:
#   MBEDTLS_TARBALL=<path>   reuse an already-downloaded tarball
#
# See README.md for the full upgrade procedure.

. "$(dirname "$0")/common.sh"

PRISTINE=0
case "${1:-}" in
    --pristine) PRISTINE=1 ;;
    "") ;;
    *) die "usage: $0 [--pristine]" ;;
esac

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM

TARBALL=$(fetch_tarball "$WORK")
extract_pristine "$TARBALL" "$WORK/tree"

echo "Replacing $VENDOR_DIR" >&2
rm -rf "$VENDOR_DIR"
mv "$WORK/tree" "$VENDOR_DIR"

if [ "$PRISTINE" -eq 1 ]; then
    echo "Vendored mbedtls $VERSION (pristine; cx-local.patch NOT applied)." >&2
    exit 0
fi

if [ ! -s "$CXMBED_DIR/cx-local.patch" ]; then
    die "cx-local.patch is missing or empty; use --pristine if that is intended"
fi

echo "Applying cx-local.patch" >&2
if ! (cd "$VENDOR_DIR" && git apply -p1 --verbose "$CXMBED_DIR/cx-local.patch"); then
    cat >&2 <<'MSG'

cx-local.patch did not apply cleanly.

Re-run with `git apply --reject` inside 3rdparty/mbedtls to leave .rej files,
port each rejected hunk by hand using README.md as the map of what every
customization is for and why, then run ./refresh.sh to re-record the patch.
MSG
    exit 1
fi

cat >&2 <<MSG

Vendored mbedtls $VERSION with cx-local.patch applied.

Next:
  git status --ignored ../mbedtls | grep -E 'error\.c|version_features|driver_wrappers'
      must print nothing -- see the .gitignore note in common.sh
  cmake --preset gcc-debug && cmake --build build/gcc-debug
MSG
