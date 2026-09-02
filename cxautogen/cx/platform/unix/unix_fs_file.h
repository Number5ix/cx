#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
// clang-format off
#include <cx/obj.h>
#include <cx/struct.h>
#include <cx/fs/fileobj.h>

CX_C_BEGIN

typedef struct FSFileUnix FSFileUnix;
typedef struct FSFileUnix_WeakRef FSFileUnix_WeakRef;
saDeclarePtr(FSFileUnix);
saDeclarePtr(FSFileUnix_WeakRef);
#define _sti_FSFileUnix _sti_object
#define SType_FSFileUnix FSFileUnix*
#define STStorageType_FSFileUnix FSFileUnix*
#define STypeArg_FSFileUnix(type, val) stgeneric(object, (ObjInst*)objInstCheckClass(FSFileUnix, val))
#define STypeArgPtr_FSFileUnix(type, val) (stgeneric*)objInstCheckClassPtr(FSFileUnix, val)
#define STypeCheckedArg_FSFileUnix(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_FSFileUnix(type, val) stType(type), stArgPtr(type, val)

typedef struct FSFileUnix_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    bool (*close)(_In_ void* self);
    bool (*read)(_In_ void* self, _Out_writes_bytes_to_(sz, *bytesread) void* buf, size_t sz, _Out_ _Deref_out_range_(0, sz) size_t* bytesread);
    bool (*write)(_In_ void* self, _In_reads_bytes_(sz) const void* buf, size_t sz, _Out_opt_ _Deref_out_range_(0, sz) size_t* byteswritten);
    int64 (*tell)(_In_ void* self);
    int64 (*seek)(_In_ void* self, int64 off, FSSeekType seektype);
    bool (*flush)(_In_ void* self);
} FSFileUnix_ClassIf;
extern FSFileUnix_ClassIf FSFileUnix_ClassIf_tmpl;

typedef struct FSFileUnix {
    union {
        FSFileUnix_ClassIf* _;
        void* _is_FSFileUnix;
        void* _is_File;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(uintptr) _ref;
    atomic(ptr) _weakref;

    int fd;
    bool locked;
} FSFileUnix;
extern ObjClassInfo FSFileUnix_clsinfo;
#define FSFileUnix(inst) objInstCheckClass(FSFileUnix, inst)
#define FSFileUnixNone ((FSFileUnix*)NULL)

typedef struct FSFileUnix_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_FSFileUnix_WeakRef;
        void* _is_File_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(uintptr) _ref;
    RWLock _lock;
} FSFileUnix_WeakRef;
#define FSFileUnix_WeakRef(inst) objWeakRefCheckClass(FSFileUnix, inst)

_objfactory_guaranteed FSFileUnix* FSFileUnix_create(int fd, bool locked);
// FSFileUnix* fsfileunixCreate(int fd, bool locked);
#define fsfileunixCreate(fd, locked) FSFileUnix_create(fd, locked)

// bool fsfileunixWriteString(FSFileUnix* self, strref str, size_t* byteswritten);
#define fsfileunixWriteString(self, str, byteswritten) File_writeString(File(self), str, byteswritten)

// bool fsfileunixClose(FSFileUnix* self);
#define fsfileunixClose(self) ((self) ? ((self)->_->close(FSFileUnix(self))) : false)
// bool fsfileunixRead(FSFileUnix* self, void* buf, size_t sz, size_t* bytesread);
#define fsfileunixRead(self, buf, sz, bytesread) ((self) ? ((self)->_->read(FSFileUnix(self), buf, sz, bytesread)) : false)
// bool fsfileunixWrite(FSFileUnix* self, const void* buf, size_t sz, size_t* byteswritten);
#define fsfileunixWrite(self, buf, sz, byteswritten) ((self) ? ((self)->_->write(FSFileUnix(self), buf, sz, byteswritten)) : false)
// int64 fsfileunixTell(FSFileUnix* self);
#define fsfileunixTell(self) ((self) ? ((self)->_->tell(FSFileUnix(self))) : -1)
// int64 fsfileunixSeek(FSFileUnix* self, int64 off, FSSeekType seektype);
#define fsfileunixSeek(self, off, seektype) ((self) ? ((self)->_->seek(FSFileUnix(self), off, seektype)) : -1)
// bool fsfileunixFlush(FSFileUnix* self);
#define fsfileunixFlush(self) ((self) ? ((self)->_->flush(FSFileUnix(self))) : false)

CX_C_END
