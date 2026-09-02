#include <cx/fs.h>
#include <cx/fs/path.h>
#include <cx/format.h>
#include <cx/fs/vfsobj.h>
#include <cx/thread/atomic.h>
#include <cx/platform/base.h>
#include <cx/string.h>

#include "vfstestprov.h"

#define TEST_FILE  vfstest
#define TEST_FUNCS vfstest_funcs
#include "common.h"

// Reads a whole VFS file into out. Returns false if it could not be opened.
static bool readAll(_Inout_ string* out, _Inout_ VFS* vfs, _In_opt_ strref path)
{
    uint8 buf[512];
    size_t bytesread = 0;

    VFSFile* f = vfsOpen(vfs, path, FS_Read);
    if (!f)
        return false;

    bool ret = vfsRead(f, buf, sizeof(buf), &bytesread);
    if (ret)
        strFromBytes(out, buf, (uint32)bytesread);
    vfsClose(f);
    return ret;
}

// Checks that path reads back as want, logging both sides on mismatch.
static void checkContents(_Inout_ int* ret, _Inout_ VFS* vfs, _In_opt_ strref path,
                          _In_opt_ strref want)
{
    string got = 0;

    if (!readAll(&got, vfs, path)) {
        TEST_FAILV(*ret, 1, _SL("could not read '${string}' (wanted '${string}')"),
                   stvar(strref, path), stvar(strref, want));
    } else if (!strEq(got, want)) {
        TEST_FAILV(*ret, 1, _SL("'${string}' contains '${string}', wanted '${string}'"),
                   stvar(strref, path), stvar(strref, got), stvar(strref, want));
    }

    strDestroy(&got);
}

// Checks vfsStat's verdict on a path.
static void checkStat(_Inout_ int* ret, _Inout_ VFS* vfs, _In_opt_ strref path, int want)
{
    int got = vfsStat(vfs, path, NULL);
    if (got != want) {
        TEST_FAILV(*ret, 1, _SL("vfsStat('${string}') == ${int}, wanted ${int}"),
                   stvar(strref, path), stvar(int32, got), stvar(int32, want));
    }
}

// Collects a VFS directory search into a sorted, comma-joined string, so a whole listing can be
// compared (and logged) in one go.
static void searchList(_Inout_ string* out, _Inout_ VFS* vfs, _In_opt_ strref path,
                       _In_opt_ strref pattern, int typefilter)
{
    FSSearchIter iter;
    sa_string names;
    saInit(&names, string, 8, SA_Sorted);

    if (vfsSearchInit(&iter, vfs, path, pattern, typefilter, false)) {
        while (vfsSearchValid(&iter)) {
            saPush(&names, string, iter.name);
            vfsSearchNext(&iter);
        }
    }
    vfsSearchFinish(&iter);

    strJoin(out, names, _S",");
    saDestroy(&names);
}

// Checks a whole directory listing against a comma-separated expected list.
static void checkSearch(_Inout_ int* ret, _Inout_ VFS* vfs, _In_opt_ strref path,
                        _In_opt_ strref pattern, int typefilter, _In_opt_ strref want)
{
    string got = 0;
    searchList(&got, vfs, path, pattern, typefilter);

    if (!strEq(got, want)) {
        TEST_FAILV(*ret, 1, _SL("search('${string}', '${string}') == '${string}', wanted '${string}'"),
                   stvar(strref, path), stvar(strref, pattern), stvar(strref, got),
                   stvar(strref, want));
    }

    strDestroy(&got);
}

// A provider with a small fixed tree:
//   a.txt, sub/, sub/b.txt, sub/deep/, sub/deep/c.txt
static VFSTestProv* sampleProvider(uint32 provflags, strref tag)
{
    VFSTestProv* prov = vfstestprovCreate(provflags);
    string contents   = 0;

    strNConcat(&contents, _S"a:", tag);
    vfstestprovAddFile(prov, _S"a.txt", contents);
    strNConcat(&contents, _S"b:", tag);
    vfstestprovAddFile(prov, _S"sub/b.txt", contents);
    strNConcat(&contents, _S"c:", tag);
    vfstestprovAddFile(prov, _S"sub/deep/c.txt", contents);

    strDestroy(&contents);
    return prov;
}

