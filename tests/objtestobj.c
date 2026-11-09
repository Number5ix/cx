// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "objtestobj.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include <cx/serialize/serreader.h>
#include <cx/serialize/serwriter.h>

_objfactory_guaranteed TestCls1* TestCls1_create()
{
    TestCls1* ret;
    ret = objInstCreate(TestCls1);
    objInstInit(ret);
    return ret;
}

int TestCls1_testfunc(_In_ TestCls1* self)
{
    return self->data;
}

_objfactory_guaranteed TestCls2* TestCls2_create()
{
    TestCls2* ret;
    ret = objInstCreate(TestCls2);
    objInstInit(ret);
    return ret;
}

_objfactory_guaranteed TestCls3* TestCls3_create()
{
    TestCls3* ret;
    ret = objInstCreate(TestCls3);
    objInstInit(ret);
    return ret;
}

int TestCls3_testfunc2(_In_ TestCls3* self)
{
    return self->data2;
}

_objfactory_guaranteed TestCls4* TestCls4_create()
{
    TestCls4* ret;
    ret = objInstCreate(TestCls4);
    objInstInit(ret);
    return ret;
}

extern int TestCls1_testfunc(_In_ TestCls1* self);   // parent
#define parent_testfunc() TestCls1_testfunc((TestCls1*)(self))
int TestCls4_testfunc(_In_ TestCls4* self)
{
    return self->data3;
}

extern int TestCls4_testfunc(_In_ TestCls4* self);   // parent
#undef parent_testfunc
#define parent_testfunc() TestCls4_testfunc((TestCls4*)(self))
int TestCls4a_testfunc(_In_ TestCls4a* self)
{
    return self->data4;
}

int TestCls4b_testfunc3(_In_ TestCls4b* self)
{
    return self->data5;
}

_objfactory_guaranteed TestCls5* TestCls5_create()
{
    TestCls5* ret;
    ret = objInstCreate(TestCls5);
    objInstInit(ret);
    return ret;
}

intptr TestCls5_cmp(_In_ TestCls5* self, TestCls5* other, uint32 flags)
{
    // Uncomment unless this function can compare different object classes
    devAssert(objClsInfo(self) == objClsInfo(other));

    return objDefaultCmp(self, other, flags);
}

_objfactory_guaranteed TestCls4b* TestCls4b_create()
{
    TestCls4b* ret;
    ret = objInstCreate(TestCls4b);
    objInstInit(ret);
    return ret;
}

_objinit_guaranteed bool StrRefCls_init(_In_ StrRefCls* self)
{
    // Autogen begins -----
    structInit(TestStrP, &self->substr);
    return true;
    // Autogen ends -------
}

void StrRefCls_destroy(_In_ StrRefCls* self)
{
    // Autogen begins -----
    structDestroyMembers(&self->substr);
    structDestroy(&self->strp);
    // Autogen ends -------
}

