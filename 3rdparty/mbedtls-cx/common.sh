# Shared helpers for vendor.sh and refresh.sh. Sourced, not executed.
#
# Both scripts must agree on exactly how a pristine upstream tree is produced,
# or refresh.sh would record the pruning as part of cx-local.patch. Everything
# that shapes that tree therefore lives here.

set -eu

CXMBED_DIR=$(cd "$(dirname "$0")" && pwd)
VENDOR_DIR=$(cd "$CXMBED_DIR/.." && pwd)/mbedtls

# shellcheck disable=SC2046
eval $(grep -E '^(VERSION|URL|SHA256)=' "$CXMBED_DIR/upstream.txt")

if [ -z "${VERSION:-}" ] || [ -z "${URL:-}" ] || [ -z "${SHA256:-}" ]; then
    echo "upstream.txt must define VERSION, URL and SHA256" >&2
    exit 1
fi

TARBALL_NAME="mbedtls-$VERSION.tar.bz2"

die() { echo "$*" >&2; exit 1; }

# Downloads the pinned tarball and verifies its checksum. Echoes the path to
# the verified file. Set MBEDTLS_TARBALL to reuse an already-downloaded copy
# (it is still checksummed).
fetch_tarball() {
    _dest=$1
    if [ -n "${MBEDTLS_TARBALL:-}" ]; then
        [ -f "$MBEDTLS_TARBALL" ] || die "MBEDTLS_TARBALL=$MBEDTLS_TARBALL does not exist"
        cp "$MBEDTLS_TARBALL" "$_dest/$TARBALL_NAME"
    else
        echo "Downloading $URL" >&2
        curl -fsSL -o "$_dest/$TARBALL_NAME" "$URL" || die "download failed"
    fi

    echo "$SHA256  $_dest/$TARBALL_NAME" | sha256sum -c - >&2 || die "checksum mismatch"
    echo "$_dest/$TARBALL_NAME"
}

# Directories dropped from the vendored tree. ENABLE_TESTING and
# ENABLE_PROGRAMS are forced OFF in 3rdparty/CMakeLists.txt, so none of this is
# reachable from a cx build; it is ~45 MB of test vectors, certificates, proofs
# and sample programs that would otherwise live in cx's history forever.
# Deleting rather than patching keeps it out of cx-local.patch.
PRUNE_DIRS="
tests
programs
framework/tests
framework/data_files
framework/psasim
tf-psa-crypto/tests
tf-psa-crypto/programs
tf-psa-crypto/framework/tests
tf-psa-crypto/framework/data_files
tf-psa-crypto/framework/psasim
tf-psa-crypto/drivers/pqcp/mldsa-native/proofs
tf-psa-crypto/drivers/pqcp/mldsa-native/examples
tf-psa-crypto/drivers/pqcp/mldsa-native/test
tf-psa-crypto/drivers/pqcp/mldsa-native/nix
tf-psa-crypto/drivers/pqcp/mldsa-native/integration
"

# Extracts the verified tarball into $1, which must not exist, and prunes it.
extract_pristine() {
    _tarball=$1
    _dest=$2

    [ -e "$_dest" ] && die "$_dest already exists"
    _stage=$(dirname "$_dest")/.extract.$$
    mkdir -p "$_stage"
    tar xjf "$_tarball" -C "$_stage"
    [ -d "$_stage/mbedtls-$VERSION" ] || die "tarball does not contain mbedtls-$VERSION/"
    mv "$_stage/mbedtls-$VERSION" "$_dest"
    rmdir "$_stage"

    prune_tree "$_dest"
}

prune_tree() {
    _root=$1

    # Submodule metadata: tf-psa-crypto and both framework copies are already
    # expanded in the tarball, so these only confuse `git submodule`.
    rm -f "$_root/.gitmodules" "$_root/tf-psa-crypto/.gitmodules"

    # Every vendored .gitignore, without exception. upstream's
    # library/.gitignore and tf-psa-crypto/core/.gitignore carry
    # ###START_GENERATED_FILES### blocks naming precisely the pre-generated
    # sources the tarball ships. Left in place they silently exclude those
    # files from `git add` and the next clean build fails on missing sources.
    find "$_root" -name .gitignore -type f -exec rm -f {} +

    for _d in $PRUNE_DIRS; do
        rm -rf "$_root/$_d"
    done

    # Directories left holding nothing but a pruned .gitignore. Git cannot track
    # an empty directory, so leaving them behind makes the vendored tree differ
    # from what a fresh checkout produces and shows up as phantom churn in
    # refresh.sh's diff. -delete implies -depth, so nesting resolves in one pass.
    find "$_root" -type d -empty -delete
}
