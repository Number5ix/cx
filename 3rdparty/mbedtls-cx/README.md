# Vendoring Mbed TLS into cx

`3rdparty/mbedtls` is a vendored, patched copy of Mbed TLS **4.2.0**, which bundles
TF-PSA-Crypto **1.2.0** under `tf-psa-crypto/`. This directory holds the tooling that
produces that tree and the record of every change cx makes to it.

| File | What it is |
|---|---|
| `upstream.txt` | The pinned release: version, URL, SHA256 |
| `vendor.sh` | Download → verify → extract → prune → apply patches |
| `refresh.sh` | Regenerate `cx-local.patch` from the current tree |
| `common.sh` | Shared by both; owns the prune list |
| `cx-local.patch` | Machine-generated record of every cx-local change. **Never hand-edit.** |

These are maintainer scripts, run by hand at upgrade time. Run them from a POSIX shell
with `curl`, `sha256sum`, `tar`, `diff` and `git` — git-bash is fine on Windows.

## Why a tarball rather than git-subrepo

Through 3.6.7 this tree tracked the upstream git tag via `git subrepo`. That stopped
working at 4.0. The `mbedtls-4.2.0` tag does not carry the generated sources —
`library/error.c`, `version_features.c`, `ssl_debug_helpers_generated.c`, the
`*_config_check_*.h` headers and `psa_crypto_driver_wrappers.h`/`.c` are all absent — and
`tf-psa-crypto` is a submodule that a tag checkout leaves empty. Building from the tag
needs Perl, Python 3 and jinja2.

The release **tarball** is self-contained: `tf-psa-crypto/` expanded in-tree at the path
upstream's `add_subdirectory` expects, both `framework/` copies, `drivers/pqcp/mldsa-native/`,
every generated file, and `GEN_FILES` defaulted OFF. So cx vendors the tarball and keeps
the local changes as a formal patch series instead.

## Upgrade procedure

1. Edit `upstream.txt` — new `VERSION`, `URL` and `SHA256` from the release's
   published `mbedtls-<version>-sha256sum.txt`.
2. `./vendor.sh`
3. If `cx-local.patch` does not apply cleanly, `vendor.sh` says so. Re-run
   `git apply --reject` inside `3rdparty/mbedtls` and port each rejected hunk by hand,
   using the group descriptions below to understand what the hunk was for. Several of
   them may simply be obsolete — that has already happened once, see *Retired* below.
4. `./refresh.sh` to re-record the patch.
5. Verify (see *Verification* below).

`vendor.sh --pristine` skips step 3's patch application, which is how you get a reference
tree to compare against. `MBEDTLS_TARBALL=<path>` on either script reuses an
already-downloaded tarball instead of re-fetching (it is still checksummed).

## Two traps

**The `.gitignore` files.** Upstream's `library/.gitignore` and
`tf-psa-crypto/core/.gitignore` contain `###START_GENERATED_FILES###` blocks listing
exactly the pre-generated sources the tarball ships. Left in place they silently exclude
those files from `git add`, and the next clean build fails on missing sources with no hint
as to why. `common.sh` deletes every vendored `.gitignore` for this reason. After
vendoring, confirm with:

```
git status --ignored 3rdparty/mbedtls | grep -E 'error\.c|version_features|driver_wrappers'
```

It must print nothing.

**Pruning is not patching.** `common.sh` deletes ~45 MB of tests, sample programs, test
certificates and formal-verification proofs, and then removes any directory left empty.
Both `vendor.sh` and `refresh.sh` apply the identical prune, or `refresh.sh` would record
all of it as deletions inside `cx-local.patch`. If you change `PRUNE_DIRS`, re-run
`refresh.sh` and check the patch still names only the eight files listed below.

## The patch series

Four groups, eight files. `cx-local.patch` is the authoritative form; this is the map.

### A. CMake compatibility — `CMakeLists.txt`, `tf-psa-crypto/CMakeLists.txt`

