#include <cx/container.h>
#include <cx/fs.h>
#include <cx/json.h>
#include <cx/string.h>
#include <cx/sys.h>

DEFINE_ENTRY_POINT

static VFS* vfs;

static SSDNode* loadsrc(strref fname)
{
    VFSFile* rf = vfsOpen(vfs, fname, FS_Read);
    if (!rf)
        return NULL;

    StreamBuffer* sb = sbufCreate(1024);
    if (!sb || !sbufFilePRegisterPull(sb, rf, true))
        return NULL;

    SSDNode* inf = jsonParseTree(sb);
    sbufRelease(&sb);

    return inf;
}

static bool saveresult(strref fname, SSDNode* json)
{
    VFSFile* wf = vfsOpen(vfs, fname, FS_Overwrite);

    StreamBuffer* sb = sbufCreate(1024);
    if (!sb || !sbufFileCRegisterPush(sb, wf, true))
        return false;

    bool ret = jsonOutTree(sb, json, JSON_Pretty);
    sbufRelease(&sb);

    return ret;
}

static bool extractNodes(SSDNode* json, strref name, sa_SSDNode* out)
{
    SSDNode* sub = ssdSubtree(json, name, SSD_Create_None);

    if (sub) {
        ssdRemove(json, name);

        foreach (ssd, iter, idx, key, val, sub) {
            SSDNode* child = stvarObj(SSDNode, val);
            if (child) {
                saPush(out, object, child);
            }
        }

        objRelease(&sub);
        return true;
    }

    return false;
}

static void joinStr2(string* out, strref n1, strref sep, strref n2)
{
    if (strEmpty(n1))
        strDup(out, n2);
    else if (strEmpty(n2))
        strDup(out, n1);
    else
        strNConcat(out, n1, sep, n2);
}

static void mergeNode(SSDNode* dest, SSDNode* c2, bool isroot, SSDLockState* _ssdCurrentLockState)
{
    foreach (ssd, iter, idx, name, val, c2) {
        // skip these in the root, they have special handling
        if (isroot && (strEq(name, _S"name") || strEq(name, _S"displayName")))
            continue;

        SSDNode* child = stvarObj(SSDNode, val);
        bool overwrite = true;

        if (child && (ssdnodeIsHashtable(child) || ssdnodeIsArray(child))) {
            stvar temp;
            if (ssdnodeGet(dest, idx, name, &temp, _ssdCurrentLockState)) {
                SSDNode* sub = stvarObj(SSDNode, &temp);
                if (sub && ssdnodeIsHashtable(child) == ssdnodeIsHashtable(sub) &&
                    ssdnodeIsArray(child) == ssdnodeIsArray(sub)) {
                    mergeNode(sub, child, false, _ssdCurrentLockState);
                    overwrite = false;
                }
                stvarDestroy(&temp);
            }

            // source doesn't exist or is wrong type, overwrite it
            if (overwrite) {
                SSDNode* clone = ssdClone(child, dest->tree);
                ssdnodeSet(dest, idx, name, stvar(object, clone), _ssdCurrentLockState);
                objRelease(&clone);
                overwrite = false;
            }
        }

        if (overwrite) {
            ssdnodeSet(dest, idx, name, *val, _ssdCurrentLockState);
        }
    }
}

static SSDNode* mergeConfig(SSDNode* c1, SSDNode* c2, SSDLockState* _ssdCurrentLockState)
{
    bool success = false;
    SSDNode* ret = ssdClone(c1, c1->tree);
    if (!ret)
        return NULL;

    string n1 = 0, n2 = 0, temp = 0;
    if (!ssdStringOut(c1, _S"name", &n1) || !ssdStringOut(c2, _S"name", &n2))
        goto out;
    joinStr2(&temp, n1, _S"-", n2);
    ssdSet(ret, _S"name", false, stvar(string, temp));

    ssdStringOutD(c1, _S"displayName", &n1, _S"");
    ssdStringOutD(c2, _S"displayName", &n2, _S"");
    joinStr2(&temp, n1, _S" ", n2);
    ssdSet(ret, _S"displayName", false, stvar(string, temp));

    // merge everything else
    mergeNode(ret, c2, true, _ssdCurrentLockState);

    success = true;
out:
    strDestroy(&n1);
    strDestroy(&n2);
    strDestroy(&temp);

    if (!success) {
        objRelease(&ret);
        return NULL;
    }

    return ret;
}