static int test_vfs_basic()
{
    int ret           = 0;
    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* prov = sampleProvider(VFS_CaseSensitive, _S"1");

    if (!vfsMountProvider(vfs, prov, _S"/"))
        TEST_FAILV(ret, 1, _SL("could not mount test provider at /"), stvNone);

    checkStat(&ret, vfs, _S"/a.txt", FS_File);
    checkStat(&ret, vfs, _S"/sub", FS_Directory);
    checkStat(&ret, vfs, _S"/sub/b.txt", FS_File);
    checkStat(&ret, vfs, _S"/sub/deep/c.txt", FS_File);
    checkStat(&ret, vfs, _S"/nope.txt", FS_Nonexistent);
    checkStat(&ret, vfs, _S"/sub/nope.txt", FS_Nonexistent);

    checkContents(&ret, vfs, _S"/a.txt", _S"a:1");
    checkContents(&ret, vfs, _S"/sub/b.txt", _S"b:1");
    checkContents(&ret, vfs, _S"/sub/deep/c.txt", _S"c:1");

    // repeat, to go through the file cache rather than the providers
    checkContents(&ret, vfs, _S"/sub/deep/c.txt", _S"c:1");
    checkStat(&ret, vfs, _S"/a.txt", FS_File);

    objRelease(&prov);
    vfsDestroy(&vfs);
    return ret;
}

static int test_vfs_write()
{
    int ret           = 0;
    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* prov = sampleProvider(VFS_CaseSensitive, _S"1");
    size_t written    = 0;

    vfsMountProvider(vfs, prov, _S"/");

    VFSFile* f = vfsOpen(vfs, _S"/new.txt", FS_Write | FS_Create);
    if (!f) {
        TEST_FAILV(ret, 1, _SL("could not create /new.txt"), stvNone);
    } else {
        if (!vfsWriteString(f, _S"hello", &written))
            TEST_FAILV(ret, 1, _SL("write to /new.txt failed"), stvNone);
        if (!vfsClose(f))
            TEST_FAILV(ret, 1, _SL("vfsClose reported failure"), stvNone);
    }

    checkStat(&ret, vfs, _S"/new.txt", FS_File);
    checkContents(&ret, vfs, _S"/new.txt", _S"hello");

    if (!vfsDelete(vfs, _S"/new.txt"))
        TEST_FAILV(ret, 1, _SL("could not delete /new.txt"), stvNone);
    checkStat(&ret, vfs, _S"/new.txt", FS_Nonexistent);

    objRelease(&prov);
    vfsDestroy(&vfs);
    return ret;
}

static int test_vfs_layer()
{
    int ret            = 0;
    VFS* vfs           = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* lower = sampleProvider(VFS_CaseSensitive, _S"low");
    VFSTestProv* upper = vfstestprovCreate(VFS_CaseSensitive);

    vfstestprovAddFile(upper, _S"a.txt", _S"a:high");
    vfstestprovAddFile(upper, _S"only-high.txt", _S"h");

    vfsMountProvider(vfs, lower, _S"/");
    vfsMountProvider(vfs, upper, _S"/");

    // the later mount is higher on the stack and shadows the lower one
    checkContents(&ret, vfs, _S"/a.txt", _S"a:high");
    // ...but does not hide what it has no entry for
    checkContents(&ret, vfs, _S"/sub/b.txt", _S"b:low");
    checkContents(&ret, vfs, _S"/only-high.txt", _S"h");

    checkSearch(&ret, vfs, _S"/", NULL, 0, _S"a.txt,only-high.txt,sub");

    objRelease(&lower);
    objRelease(&upper);
    vfsDestroy(&vfs);
    return ret;
}

static int test_vfs_opaque()
{
    int ret            = 0;
    VFS* vfs           = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* lower = sampleProvider(VFS_CaseSensitive, _S"low");
    VFSTestProv* upper = vfstestprovCreate(VFS_CaseSensitive);

    vfstestprovAddFile(upper, _S"only-high.txt", _S"h");

    vfsMountProvider(vfs, lower, _S"/");
    vfsMountProvider(vfs, upper, _S"/", VFS_Opaque);

    // an opaque layer hides everything below it, shadowed or not
    checkStat(&ret, vfs, _S"/only-high.txt", FS_File);
    checkStat(&ret, vfs, _S"/a.txt", FS_Nonexistent);
    checkSearch(&ret, vfs, _S"/", NULL, 0, _S"only-high.txt");

    objRelease(&lower);
    objRelease(&upper);
    vfsDestroy(&vfs);
    return ret;
}

