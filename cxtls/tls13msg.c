#include "tls13_private.h"
#include "tls_private.h"

#undef LOG_CHANNEL
#define LOG_CHANNEL TlsLogChannel

// TLS 1.3 puts 0x0303 in legacy_version and negotiates the real version through the
// supported_versions extension, so both hellos carry this and nothing else.
#define TLS13_LEGACY_VERSION 0x0303

// ---------------------------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void _tls13RdInit(Tls13Rd* rd, const uint8* data, size_t len)
{
    rd->p   = data;
    rd->end = data + len;
    rd->bad = false;
}

_Use_decl_annotations_
size_t _tls13RdLeft(const Tls13Rd* rd)
{
    return rd->bad ? 0 : (size_t)(rd->end - rd->p);
}

_Use_decl_annotations_
const uint8* _tls13RdBytes(Tls13Rd* rd, size_t n)
{
    if (rd->bad || (size_t)(rd->end - rd->p) < n) {
        rd->bad = true;
        return NULL;
    }

    const uint8* p = rd->p;
    rd->p += n;
    return p;
}

_Use_decl_annotations_
uint8 _tls13Rd8(Tls13Rd* rd)
{
    const uint8* p = _tls13RdBytes(rd, 1);
    return p ? p[0] : 0;
}

_Use_decl_annotations_
uint16 _tls13Rd16(Tls13Rd* rd)
{
    const uint8* p = _tls13RdBytes(rd, 2);
    return p ? (uint16)(((uint16)p[0] << 8) | p[1]) : 0;
}

_Use_decl_annotations_
uint32 _tls13Rd24(Tls13Rd* rd)
{
    const uint8* p = _tls13RdBytes(rd, 3);
    return p ? (((uint32)p[0] << 16) | ((uint32)p[1] << 8) | p[2]) : 0;
}