_objfactory_guaranteed SerCls1* SerCls1_create()
{
    SerCls1* self;
    self = objInstCreate(SerCls1);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

void SerCls1_destroy(_In_ SerCls1* self)
{
    // Autogen begins -----
    strDestroy(&self->title);
    strDestroy(&self->scratch);
    // Autogen ends -------
}

_objfactory_guaranteed SerCls2* SerCls2_create()
{
    SerCls2* self;
    self = objInstCreate(SerCls2);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

_objinit_guaranteed bool SerCls2_init(_In_ SerCls2* self)
{
    // Autogen begins -----
    saInit(&self->nums, int32, 1);
    structInit(TestStr1, &self->sub);
    return true;
    // Autogen ends -------
}

void SerCls2_destroy(_In_ SerCls2* self)
{
    // Autogen begins -----
    saDestroy(&self->nums);
    structDestroyMembers(&self->sub);
    // Autogen ends -------
}

_objfactory_guaranteed SerPlain* SerPlain_create()
{
    SerPlain* self;
    self = objInstCreate(SerPlain);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

_objfactory_guaranteed SerCls3* SerCls3_create()
{
    SerCls3* self;
    self = objInstCreate(SerCls3);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

void SerCls3_destroy(_In_ SerCls3* self)
{
    // Autogen begins -----
    strDestroy(&self->leaf);
    // Autogen ends -------
}

_objfactory_guaranteed SerHolder* SerHolder_create()
{
    SerHolder* self;
    self = objInstCreate(SerHolder);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

_objinit_guaranteed bool SerHolder_init(_In_ SerHolder* self)
{
    // Autogen begins -----
    saInit(&self->kids, object, 1);
    return true;
    // Autogen ends -------
}

void SerHolder_destroy(_In_ SerHolder* self)
{
    // Autogen begins -----
    objRelease(&self->child);
    objRelease(&self->anyobj);
    saDestroy(&self->kids);
    // Autogen ends -------
}

_objfactory_guaranteed SerCycle* SerCycle_create()
{
    SerCycle* self;
    self = objInstCreate(SerCycle);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

void SerCycle_destroy(_In_ SerCycle* self)
{
    // Autogen begins -----
    strDestroy(&self->tag);
    objRelease(&self->next);
    // Autogen ends -------
}

_objfactory_guaranteed SerCustom* SerCustom_create()
{
    SerCustom* self;
    self = objInstCreate(SerCustom);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

bool SerCustom_serialize(_In_ SerCustom* self, SerWriter* w)
{
    return serArrBegin(w, 2) && serWriteInt(w, self->magic, stType(int32)) &&
           serWriteStr(w, self->label) && serArrEnd(w);
}

bool SerCustom_deserialize(_In_ SerCustom* self, SerReader* r)
{
    int32 count;
    if (!serArrBeginR(r, &count) || !serArrNext(r))
        return false;

    int64 magic;
    if (!serReadInt(r, &magic, stType(int32)))
        return false;
    self->magic = (int32)magic;

    if (!serArrNext(r))
        return serReaderFail(r, SER_Err_Data, _SL("a SerCustom is a magic and a label"));

    strDestroy(&self->label);
    return serReadStr(r, &self->label) && serArrEndR(r);
}

void SerCustom_destroy(_In_ SerCustom* self)
{
    // Autogen begins -----
    strDestroy(&self->label);
    // Autogen ends -------
}

_objfactory_guaranteed SerAny* SerAny_create()
{
    SerAny* self;
    self = objInstCreate(SerAny);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

void SerAny_destroy(_In_ SerAny* self)
{
    // Autogen begins -----
    objRelease(&self->one);
    objRelease(&self->two);
    // Autogen ends -------
}

_objfactory_guaranteed SerRenamed* SerRenamed_create()
{
    SerRenamed* self;
    self = objInstCreate(SerRenamed);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

void SerRenamed_destroy(_In_ SerRenamed* self)
{
    // Autogen begins -----
    strDestroy(&self->category);
    // Autogen ends -------
}

_objfactory_guaranteed SerSetHolder* SerSetHolder_create()
{
    SerSetHolder* self;
    self = objInstCreate(SerSetHolder);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

_objinit_guaranteed bool SerSetHolder_init(_In_ SerSetHolder* self)
{
    // Autogen begins -----
    saInit(&self->items, object, 1);
    return true;
    // Autogen ends -------
}

void SerSetHolder_destroy(_In_ SerSetHolder* self)
{
    // Autogen begins -----
    objRelease(&self->item);
    saDestroy(&self->items);
    // Autogen ends -------
}

_objfactory_guaranteed PropBase* PropBase_create()
{
    PropBase* self;
    self = objInstCreate(PropBase);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

int32 PropBase_num(_In_ PropBase* self)
{
    return self->vnum;
}

void PropBase_setNum(_In_ PropBase* self, int32 v)
{
    self->vnum = v;
    self->vversion++;
}

bool PropBase_flag(_In_ PropBase* self)
{
    return self->vflag;
}

void PropBase_setFlag(_In_ PropBase* self, bool v)
{
    self->vflag = v;
    self->vversion++;
}

float32 PropBase_f32(_In_ PropBase* self)
{
    return self->vf32;
}

void PropBase_setF32(_In_ PropBase* self, float32 v)
{
    self->vf32 = v;
    self->vversion++;
}

float64 PropBase_ratio(_In_ PropBase* self)
{
    return self->vratio;
}

void PropBase_setRatio(_In_ PropBase* self, float64 v)
{
    self->vratio = v;
    self->vversion++;
}

int64 PropBase_big(_In_ PropBase* self)
{
    return self->vbig;
}

void PropBase_setBig(_In_ PropBase* self, int64 v)
{
    self->vbig = v;
    self->vversion++;
}

uint8 PropBase_small(_In_ PropBase* self)
{
    return self->vsmall;
}

void PropBase_setSmall(_In_ PropBase* self, uint8 v)
{
    self->vsmall = v;
    self->vversion++;
}

size_t PropBase_size(_In_ PropBase* self)
{
    return self->vsize;
}

void PropBase_setSize(_In_ PropBase* self, size_t v)
{
    self->vsize = v;
    self->vversion++;
}

int32 PropBase_mode(_In_ PropBase* self)
{
    return self->vmode;
}

void PropBase_setMode(_In_ PropBase* self, int32 v)
{
    self->vmode = v;
    self->vversion++;
}

PropBase* PropBase_peer(_In_ PropBase* self)
{
    return self->vpeer;
}

void PropBase_setPeer(_In_ PropBase* self, _In_opt_ PropBase* p)
{
    objRelease(&self->vpeer);
    self->vpeer = objAcquire(p);
    self->vversion++;
}

int32 PropBase_limit(_In_ PropBase* self)
{
    return self->vlimit;
}

bool PropBase_setLimit(_In_ PropBase* self, int32 v)
{
    if (v > 100)
        return false;
    self->vlimit = v;
    self->vversion++;
    return true;
}

int32 PropBase_version(_In_ PropBase* self)
{
    return self->vversion;
}

int32 PropBase_tag(_In_ PropBase* self)
{
    return self->vtag;
}

void PropBase_setTag(_In_ PropBase* self, int32 v)
{
    self->vtag = v;
}

strref PropBase_label(_In_ PropBase* self)
{
    return self->vlabel;
}

void PropBase_setLabel(_In_ PropBase* self, _In_opt_ strref label)
{
    strDup(&self->vlabel, label);
    self->vversion++;
}

void PropBase_destroy(_In_ PropBase* self)
{
    // Autogen begins -----
    strDestroy(&self->vlabel);
    objRelease(&self->vpeer);
    // Autogen ends -------
}

_objfactory_guaranteed PropChild* PropChild_create()
{
    PropChild* self;
    self = objInstCreate(PropChild);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

int32 PropChild_extra(_In_ PropChild* self)
{
    return self->vextra;
}

void PropChild_setExtra(_In_ PropChild* self, int32 v)
{
    self->vextra = v;
}

extern void PropBase_setNum(_In_ PropBase* self, int32 v);   // parent
#define parent_setNum(v) PropBase_setNum((PropBase*)(self), v)
void PropChild_setNum(_In_ PropChild* self, int32 v)
{
    parent_setNum(v * 2);
}

_objfactory_guaranteed PropOther* PropOther_create()
{
    PropOther* self;
    self = objInstCreate(PropOther);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

strref PropOther_label(_In_ PropOther* self)
{
    return self->vlabel;
}

void PropOther_setLabel(_In_ PropOther* self, _In_opt_ strref label)
{
    strConcat(&self->vlabel, _S"other:", label);
}

void PropOther_destroy(_In_ PropOther* self)
{
    // Autogen begins -----
    strDestroy(&self->vlabel);
    // Autogen ends -------
}

_objfactory_guaranteed PropSquare* PropSquare_create()
{
    PropSquare* self;
    self = objInstCreate(PropSquare);

    // Insert any pre-initialization object construction here

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

int32 PropSquare_sides(_In_ PropSquare* self)
{
    return 4;
}

// Autogen begins -----
// clang-format off
#include "objtestobj.auto.inc"
// clang-format on
// Autogen ends -------