static int test_vfs_readonly()
{
    int ret           = 0;
    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* prov = sampleProvider(VFS_CaseSensitive, _S"1");

    vfsMountProvider(vfs, prov, _S"/", VFS_ReadOnly);

    if (vfsOpen(vfs, _S"/new.txt", FS_Write | FS_Create))
        TEST_FAILV(ret, 1, _SL("opened a file for writing on a read-only mount"), stvNone);
    if (vfsDelete(vfs, _S"/a.txt"))
        TEST_FAILV(ret, 1, _SL("deleted a file on a read-only mount"), stvNone);

    // reads still work
    checkContents(&ret, vfs, _S"/a.txt", _S"a:1");

    objRelease(&prov);
    vfsDestroy(&vfs);
    return ret;
}

static int test_vfs_search()
{
    int ret           = 0;
    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* prov = sampleProvider(VFS_CaseSensitive, _S"1");

    vfsMountProvider(vfs, prov, _S"/");

    checkSearch(&ret, vfs, _S"/", NULL, 0, _S"a.txt,sub");
    checkSearch(&ret, vfs, _S"/sub", NULL, 0, _S"b.txt,deep");
    checkSearch(&ret, vfs, _S"/sub/deep", NULL, 0, _S"c.txt");

    // pattern and type filters
    checkSearch(&ret, vfs, _S"/sub", _S"*.txt", 0, _S"b.txt");
    checkSearch(&ret, vfs, _S"/sub", NULL, FS_File, _S"b.txt");
    checkSearch(&ret, vfs, _S"/sub", NULL, FS_Directory, _S"deep");

    objRelease(&prov);
    vfsDestroy(&vfs);
    return ret;
}

static int test_vfs_mount()
{
    int ret            = 0;
    VFS* vfs           = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* root  = sampleProvider(VFS_CaseSensitive, _S"root");
    VFSTestProv* inner = sampleProvider(VFS_CaseSensitive, _S"inner");
    VFSTestProv* nsp   = sampleProvider(VFS_CaseSensitive, _S"ns");

    vfsMountProvider(vfs, root, _S"/");
    vfsMountProvider(vfs, inner, _S"/mnt");
    vfsMountProvider(vfs, nsp, _S"z:/");

    checkContents(&ret, vfs, _S"/a.txt", _S"a:root");
    checkContents(&ret, vfs, _S"/mnt/a.txt", _S"a:inner");
    checkContents(&ret, vfs, _S"z:/a.txt", _S"a:ns");
    checkStat(&ret, vfs, _S"y:/a.txt", FS_Nonexistent);

    if (!vfsUnmount(vfs, _S"/mnt"))
        TEST_FAILV(ret, 1, _SL("could not unmount /mnt"), stvNone);
    checkStat(&ret, vfs, _S"/mnt/a.txt", FS_Nonexistent);
    // the root mount is untouched
    checkContents(&ret, vfs, _S"/a.txt", _S"a:root");

    if (!vfsUnmount(vfs, _S"z:/"))
        TEST_FAILV(ret, 1, _SL("could not unmount z:/"), stvNone);
    checkStat(&ret, vfs, _S"z:/a.txt", FS_Nonexistent);

    objRelease(&root);
    objRelease(&inner);
    objRelease(&nsp);
    vfsDestroy(&vfs);
    return ret;
}

static int test_vfs_curdir()
{
    int ret           = 0;
    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* prov = sampleProvider(VFS_CaseSensitive, _S"1");
    string cur = 0, abs = 0;

    vfsMountProvider(vfs, prov, _S"/");

    if (!vfsSetCurDir(vfs, _S"/sub"))
        TEST_FAILV(ret, 1, _SL("vfsSetCurDir(/sub) failed"), stvNone);

    vfsCurDir(vfs, &cur);
    if (!strEq(cur, _S"/sub"))
        TEST_FAILV(ret, 1, _SL("vfsCurDir == '${string}', wanted '/sub'"), stvar(strref, cur));

    vfsAbsolutePath(vfs, &abs, _S"b.txt");
    if (!strEq(abs, _S"/sub/b.txt")) {
        TEST_FAILV(ret, 1, _SL("vfsAbsolutePath('b.txt') == '${string}', wanted '/sub/b.txt'"),
                   stvar(strref, abs));
    }

    // relative paths resolve against it
    checkContents(&ret, vfs, _S"b.txt", _S"b:1");
    checkStat(&ret, vfs, _S"deep/c.txt", FS_File);

    strDestroy(&cur);
    strDestroy(&abs);
    objRelease(&prov);
    vfsDestroy(&vfs);
    return ret;
}