static SSDNode* mkConfigs(_Inout_ sa_SSDNode* fullconfigs, _In_ sa_SSDNode* compilers,
                          _In_ sa_SSDNode* configs)
{
    ssdLockedTransaction(compilers->a[0])
    {
        foreach (sarray, compileridx, SSDNode*, compiler, *compilers) {
            foreach (sarray, configidx, SSDNode*, config, *configs) {
                SSDNode* merged = mergeConfig(compiler, config, _ssdCurrentLockState);
                if (merged) {
                    saPushC(fullconfigs, object, &merged);
                }
            }
        }
    }
    return NULL;
}

static void addConfigs(_In_ SSDNode* json, _In_ sa_SSDNode* fullconfigs)
{
    foreach (sarray, fcidx, SSDNode*, fc, *fullconfigs) {
        ssdAppend(json, _S"configurePresets", true, stvar(object, fc));
    }
}

static void addBuilds(_In_ SSDNode* json, _In_ sa_SSDNode* fullconfigs)
{
    // for now we just create a 1:1 mapping of synthetic build presets
    string name = 0, dname = 0;
    foreach (sarray, fcidx, SSDNode*, fc, *fullconfigs) {
        ssdStringOut(fc, _S"name", &name);
        ssdStringOutD(fc, _S"displayName", &dname, name);

        SSDNode* build = ssdCreateCustom(SSD_Create_Hashtable, json->tree);
        ssdSet(build, _S"name", false, stvar(string, name));
        ssdSet(build, _S"displayName", false, stvar(string, dname));
        ssdSet(build, _S"configurePreset", false, stvar(string, name));

        ssdAppend(json, _S"buildPresets", true, stvar(object, build));
        objRelease(&build);
    }
    strDestroy(&name);
    strDestroy(&dname);
}

static void addTests(_In_ SSDNode* json, _In_ sa_SSDNode* fullconfigs, sa_SSDNode* tests)
{
    string name = 0, dname = 0;
    ssdLockedTransaction(fullconfigs->a[0])
    {
        foreach (sarray, fcidx, SSDNode*, fc, *fullconfigs) {
            // add a default test for each config that just runs everything
            ssdStringOut(fc, _S"name", &name);
            ssdStringOutD(fc, _S"displayName", &dname, name);

            SSDNode* build = ssdCreateCustom(SSD_Create_Hashtable, json->tree);
            ssdSet(build, _S"name", false, stvar(string, name));
            ssdSet(build, _S"displayName", false, stvar(string, dname));
            ssdSet(build, _S"configurePreset", false, stvar(string, name));

            ssdAppend(json, _S"testPresets", true, stvar(object, build));

            // if there are any custom tests defined, add them for each config
            foreach(sarray, testidx, SSDNode*, test, *tests) {
                SSDNode* merged = mergeConfig(build, test, _ssdCurrentLockState);
                if (merged) {
                    ssdAppend(json, _S"testPresets", true, stvar(object, merged));
                    objRelease(&merged);
                }
            }
            objRelease(&build);
        }
    }
    strDestroy(&name);
    strDestroy(&dname);
}

int entryPoint(void)
{
    if (saSize(cmdArgs) < 2) {
        puts("Usage: presetsmaker infile outfile");
        return 1;
    }

    vfs = vfsCreateFromFS();
    if (!vfs)
        return 1;

    SSDNode* json = loadsrc(cmdArgs.a[0]);
    if (!json) {
        puts("ERROR: Failed to load source file");
        return 1;
    }

    sa_SSDNode compilers, configs, tests, fullconfigs;
    saInit(&compilers, object, 16);
    saInit(&configs, object, 16);
    saInit(&tests, object, 16);
    saInit(&fullconfigs, object, 16);

    extractNodes(json, _S "compilers", &compilers);
    extractNodes(json, _S "configs", &configs);
    extractNodes(json, _S "tests", &tests);

    mkConfigs(&fullconfigs, &compilers, &configs);
    addConfigs(json, &fullconfigs);
    addBuilds(json, &fullconfigs);
    addTests(json, &fullconfigs, &tests);

    saveresult(cmdArgs.a[1], json);

    objRelease(&json);
    saDestroy(&compilers);
    saDestroy(&configs);
    saDestroy(&tests);
    saDestroy(&fullconfigs);

    return 0;
}
