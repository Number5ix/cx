#include "jsonparse.h"

#include <cx/ssdtree/ssdnodes.h>
#include <cx/ssdtree/ssdtree.h>
#include <cx/string.h>

typedef struct JSONTreeState {
    SSDLock lock;
    SSDNode *root;
    SSDNode *cur;
    sa_SSDNode nodestack;
    bool error;
} JSONTreeState;

static void setValDirect(JSONTreeState *jts, JSONParseContext *ctx, stvar val)
{
    switch (ctx->ctype) {
    case JSON_Object:
        ssdnodeSet(jts->cur, SSD_ByName, ctx->cdata.curKey, val, &jts->lock);
        break;
    case JSON_Array:
        ssdnodeSet(jts->cur, ctx->cdata.curIdx, NULL, val, &jts->lock);
        break;
    }
}

static void pushObject(JSONTreeState *jts, JSONParseContext *ctx)
{
    if (!jts->root) {
        jts->root = ssdCreateHashtable();
        jts->cur = objAcquire(jts->root);
    } else {
        SSDHashNode *node = ssdhashnodeCreate(jts->root->info);
        setValDirect(jts, ctx, stvar(object, node));
        saPushC(&jts->nodestack, object, &jts->cur);
        jts->cur = SSDNode(node);
    }
}

static void pushArray(JSONTreeState *jts, JSONParseContext *ctx)
{
    if (!jts->root) {
        jts->root = ssdCreateArray();
        jts->cur = objAcquire(jts->root);
    } else {
        SSDArrayNode *node = ssdarraynodeCreate(jts->root->info);
        setValDirect(jts, ctx, stvar(object, node));
        saPushC(&jts->nodestack, object, &jts->cur);
        jts->cur = SSDNode(node);
    }
}

static void popNode(JSONTreeState *jts)
{
    objRelease(&jts->cur);
    jts->cur = saPopPtr(&jts->nodestack);
}

static void setVal(JSONTreeState *jts, JSONParseContext *ctx, stvar val)
{
    if (!jts->root) {
        jts->root = ssdCreateSingle(val);
        jts->cur = jts->root;
    }

    devAssert(jts->cur);

    setValDirect(jts, ctx, val);
}

void jsonTreeCB(JSONParseEvent *ev, void *userdata)
{
    JSONTreeState *jts = (JSONTreeState *)userdata;
    switch (ev->etype) {
    case JSON_Object_Begin:
        pushObject(jts, ev->ctx);
        break;
    case JSON_Object_End:
        popNode(jts);
        break;
    case JSON_Array_Begin:
        pushArray(jts, ev->ctx);
        break;
    case JSON_Array_End:
        popNode(jts);
        break;
    case JSON_String:
        setVal(jts, ev->ctx, stvar(string, ev->edata.strData));
        break;
    case JSON_Int:
        setVal(jts, ev->ctx, stvar(int64, ev->edata.intData));
        break;
    case JSON_Float:
        setVal(jts, ev->ctx, stvar(float64, ev->edata.floatData));
        break;
    case JSON_True:
        setVal(jts, ev->ctx, stvar(bool, true));
        break;
    case JSON_False:
        setVal(jts, ev->ctx, stvar(bool, false));
        break;
    case JSON_Null:
        setVal(jts, ev->ctx, stvNone);
        break;
    case JSON_Error:
        jts->error = true;
        break;
    }
}

SSDNode *jsonParseTree(StreamBuffer *sb)
{
    SSDNode *ret = NULL;
    JSONTreeState jts = { 0 };
    saInit(&jts.nodestack, object, 8);
    ssdLockInit(&jts.lock);

    if (jsonParse(sb, jsonTreeCB, &jts) && ! jts.error) {
        ret = objAcquire(jts.root);
    }

    if (jts.root)
        ssdLockEnd(jts.root, &jts.lock);

    saDestroy(&jts.nodestack);
    objRelease(&jts.root);
    return ret;
}