// A failed copy-on-write must leave the VFS usable. The COW layer here refuses to open anything
// for writing, which is the shape of a full disk or a read-only COW target.
static int test_vfs_cowfail()
{
    int ret            = 0;
    VFS* vfs           = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* lower = sampleProvider(VFS_CaseSensitive, _S"low");
    VFSTestProv* cow   = vfstestprovCreate(VFS_CaseSensitive);
    size_t written     = 0;

    cow->failmask = VFSTP_FailOpenWrite;
    vfsMountProvider(vfs, lower, _S"/", VFS_ReadOnly);
    vfsMountProvider(vfs, cow, _S"/", VFS_AlwaysCOW);

    VFSFile* f = vfsOpen(vfs, _S"/a.txt", FS_Write);
    if (!f) {
        TEST_FAILV(ret, 1, _SL("could not open /a.txt for COW writing"), stvNone);
    } else {
        if (vfsWrite(f, "x", 1, &written))
            TEST_FAILV(ret, 1, _SL("write succeeded despite a COW layer that cannot open files"), stvNone);
        vfsClose(f);
    }

    // the write failing is expected; the VFS still being usable afterwards is the point
    checkStat(&ret, vfs, _S"/a.txt", FS_File);
    checkContents(&ret, vfs, _S"/a.txt", _S"a:low");

    objRelease(&lower);
    objRelease(&cow);
    vfsDestroy(&vfs);
    return ret;
}

// A copy-on-write that succeeds puts the new copy on the COW layer and leaves the original alone.
static int test_vfs_cow()
{
    int ret            = 0;
    VFS* vfs           = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* lower = sampleProvider(VFS_CaseSensitive, _S"low");
    VFSTestProv* cow   = vfstestprovCreate(VFS_CaseSensitive);
    string got         = 0;

    vfsMountProvider(vfs, lower, _S"/", VFS_ReadOnly);
    vfsMountProvider(vfs, cow, _S"/", VFS_AlwaysCOW);

    VFSFile* f = vfsOpen(vfs, _S"/sub/b.txt", FS_Write);
    if (!f) {
        TEST_FAILV(ret, 1, _SL("could not open /sub/b.txt for COW writing"), stvNone);
    } else {
        if (!vfsWriteString(f, _S"NEW", NULL))
            TEST_FAILV(ret, 1, _SL("COW write failed"), stvNone);
        vfsClose(f);
    }

    // The copy landed on the COW layer, parent directories and all. The file was opened without
    // FS_Truncate, so the tail of the original ("b:low" overwritten by "NEW") has to still be
    // there -- which is what proves the lower layer's contents were copied up before the write.
    if (!htFind(cow->files, string, _S"sub/b.txt", string, &got))
        TEST_FAILV(ret, 1, _SL("COW layer has no sub/b.txt"), stvNone);
    else if (!strEq(got, _S"NEWow"))
        TEST_FAILV(ret, 1, _SL("COW copy contains '${string}', wanted 'NEWow'"), stvar(strref, got));

    // and reads through the VFS now see the COW layer's copy
    checkContents(&ret, vfs, _S"/sub/b.txt", _S"NEWow");

    // and the read-only original is untouched
    strDestroy(&got);
    if (htFind(lower->files, string, _S"sub/b.txt", string, &got) && !strEq(got, _S"b:low"))
        TEST_FAILV(ret, 1, _SL("read-only original was modified to '${string}'"), stvar(strref, got));

    strDestroy(&got);
    objRelease(&lower);
    objRelease(&cow);
    vfsDestroy(&vfs);
    return ret;
}