Both declare `cmake_minimum_required(VERSION 3.5.1)`, which modern CMake rejects outright.
Bumped to 3.24, cx's own floor.

Both also `set(CMAKE_C_STANDARD 99)`. That variable is directory-scoped and would pin the
standard for cx targets configured afterwards, so the line is dropped and mbedTLS inherits
cx's setting.

### B. Object libraries — `library/CMakeLists.txt`, `tf-psa-crypto/core/CMakeLists.txt`, plus the two `CMakeLists.txt` from group A

cxtls absorbs the mbedTLS objects so downstream consumers link one `cxtls.a` rather than
several archives. Upstream already builds every *sub*-piece as an OBJECT library —
`platform`, `extras` and `utilities` via `objlib.cmake`, and `builtin`, `everest`,
`p256-m` and `pqcp` via `drivers/driver.cmake` and their own files. Only the two aggregates
needed changing.

- New `USE_OBJECT_MBEDTLS_LIBRARY` / `USE_OBJECT_TF_PSA_CRYPTO_LIBRARY` options, declared
  next to their `USE_STATIC_*` siblings and forwarded from Mbed TLS to TF-PSA-Crypto the
  same way upstream forwards the others.
- `tfpsacrypto`, `mbedx509` and `mbedtls` become OBJECT instead of STATIC under those
  options. `tfpsacrypto` also stops absorbing `${target_objects}`: an OBJECT library cannot
  swallow another target's objects, so cxtls gathers all ten targets itself
  (`cxtls/CMakeLists.txt`).
- `USE_STATIC_*` deliberately stays ON. All the target-*naming* logic in `objlib.cmake`,
  `drivers/driver.cmake`, `drivers/everest/CMakeLists.txt` and
  `drivers/p256-m/CMakeLists.txt` is keyed on `USE_STATIC_TF_PSA_CRYPTO_LIBRARY`; layering
  on top of it rather than replacing it keeps this patch to two files instead of six.
- The `install(TARGETS ...)` calls in both files are guarded with
  `if(NOT DISABLE_PACKAGE_CONFIG_AND_INSTALL)`. That option already defaults ON for a
  subproject, the export sets they feed are only installed under the same condition, and
  `install(TARGETS)` rejects an OBJECT library outright.
- The trailing `foreach(target IN LISTS tf_psa_crypto_library_targets)` block in
  `library/CMakeLists.txt` is skipped entirely for object builds. It exists to provide the
  crypto library under its historical `libmbedcrypto.*` name by copying and installing
  `$<TARGET_FILE:${target}>`; an OBJECT library has no such file, and the `else()` branch
  would go on to ask for `$<TARGET_SONAME_FILE_NAME:>`.

### C. cx threading — `tf-psa-crypto/include/mbedtls/threading.h`, `tf-psa-crypto/platform/threading.c`, `tf-psa-crypto/core/tf_psa_crypto_check_config.h`

`MBEDTLS_THREADING_CX` makes mbedTLS lock with cx's own mutexes and condition variables
instead of pulling in a second threading implementation. Not `MBEDTLS_THREADING_ALT`:
that needs a runtime `mbedtls_threading_set_alt()` call before any other library function,
and there is no natural place for cx to make it.

4.x restructured this area, so it is a rewrite of the 3.6.7 patch rather than a
re-application. Two things changed for the better and one for the worse:

- The *platform* mutex type is now separate from the wrapper `mbedtls_threading_mutex_t`
  (which carries `initialized` and `state`), so cx no longer defines the wrapper struct
  itself. Strictly less to maintain: `typedef Mutex mbedtls_platform_mutex_t;`.
- The PTHREAD block is a clean template to mirror, including the four
  `mbedtls_mutex_*_ptr` assignments.
- New in 4.x: five condition-variable entry points
  (`mbedtls_condition_variable_{init,free,signal,broadcast,wait}`) must be defined by the
  backend. They map onto `cvarInit` / `cvarDestroy` / `cvarSignal` / `cvarBroadcast` /
  `cvarWait`.

