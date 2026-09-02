#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
// clang-format off
#include <cx/obj.h>
#include <cx/struct.h>
#include <cx/fs/fileobj.h>
#include <cx/platform/win.h>

CX_C_BEGIN

typedef struct FSFileWin FSFileWin;
typedef struct FSFileWin_WeakRef FSFileWin_WeakRef;
saDeclarePtr(FSFileWin);
saDeclarePtr(FSFileWin_WeakRef);
#define _sti_FSFileWin _sti_object
#define SType_FSFileWin FSFileWin*
#define STStorageType_FSFileWin FSFileWin*
#define STypeArg_FSFileWin(type, val) stgeneric(object, (ObjInst*)objInstCheckClass(FSFileWin, val))
#define STypeArgPtr_FSFileWin(type, val) (stgeneric*)objInstCheckClassPtr(FSFileWin, val)
#define STypeCheckedArg_FSFileWin(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_FSFileWin(type, val) stType(type), stArgPtr(type, val)

typedef struct FSFileWin_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    bool (*close)(_In_ void* self);
    bool (*read)(_In_ void* self, _Out_writes_bytes_to_(sz, *bytesread) void* buf, size_t sz, _Out_ _Deref_out_range_(0, sz) size_t* bytesread);
    bool (*write)(_In_ void* self, _In_reads_bytes_(sz) const void* buf, size_t sz, _Out_opt_ _Deref_out_range_(0, sz) size_t* byteswritten);
    int64 (*tell)(_In_ void* self);
    int64 (*seek)(_In_ void* self, int64 off, FSSeekType seektype);
    bool (*flush)(_In_ void* self);
} FSFileWin_ClassIf;
extern FSFileWin_ClassIf FSFileWin_ClassIf_tmpl;

typedef struct FSFileWin {
    union {
        FSFileWin_ClassIf* _;
        void* _is_FSFileWin;
        void* _is_File;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(uintptr) _ref;
    atomic(ptr) _weakref;

    HANDLE h;
} FSFileWin;
extern ObjClassInfo FSFileWin_clsinfo;
#define FSFileWin(inst) objInstCheckClass(FSFileWin, inst)
#define FSFileWinNone ((FSFileWin*)NULL)

typedef struct FSFileWin_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_FSFileWin_WeakRef;
        void* _is_File_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(uintptr) _ref;
    RWLock _lock;
} FSFileWin_WeakRef;
#define FSFileWin_WeakRef(inst) objWeakRefCheckClass(FSFileWin, inst)

_objfactory_guaranteed FSFileWin* FSFileWin_create(HANDLE h);
// FSFileWin* fsfilewinCreate(HANDLE h);
#define fsfilewinCreate(h) FSFileWin_create(h)

// bool fsfilewinWriteString(FSFileWin* self, strref str, size_t* byteswritten);
#define fsfilewinWriteString(self, str, byteswritten) File_writeString(File(self), str, byteswritten)

// bool fsfilewinClose(FSFileWin* self);
#define fsfilewinClose(self) ((self) ? ((self)->_->close(FSFileWin(self))) : false)
// bool fsfilewinRead(FSFileWin* self, void* buf, size_t sz, size_t* bytesread);
#define fsfilewinRead(self, buf, sz, bytesread) ((self) ? ((self)->_->read(FSFileWin(self), buf, sz, bytesread)) : false)
// bool fsfilewinWrite(FSFileWin* self, const void* buf, size_t sz, size_t* byteswritten);
#define fsfilewinWrite(self, buf, sz, byteswritten) ((self) ? ((self)->_->write(FSFileWin(self), buf, sz, byteswritten)) : false)
// int64 fsfilewinTell(FSFileWin* self);
#define fsfilewinTell(self) ((self) ? ((self)->_->tell(FSFileWin(self))) : -1)
// int64 fsfilewinSeek(FSFileWin* self, int64 off, FSSeekType seektype);
#define fsfilewinSeek(self, off, seektype) ((self) ? ((self)->_->seek(FSFileWin(self), off, seektype)) : -1)
// bool fsfilewinFlush(FSFileWin* self);
#define fsfilewinFlush(self) ((self) ? ((self)->_->flush(FSFileWin(self))) : false)

CX_C_END