// Mounting a second provider invalidates the whole directory cache, which walks the subdirs
// hashtable of every cached node. Warm the cache with enough cache-only directories that the
// invalidation walk has plenty to remove while it is iterating.
static int test_vfs_invalidate()
{
    int ret           = 0;
    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* prov = sampleProvider(VFS_CaseSensitive, _S"1");
    VFSTestProv* p2   = vfstestprovCreate(VFS_CaseSensitive);
    string path       = 0;

    vfstestprovAddFile(p2, _S"second.txt", _S"2");
    vfsMountProvider(vfs, prov, _S"/");

    // every one of these creates a permanent cache-only VFSDir for a directory no provider has
    for (int i = 0; i < 400; i++) {
        strFormat(&path, _SL("/d${int}/f.txt"), stvar(int32, i));
        checkStat(&ret, vfs, path, FS_Nonexistent);
    }

    // ...which the mount below has to invalidate, all in one traversal
    if (!vfsMountProvider(vfs, p2, _S"/"))
        TEST_FAILV(ret, 1, _SL("could not mount the second provider"), stvNone);

    checkContents(&ret, vfs, _S"/second.txt", _S"2");
    checkContents(&ret, vfs, _S"/a.txt", _S"a:1");
    checkStat(&ret, vfs, _S"/d1/f.txt", FS_Nonexistent);

    strDestroy(&path);
    objRelease(&prov);
    objRelease(&p2);
    vfsDestroy(&vfs);
    return ret;
}

// FS_Write | FS_Create must land on the layer the caller marked VFS_NewFiles, not simply on the
// first writable layer the search happens to reach.
static int test_vfs_newfiles()
{
    int ret           = 0;
    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* newf = vfstestprovCreate(VFS_CaseSensitive);
    VFSTestProv* base = sampleProvider(VFS_CaseSensitive, _S"base");

    // newf is mounted lower, so it is only chosen because of VFS_NewFiles
    vfsMountProvider(vfs, newf, _S"/", VFS_NewFiles);
    vfsMountProvider(vfs, base, _S"/");

    VFSFile* f = vfsOpen(vfs, _S"/brand-new.txt", FS_Write | FS_Create);
    if (!f) {
        TEST_FAILV(ret, 1, _SL("could not create /brand-new.txt"), stvNone);
    } else {
        vfsWriteString(f, _S"n", NULL);
        vfsClose(f);
    }

    if (!htHasKey(newf->files, string, _S"brand-new.txt"))
        TEST_FAILV(ret, 1, _SL("new file did not land on the VFS_NewFiles layer"), stvNone);
    if (htHasKey(base->files, string, _S"brand-new.txt"))
        TEST_FAILV(ret, 1, _SL("new file landed on the plain writable layer"), stvNone);

    objRelease(&newf);
    objRelease(&base);
    vfsDestroy(&vfs);
    return ret;
}

// A rename through a single provider has to invalidate the cache entry for the old name, the
// same way a delete does.
static int test_vfs_rename()
{
    int ret           = 0;
    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* prov = sampleProvider(VFS_CaseSensitive, _S"1");

    vfsMountProvider(vfs, prov, _S"/");

    // warm the cache for both names
    checkStat(&ret, vfs, _S"/a.txt", FS_File);
    checkStat(&ret, vfs, _S"/renamed.txt", FS_Nonexistent);

    if (!vfsRename(vfs, _S"/a.txt", _S"/renamed.txt"))
        TEST_FAILV(ret, 1, _SL("vfsRename failed"), stvNone);

    checkStat(&ret, vfs, _S"/renamed.txt", FS_File);
    checkStat(&ret, vfs, _S"/a.txt", FS_Nonexistent);
    checkContents(&ret, vfs, _S"/renamed.txt", _S"a:1");

    // renaming onto an existing name is refused
    if (vfsRename(vfs, _S"/renamed.txt", _S"/sub/b.txt"))
        TEST_FAILV(ret, 1, _SL("vfsRename overwrote an existing file"), stvNone);

    objRelease(&prov);
    vfsDestroy(&vfs);
    return ret;
}

// Errors that a caller can reach on demand: searching an unknown namespace, and a provider whose
// close fails. Run under LSan to also cover the allocations the failing paths have to release.
static int test_vfs_errors()
{
    int ret           = 0;
    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* prov = sampleProvider(VFS_CaseSensitive, _S"1");
    FSSearchIter iter;

    vfsMountProvider(vfs, prov, _S"/");

    // no such namespace, so the search never gets as far as a provider
    if (vfsSearchInit(&iter, vfs, _S"z:/nope", NULL, 0, false))
        TEST_FAILV(ret, 1, _SL("search of an unknown namespace succeeded"), stvNone);
    vfsSearchFinish(&iter);

    // ...and a directory that no provider has
    if (vfsSearchInit(&iter, vfs, _S"/no/such/dir", NULL, 0, false))
        TEST_FAILV(ret, 1, _SL("search of a nonexistent directory succeeded"), stvNone);
    vfsSearchFinish(&iter);

    // a provider whose searchInit fails must not take the search down with it
    prov->failmask = VFSTP_FailSearchInit;
    if (vfsSearchInit(&iter, vfs, _S"/sub", NULL, 0, false))
        TEST_FAILV(ret, 1, _SL("search succeeded on a provider that cannot list"), stvNone);
    vfsSearchFinish(&iter);
    prov->failmask = 0;

    // a failed flush at close has to reach the caller
    prov->failmask = VFSTP_FailClose;
    VFSFile* f     = vfsOpen(vfs, _S"/a.txt", FS_Read);
    if (!f) {
        TEST_FAILV(ret, 1, _SL("could not open /a.txt"), stvNone);
    } else if (vfsClose(f)) {
        TEST_FAILV(ret, 1, _SL("vfsClose reported success on a provider whose close failed"), stvNone);
    }
    prov->failmask = 0;

    objRelease(&prov);
    vfsDestroy(&vfs);
    return ret;
}