_Use_decl_annotations_
uint32 _tls13Rd32(Tls13Rd* rd)
{
    const uint8* p = _tls13RdBytes(rd, 4);
    return p ? (((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) | p[3]) : 0;
}

_Use_decl_annotations_
uint64 _tls13Rd64(Tls13Rd* rd)
{
    const uint8* p = _tls13RdBytes(rd, 8);
    if (!p)
        return 0;

    uint64 v = 0;
    for (int i = 0; i < 8; i++)
        v = (v << 8) | p[i];
    return v;
}

// The three vector readers differ only in how wide the length prefix is.
#define TLS13_RDVEC_GEN(bits, rdlen)                              \
    _Use_decl_annotations_                                        \
    bool _tls13RdVec##bits(Tls13Rd* rd, Tls13Rd* sub)             \
    {                                                             \
        _tls13RdInit(sub, rd->p, 0);                              \
        size_t n       = rdlen(rd);                               \
        const uint8* p = _tls13RdBytes(rd, n);                    \
        if (!p) {                                                 \
            sub->bad = true;                                      \
            return false;                                         \
        }                                                         \
        _tls13RdInit(sub, p, n);                                  \
        return true;                                              \
    }

TLS13_RDVEC_GEN(8, _tls13Rd8)
TLS13_RDVEC_GEN(16, _tls13Rd16)
TLS13_RDVEC_GEN(24, _tls13Rd24)

// ---------------------------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void _tls13WrInit(Tls13Wr* wr, size_t hint)
{
    memset(wr, 0, sizeof(*wr));
    wr->buf = bufCreate(hint ? hint : 512);
}

_Use_decl_annotations_
void _tls13WrDestroy(Tls13Wr* wr)
{
    bufDestroy(&wr->buf);
    wr->len = 0;
    wr->bad = false;
}

// Make room for `n` more bytes and return where they go, or NULL if the writer has already failed.
static _Ret_maybenull_ uint8* tls13WrGrow(_Inout_ Tls13Wr* wr, size_t n)
{
    if (wr->bad || !wr->buf)
        return NULL;

    if (wr->len + n > wr->buf->sz) {
        size_t want = wr->buf->sz * 2;
        while (want < wr->len + n)
            want *= 2;
        bufResize(&wr->buf, want);
        if (!wr->buf) {
            wr->bad = true;
            return NULL;
        }
    }

    uint8* p = wr->buf->data + wr->len;
    wr->len += n;
    wr->buf->len = wr->len;
    return p;
}

_Use_decl_annotations_
void _tls13Wr8(Tls13Wr* wr, uint8 v)
{
    uint8* p = tls13WrGrow(wr, 1);
    if (p)
        p[0] = v;
}

_Use_decl_annotations_
void _tls13Wr16(Tls13Wr* wr, uint16 v)
{
    uint8* p = tls13WrGrow(wr, 2);
    if (p) {
        p[0] = (uint8)(v >> 8);
        p[1] = (uint8)v;
    }
}

_Use_decl_annotations_
void _tls13Wr24(Tls13Wr* wr, uint32 v)
{
    uint8* p = tls13WrGrow(wr, 3);
    if (p) {
        p[0] = (uint8)(v >> 16);
        p[1] = (uint8)(v >> 8);
        p[2] = (uint8)v;
    }
}

_Use_decl_annotations_
void _tls13Wr32(Tls13Wr* wr, uint32 v)
{
    uint8* p = tls13WrGrow(wr, 4);
    if (p) {
        p[0] = (uint8)(v >> 24);
        p[1] = (uint8)(v >> 16);
        p[2] = (uint8)(v >> 8);
        p[3] = (uint8)v;
    }
}

_Use_decl_annotations_
void _tls13Wr64(Tls13Wr* wr, uint64 v)
{
    uint8* p = tls13WrGrow(wr, 8);
    if (p) {
        for (int i = 0; i < 8; i++)
            p[i] = (uint8)(v >> (8 * (7 - i)));
    }
}

_Use_decl_annotations_
void _tls13WrBytes(Tls13Wr* wr, const uint8* data, size_t len)
{
    if (!len)
        return;

    uint8* p = tls13WrGrow(wr, len);
    if (p)
        memcpy(p, data, len);
}

// Open reserves the length prefix and returns the offset just past it; close backfills the prefix
// from how much has been written since. The mark is an offset rather than a pointer because the
// buffer can move under a resize in between.
#define TLS13_WRVEC_GEN(bits, nbytes, wrlen, maxlen)              \
    _Use_decl_annotations_                                        \
    size_t _tls13WrOpen##bits(Tls13Wr* wr)                        \
    {                                                             \
        wrlen(wr, 0);                                             \
        return wr->len;                                           \
    }                                                             \
                                                                  \
    _Use_decl_annotations_                                        \
    void _tls13WrClose##bits(Tls13Wr* wr, size_t mark)            \
    {                                                             \
        if (wr->bad || !wr->buf)                                  \
            return;                                               \
        size_t n = wr->len - mark;                                \
        if (n > (maxlen)) {                                       \
            wr->bad = true;                                       \
            return;                                               \
        }                                                         \
        uint8* p = wr->buf->data + mark - (nbytes);               \
        for (size_t i = 0; i < (nbytes); i++)                     \
            p[i] = (uint8)(n >> (8 * ((nbytes) - 1 - i)));        \
    }

TLS13_WRVEC_GEN(8, 1, _tls13Wr8, 0xff)
TLS13_WRVEC_GEN(16, 2, _tls13Wr16, 0xffff)
TLS13_WRVEC_GEN(24, 3, _tls13Wr24, 0xffffff)

_Use_decl_annotations_
Buffer _tls13WrTake(Tls13Wr* wr)
{
    if (wr->bad || !wr->buf) {
        _tls13WrDestroy(wr);
        return NULL;
    }

    Buffer b     = wr->buf;
    b->len       = wr->len;
    wr->buf      = NULL;
    wr->len      = 0;
    return b;
}

// ---------------------------------------------------------------------------------------------
// ClientHello / ServerHello
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
const Tls13Ext* _tls13HelloExt(const Tls13Hello* h, uint16 type)
{
    for (uint8 i = 0; i < h->nexts; i++) {
        if (h->exts[i].type == type)
            return &h->exts[i];
    }
    return NULL;
}

_Use_decl_annotations_
bool _tls13HelloAddExt(Tls13Hello* h, uint16 type, const uint8* data, uint16 len)
{
    if (h->nexts >= TLS13_MAX_EXTS)
        return false;

    h->exts[h->nexts].type = type;
    h->exts[h->nexts].len  = len;
    h->exts[h->nexts].data = data;
    h->nexts++;
    return true;
}

_Use_decl_annotations_
bool _tls13HelloParse(Tls13Hello* h, bool server, const uint8* msg, size_t len)
{
    memset(h, 0, sizeof(*h));

    Tls13Rd rd;
    _tls13RdInit(&rd, msg, len);

    uint8 type    = _tls13Rd8(&rd);
    uint32 bodyLen = _tls13Rd24(&rd);

    Tls13Rd body;
    _tls13RdInit(&body, rd.p, 0);
    const uint8* bp = _tls13RdBytes(&rd, bodyLen);
    if (!bp || type != (server ? TLS13_HS_SERVER_HELLO : TLS13_HS_CLIENT_HELLO))
        return false;

    // Trailing bytes after the declared body length mean the caller handed over more than one
    // message, which every caller here treats as malformed rather than silently ignoring.
    if (_tls13RdLeft(&rd) != 0)
        return false;

    _tls13RdInit(&body, bp, bodyLen);

    h->legacyVersion = _tls13Rd16(&body);

    const uint8* rnd = _tls13RdBytes(&body, 32);
    if (rnd)
        memcpy(h->random, rnd, 32);

    Tls13Rd sid;
    if (!_tls13RdVec8(&body, &sid) || _tls13RdLeft(&sid) > sizeof(h->sessionId))
        return false;
    h->sessionIdLen = (uint8)_tls13RdLeft(&sid);
    if (h->sessionIdLen)
        memcpy(h->sessionId, sid.p, h->sessionIdLen);

    if (server) {
        h->suites[0] = _tls13Rd16(&body);
        h->nsuites   = 1;

        // legacy_compression_method: a single byte, and TLS 1.3 permits only null compression.
        if (_tls13Rd8(&body) != 0)
            return false;
    } else {
        Tls13Rd cs;
        if (!_tls13RdVec16(&body, &cs))
            return false;
        while (_tls13RdLeft(&cs) >= 2) {
            uint16 id = _tls13Rd16(&cs);
            if (h->nsuites < TLS13_MAX_SUITES)
                h->suites[h->nsuites++] = id;
        }
        if (_tls13RdLeft(&cs) != 0)
            return false;

        // legacy_compression_methods: the one-entry list { null } that TLS 1.3 requires.
        Tls13Rd comp;
        if (!_tls13RdVec8(&body, &comp) || _tls13RdLeft(&comp) != 1 || comp.p[0] != 0)
            return false;
    }

    Tls13Rd exts;
    if (!_tls13RdVec16(&body, &exts))
        return false;

    while (_tls13RdLeft(&exts) > 0) {
        uint16 etype = _tls13Rd16(&exts);
        Tls13Rd edata;
        if (!_tls13RdVec16(&exts, &edata))
            return false;
        if (!_tls13HelloAddExt(h, etype, edata.p, (uint16)_tls13RdLeft(&edata)))
            return false;
    }

    // A hello with anything left over after its extensions is malformed.
    return !body.bad && !exts.bad && _tls13RdLeft(&body) == 0;
}

_Use_decl_annotations_
Buffer _tls13HelloEncode(const Tls13Hello* h, bool server)
{
    if (server && h->nsuites != 1)
        return NULL;
    if (h->sessionIdLen > sizeof(h->sessionId))
        return NULL;

    Tls13Wr wr;
    _tls13WrInit(&wr, 512);

    _tls13Wr8(&wr, server ? TLS13_HS_SERVER_HELLO : TLS13_HS_CLIENT_HELLO);
    size_t bodyMark = _tls13WrOpen24(&wr);

    _tls13Wr16(&wr, h->legacyVersion ? h->legacyVersion : TLS13_LEGACY_VERSION);
    _tls13WrBytes(&wr, h->random, sizeof(h->random));

    size_t sidMark = _tls13WrOpen8(&wr);
    _tls13WrBytes(&wr, h->sessionId, h->sessionIdLen);
    _tls13WrClose8(&wr, sidMark);

    if (server) {
        _tls13Wr16(&wr, h->suites[0]);
        _tls13Wr8(&wr, 0);   // legacy_compression_method: null
    } else {
        size_t csMark = _tls13WrOpen16(&wr);
        for (uint8 i = 0; i < h->nsuites; i++)
            _tls13Wr16(&wr, h->suites[i]);
        _tls13WrClose16(&wr, csMark);

        // legacy_compression_methods: the fixed one-entry { null } list
        _tls13Wr8(&wr, 1);
        _tls13Wr8(&wr, 0);
    }

    size_t extMark = _tls13WrOpen16(&wr);
    for (uint8 i = 0; i < h->nexts; i++) {
        _tls13Wr16(&wr, h->exts[i].type);
        size_t edMark = _tls13WrOpen16(&wr);
        _tls13WrBytes(&wr, h->exts[i].data, h->exts[i].len);
        _tls13WrClose16(&wr, edMark);
    }
    _tls13WrClose16(&wr, extMark);

    _tls13WrClose24(&wr, bodyMark);

    return _tls13WrTake(&wr);
}