**The `MUTEX_INIT` subtlety, which cost real debugging time.** Nothing in the library ever
calls `mbedtls_mutex_init()` on the five global mutexes at the bottom of `threading.c`.
PTHREAD statically initializes them; `MBEDTLS_THREADING_ALT` initializes them from
`mbedtls_threading_set_alt()`. A backend that defines neither leaves them with
`initialized == 0`, and then *every* lock on them returns
`MBEDTLS_ERR_THREADING_USAGE_ERROR` — which surfaces as `psa_crypto_init()` failing with
`PSA_ERROR_SERVICE_FAILURE` (-144) and nothing pointing at threading. So this backend
statically initializes them too.

An all-bits-zero cx `Mutex` is exactly what `mutexInit()` produces, with one exception:
on Windows, `futexInit()` also performs a process-wide one-time resolve of the platform
wait/wake primitives, and a statically-initialized mutex skips it, leaving the futex
fallback path with no usable handle. `threading_platform_init_cx()` runs that setup
through `lazyInit()` on first lock, at the cost of one relaxed load per lock.

`tf_psa_crypto_check_config.h` gets the `MBEDTLS_THREADING_CX` prerequisite paragraph next
to the PTHREAD and ALT ones. Note that this file, not the generated
`tf_psa_crypto_config_check_*.h`, is where the threading checks live.

### D. Windows XP — `tf-psa-crypto/core/CMakeLists.txt`, `library/x509_crt.c`

- `if(WIN32) set(libs ${libs} ws2_32 bcrypt)` omits `bcrypt` under `CX_XP_COMPAT`;
  `bcrypt.dll` does not exist on XP and a static import of it fails at process start.
  (This moved from `library/CMakeLists.txt` in 4.x.) With the entropy hook below in place,
  nothing in the tree references BCrypt at all.
- `x509_crt.c`: `#ifdef _MSC_VER` → `#if defined(_MSC_VER) && _WIN32_WINNT >= 0x0600`
  around the `#pragma comment(lib, "ws2_32.lib")` / `<winsock2.h>` block, so MSVC takes
  mbedTLS's software `inet_pton()` fallback. Unchanged one-liner from the 3.6.7 patch.

### Retired

**The `entropy_poll.c` patch is gone.** 3.6.7 needed a hand-written `CryptGenRandom` branch
because `MBEDTLS_NO_PLATFORM_ENTROPY` left no way to supply entropy. 4.x has a supported
hook: `MBEDTLS_PSA_DRIVER_GET_ENTROPY` makes the library call a caller-supplied
`mbedtls_platform_get_entropy()` instead of its built-in pollers. cxtls implements it over
cx's `osGenRandom()` in `cxtls/cxtls_entropy.c`, enabled on **all** platforms rather than
only XP, so there is one RNG code path and cx owns its entropy source end to end. One less
patched upstream file, and the XP case stops being special.

**The whole "don't install anything" patch category is gone.** 4.x adds
`MBEDTLS_AS_SUBPROJECT` / `TF_PSA_CRYPTO_AS_SUBPROJECT` and
`DISABLE_PACKAGE_CONFIG_AND_INSTALL` (defaulting ON for subprojects), which covers the
package-config export, `pkgconfig/` and the header installs. `framework/CMakeLists.txt` is
intentionally blank, so `add_subdirectory(framework)` costs nothing and needs no guard.
Only the two `install(TARGETS)` guards in group B remain, and those are there because
OBJECT libraries cannot be installed, not to suppress installation as such.

## Configuration lives in cx, not in the vendored tree

`include/mbedtls/mbedtls_config.h` and `tf-psa-crypto/include/psa/crypto_config.h` are at
**zero diff** against upstream — deliberately, since config drift was the single largest
source of upgrade merge pain in the 3.x series. Everything cx changes goes through the two
`*_USER_CONFIG_FILE` variables set in `3rdparty/CMakeLists.txt`:

- `cxtls/include/cxtls_crypto_config.h` → `TF_PSA_CRYPTO_USER_CONFIG_FILE`: the allocator,
  threading and entropy hooks.
- `cxtls/include/cxtls_mbedtls_config.h` → `MBEDTLS_USER_CONFIG_FILE`: currently empty.

Both are included after the defaults and before the `config_adjust_*` headers, so `#undef`
works there. Note that Mbed TLS actively rejects crypto-domain options set in the TLS
config (`library/mbedtls_config_check_user.h` enforces it), which is why threading and
allocation settings belong in the crypto one.

### Mechanism trimming

The 3.6.7 patch trimmed DES, CHACHA20, POLY1305, RIPEMD160, the PSK and DHE key exchanges,
`MBEDTLS_ERROR_C` and `MBEDTLS_SELF_TEST` out of the config for footprint. Part of that is
moot: 4.0 removed DES, the RSA-decryption, DHE and static-ECDH key exchanges,
`MBEDTLS_OID_C` and the low/high-level error split outright.

The cipher and hash half is carried forward, in `cxtls_crypto_config.h` under `PSA_WANT_*`
spellings rather than as a patch: RIPEMD-160 is disabled.

Still deferred, because it wants real code to size against: the TLS-layer options in
`cxtls_mbedtls_config.h` (`MBEDTLS_SELF_TEST`, unused key exchanges, protocol versions,
X.509 features) and the remaining unused crypto mechanisms — ARIA, Camellia, Brainpool and
Montgomery curves, FFDH/RFC 7919 groups, J-PAKE, SHA-3/SHAKE, PBKDF2 among them. Those are
each a decision about what cxtls must interoperate with, not a footprint judgement call.

## Verification

After an upgrade, in rough order of how much they catch:

1. **Build**, at minimum `gcc-debug`, `gcc-release`, `clang-dev`, `msvc-dev`, and
   `msvc-dev` with `-DCX_XP_COMPAT=ON`. Confirm `-DCX_TLS=OFF` still configures.
2. **Object absorption**: `nm -g build/<preset>/cxtls/libcxtls.a | grep -c ' T psa_'` is
   non-zero, and every object under `build/<preset>/3rdparty/mbedtls/**/CMakeFiles/*.dir/`
   appears in `ar t libcxtls.a`. This is what catches a silently dropped
   `$<TARGET_OBJECTS>` after a target rename.
3. **Hooks actually wired**, which compiling alone does not prove:
   - `nm -u <any mbedtls .o> | grep -E ' (calloc|malloc|free)$'` finds nothing, and
     `psa_crypto.c.o` has undefined `xa_calloc` / `xa_free`.
   - `nm -u .../platform.dir/threading.c.o` shows `_mutexInit`, `mutexTryAcquireTimeout`,
     `mutexRelease`, `_cvarInit` … and no `pthread_mutex_*` anywhere in the tree.
   - `mbedtls_platform_get_entropy` is defined only by `cxtls_entropy.c.o`.
4. **Smoke test**: `test_runner tlstest psa` — `psa_crypto_init()` then
   `psa_generate_random()`. Run it under the leak-checking build too, to confirm
   `xa_calloc` / `xa_free` really are in the path.
5. **The `.gitignore` check** above.
6. **Patch round-trip**: `./refresh.sh` leaves `cx-local.patch` unchanged, and `./vendor.sh`
   from scratch reproduces a byte-identical tree.

## Notes for whenever cxtls gets written

The 4.x API is a hard break from 3.6: the PSA API is the only crypto API and the legacy
`mbedtls_*` primitives are gone. In particular `psa_crypto_init()` is now mandatory before
any TLS or X.509 use, the `f_rng` / `p_rng` parameters are gone everywhere, and functions
may return `PSA_ERROR_*` as well as `MBEDTLS_ERR_*`.

The generic target names `platform`, `extras` and `utilities` now occupy cx's global CMake
target namespace. They do not collide with anything today; worth remembering if cx ever
grows a target by one of those names.