// A case-insensitive VFS over a case-sensitive provider has to walk the provider's real
// directory entries to resolve a path. That is the only configuration in which the
// case-insensitive resolution helper runs, and until now nothing exercised it through a search.
static int test_vfs_caseinsens()
{
    int ret           = 0;
    VFS* vfs          = vfsCreate(0);   // case-insensitive
    VFSTestProv* prov = vfstestprovCreate(VFS_CaseSensitive);

    vfstestprovAddFile(prov, _S"A.txt", _S"a");
    vfstestprovAddFile(prov, _S"Sub/B.txt", _S"b");
    vfstestprovAddFile(prov, _S"Sub/Deep/C.txt", _S"c");
    vfsMountProvider(vfs, prov, _S"/");

    // paths resolve whatever case the caller spells them in
    checkStat(&ret, vfs, _S"/a.TXT", FS_File);
    checkContents(&ret, vfs, _S"/a.txt", _S"a");
    checkContents(&ret, vfs, _S"/SUB/b.txt", _S"b");
    checkContents(&ret, vfs, _S"/sub/deep/c.TXT", _S"c");

    // and so do searches, which report the provider's real casing
    checkSearch(&ret, vfs, _S"/sub", NULL, 0, _S"B.txt,Deep");
    checkSearch(&ret, vfs, _S"/SUB/deep", NULL, 0, _S"C.txt");

    // Caching what a search found must not attribute one directory's files to another: after
    // enumerating /sub, the provider's root-level A.txt must not have become /sub/A.txt.
    checkStat(&ret, vfs, _S"/sub/A.txt", FS_Nonexistent);
    checkStat(&ret, vfs, _S"/sub/a.txt", FS_Nonexistent);

    objRelease(&prov);
    vfsDestroy(&vfs);
    return ret;
}

// Mounting a VFS into itself is a supported (and, per vfsDestroy, common) configuration, so no
// VFS operation may hold a lock across a call into a provider.
static int test_vfs_loopback()
{
    int ret           = 0;
    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* prov = sampleProvider(VFS_CaseSensitive, _S"1");

    vfsMountProvider(vfs, prov, _S"/");
    if (!vfsMountVFS(vfs, _S"/loop", vfs, _S"/"))
        TEST_FAILV(ret, 1, _SL("could not mount the VFS into itself"), stvNone);

    checkStat(&ret, vfs, _S"/loop/sub/b.txt", FS_File);
    checkContents(&ret, vfs, _S"/loop/a.txt", _S"a:1");
    checkContents(&ret, vfs, _S"/loop/sub/deep/c.txt", _S"c:1");

    // /loop mirrors /, so it contains the loop mount point itself
    checkSearch(&ret, vfs, _S"/loop", NULL, 0, _S"a.txt,loop,sub");
    checkSearch(&ret, vfs, _S"/loop/sub", NULL, 0, _S"b.txt,deep");
    checkContents(&ret, vfs, _S"/loop/loop/a.txt", _S"a:1");

    objRelease(&prov);
    vfsDestroy(&vfs);
    return ret;
}

// Two providers at one mount point, only one of which reports itself case-sensitive. The
// corrected on-disk casing found for one must not be handed to the other -- a VFSVFS always
// reports case-insensitive no matter what the VFS behind it does, which is exactly the mismatch.
static int test_vfs_casemix()
{
    int ret            = 0;
    VFS* vfs           = vfsCreate(0);   // case-insensitive
    VFS* inner         = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* ip    = vfstestprovCreate(VFS_CaseSensitive);
    VFSTestProv* upper = vfstestprovCreate(VFS_CaseSensitive);

    vfstestprovAddFile(ip, _S"dir/y.txt", _S"y");
    vfsMountProvider(inner, ip, _S"/");

    vfstestprovAddFile(upper, _S"DIR/x.txt", _S"x");

    // the loopback VFS is mounted lower, so the upper provider's casing is resolved first
    vfsMountVFS(vfs, _S"/", inner, _S"/");
    vfsMountProvider(vfs, upper, _S"/");

    checkSearch(&ret, vfs, _S"/dir", NULL, 0, _S"x.txt,y.txt");

    objRelease(&ip);
    objRelease(&upper);
    vfsDestroy(&inner);
    vfsDestroy(&vfs);
    return ret;
}

// Mount points are entries in their parent directory's listing, so they have to behave like any
// other entry: they make the parent exist, they honour the caller's pattern and type filter, and
// they carry a real stat when one was asked for.
static int test_vfs_mountents()
{
    int ret            = 0;
    VFS* vfs           = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* inner = sampleProvider(VFS_CaseSensitive, _S"inner");
    VFSTestProv* root  = vfstestprovCreate(VFS_CaseSensitive);
    FSSearchIter iter;

    // nothing is mounted at /, so / exists only as the parent of the mount point below it
    vfsMountProvider(vfs, inner, _S"/mnt");
    checkSearch(&ret, vfs, _S"/", NULL, 0, _S"mnt");
    // and vfsStat has to agree: a bare mount point is a directory even with nothing mounted
    // above it to serve it as an entry of its own parent
    checkStat(&ret, vfs, _S"/mnt", FS_Directory);

    // now give / a provider with one file, so the two kinds of entry sit side by side
    vfstestprovAddFile(root, _S"x.txt", _S"x");
    vfsMountProvider(vfs, root, _S"/");

    checkSearch(&ret, vfs, _S"/", NULL, 0, _S"mnt,x.txt");
    // a mount point is a directory, so a file filter and a *.txt pattern both exclude it
    checkSearch(&ret, vfs, _S"/", _S"*.txt", 0, _S"x.txt");
    checkSearch(&ret, vfs, _S"/", NULL, FS_File, _S"x.txt");
    checkSearch(&ret, vfs, _S"/", NULL, FS_Directory, _S"mnt");
    checkSearch(&ret, vfs, _S"/", _S"m*", 0, _S"mnt");

    // and it reports real metadata when the caller asked for stat
    if (vfsSearchInit(&iter, vfs, _S"/", _S"mnt", 0, true)) {
        if (iter.stat.modified == 0)
            TEST_FAILV(ret, 1, _SL("mount point entry came back with a zeroed stat"), stvNone);
    } else {
        TEST_FAILV(ret, 1, _SL("could not search / for the mount point"), stvNone);
    }
    vfsSearchFinish(&iter);

    objRelease(&inner);
    objRelease(&root);
    vfsDestroy(&vfs);
    return ret;
}

// The directory tree grows with every path ever looked up, whether or not any provider has it,
// so it needs a bound. Eviction must free those nodes without losing anything a caller can
// still reach through a provider.
static int test_vfs_evict()
{
    int ret           = 0;
    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* prov = sampleProvider(VFS_CaseSensitive, _S"1");
    string path       = 0;

    vfsMountProvider(vfs, prov, _S"/");

    // warm the real tree, which must survive everything below
    checkContents(&ret, vfs, _S"/sub/deep/c.txt", _S"c:1");

    // flood it with directories no provider has, under limits that make everything untouched
    // stale immediately
    vfsSetCacheLimits(vfs, 64, 1);
    for (int i = 0; i < 400; i++) {
        strFormat(&path, _SL("/junk${int}/f.txt"), stvar(int32, i));
        checkStat(&ret, vfs, path, FS_Nonexistent);
    }

    // the tree stayed bounded on its own, without the caller asking
    uint32 flooded = atomicLoad(uint32, &vfs->dcache.dircount, Relaxed);
    if (flooded >= 400) {
        TEST_FAILV(ret, 1, _SL("400 lookups of nonexistent paths left ${uint} directory nodes"),
                   stvar(uint32, flooded));
    }

    // and an explicit prune clears out what is left of them
    vfsPruneCache(vfs);
    uint32 pruned = atomicLoad(uint32, &vfs->dcache.dircount, Relaxed);
    if (pruned >= flooded) {
        TEST_FAILV(ret, 1, _SL("prune left ${uint} of ${uint} directory nodes"),
                   stvar(uint32, pruned), stvar(uint32, flooded));
    }

    // nothing a provider still has became unreachable
    checkContents(&ret, vfs, _S"/a.txt", _S"a:1");
    checkContents(&ret, vfs, _S"/sub/deep/c.txt", _S"c:1");
    checkSearch(&ret, vfs, _S"/sub", NULL, 0, _S"b.txt,deep");
    checkStat(&ret, vfs, _S"/junk1/f.txt", FS_Nonexistent);

    // with the limit back up, nothing is dropped at all
    vfsSetCacheLimits(vfs, 0, 0);
    uint32 before = atomicLoad(uint32, &vfs->dcache.dircount, Relaxed);
    for (int i = 0; i < 100; i++) {
        strFormat(&path, _SL("/keep${int}/f.txt"), stvar(int32, i));
        checkStat(&ret, vfs, path, FS_Nonexistent);
    }
    uint32 kept = atomicLoad(uint32, &vfs->dcache.dircount, Relaxed);
    if (kept != before + 100) {
        TEST_FAILV(ret, 1, _SL("expected ${uint} directory nodes under the default limit, got ${uint}"),
                   stvar(uint32, before + 100), stvar(uint32, kept));
    }

    strDestroy(&path);
    objRelease(&prov);
    vfsDestroy(&vfs);
    return ret;
}

// The one test that goes through VFSFS to the real filesystem, so the OS provider itself stays
// covered. Everything else uses the memory provider.
static int test_vfs_fsprov()
{
    int ret = 0;
    string cwd = 0, dir = 0, file = 0;
    VFS* vfs = NULL;

    fsCurDir(&cwd);
    pathJoin(&dir, cwd, _S"cxvfstest");
    pathJoin(&file, dir, _S"f.txt");

    if (!fsCreateAll(dir)) {
        TEST_FAILV(ret, 1, _SL("could not create scratch directory '${string}'"), stvar(strref, dir));
        goto out;
    }

    FSFile* fh = fsOpen(file, FS_Overwrite);
    if (!fh) {
        TEST_FAILV(ret, 1, _SL("could not create '${string}'"), stvar(strref, file));
        goto out;
    }
    fsWrite(fh, "ondisk", 6, NULL);
    fsClose(fh);

    // Match what vfsCreateFromFS would pick, so the VFS and the provider agree on casing.
#if defined(_PLATFORM_UNIX)
    vfs = vfsCreate(VFS_CaseSensitive);
#else
    vfs = vfsCreate(0);
#endif
    if (!vfsMountFS(vfs, _S"/", dir)) {
        TEST_FAILV(ret, 1, _SL("could not mount '${string}'"), stvar(strref, dir));
        goto out;
    }

    checkStat(&ret, vfs, _S"/f.txt", FS_File);
    checkContents(&ret, vfs, _S"/f.txt", _S"ondisk");
    checkSearch(&ret, vfs, _S"/", NULL, 0, _S"f.txt");

out:
    if (vfs)
        vfsDestroy(&vfs);
    fsDelete(file);
    fsRemoveDir(dir);
    strDestroy(&cwd);
    strDestroy(&dir);
    strDestroy(&file);
    return ret;
}

testfunc vfstest_funcs[] = {
    { "basic",    test_vfs_basic    },
    { "write",    test_vfs_write    },
    { "layer",    test_vfs_layer    },
    { "opaque",   test_vfs_opaque   },
    { "readonly", test_vfs_readonly },
    { "search",   test_vfs_search   },
    { "mount",    test_vfs_mount    },
    { "curdir",     test_vfs_curdir     },
    { "invalidate", test_vfs_invalidate },
    { "cow",        test_vfs_cow        },
    { "cowfail",    test_vfs_cowfail    },
    { "newfiles",   test_vfs_newfiles   },
    { "rename",     test_vfs_rename     },
    { "errors",     test_vfs_errors     },
    { "caseinsens", test_vfs_caseinsens },
    { "loopback",   test_vfs_loopback   },
    { "casemix",    test_vfs_casemix    },
    { "mountents",  test_vfs_mountents  },
    { "evict",      test_vfs_evict      },
    { "fsprov",   test_vfs_fsprov   },
    { 0,          0                 }
};
