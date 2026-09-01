// ---------------------------------------------------------------------------------------------
// Form bodies
//
// The two encodings an HTML form can produce, because an API that only speaks JSON still has to
// log in somewhere. urlencoded builds its body in memory, which is all a field set needs.
// multipart is a list of part descriptors and a state machine over them, so a file part is read
// while the body is being written rather than held.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

#include <cx/platform/os.h>
#include <cx/utils/compare.h>

STR_CONST(kUrlEncodedType, "application/x-www-form-urlencoded");
STR_CONST(kMultipartPrefix, "multipart/form-data; boundary=");
STR_CONST(kOctetStream, "application/octet-stream");

// Long enough that it cannot occur in the payload by accident, and drawn from a real random source
// rather than a counter: a boundary an attacker can predict is a boundary they can inject.
#define BOUNDARY_BYTES 18

_Use_decl_annotations_
bool httpFormUrlEncoded(strhandle out, const HttpHeaders* fields)
{
    return httpQueryFormat(out, fields);
}

_Use_decl_annotations_
strref httpFormUrlEncodedType(void)
{
    return kUrlEncodedType;
}

// ---------------------------------------------------------------------------------------------
// multipart/form-data
// ---------------------------------------------------------------------------------------------

static void makeBoundary(strhandle out)
{
    uint8 raw[BOUNDARY_BYTES];
    if (!osGenRandom(raw, sizeof(raw))) {
        strClear(out);
        return;
    }

    string b64 = 0;
    strB64Encode(&b64, raw, sizeof(raw), true);

    // The URL-safe alphabet is already inside the character set a boundary may use; only the '='
    // padding has to go.
    strClear(out);
    strAppend(out, _SL("cxhttp-"));
    striter it;
    striBorrow(&it, b64);
    uint8 c;
    while (striChar(&it, &c)) {
        if (c != '=')
            strAppendChar(out, c);
    }
    strDestroy(&b64);
}

// A quoted-string in a Content-Disposition parameter. RFC 7578 says to send the name as-is and
// escape the two characters that would end the string early; anything cleverer (RFC 2231 encoding)
// is understood by almost nothing.
static void appendQuoted(strhandle out, strref s)
{
    strAppendChar(out, '"');
    striter it;
    striBorrow(&it, s);
    uint8 c;
    while (striChar(&it, &c)) {
        if (c == '"' || c == '\\')
            strAppendChar(out, '\\');
        // A raw CR or LF in a field name would split the part header in two, which is the
        // multipart spelling of header injection.
        if (c == '\r' || c == '\n')
            continue;
        strAppendChar(out, c);
    }
    strAppendChar(out, '"');
}

// ---------------------------------------------------------------------------------------------
// Parts
// ---------------------------------------------------------------------------------------------

// Start a part and put it on the list. The header is everything up to the content: the boundary
// line, the Content-Disposition, an optional Content-Type, and the blank line that ends them.
static HttpMultipartPart* addPart(HttpMultipart* mp, strref name, strref filename,
                                  strref contentType)
{
    HttpMultipartPart* p = xaAllocStruct(HttpMultipartPart, XA_Zero);

    strAppend(&p->header, _SL("--"));
    strAppend(&p->header, mp->boundary);
    strAppend(&p->header, _SL("\r\nContent-Disposition: form-data; name="));
    appendQuoted(&p->header, name);

    if (!strEmpty(filename)) {
        strAppend(&p->header, _SL("; filename="));
        appendQuoted(&p->header, filename);
    }
    strAppend(&p->header, _SL("\r\n"));

    if (!strEmpty(contentType)) {
        strAppend(&p->header, _SL("Content-Type: "));
        strAppend(&p->header, contentType);
        strAppend(&p->header, _SL("\r\n"));
    }
    strAppend(&p->header, _SL("\r\n"));

    saPush(&mp->parts, ptr, p);
    return p;
}

static void destroyPart(HttpMultipartPart* p)
{
    strDestroy(&p->header);

    switch (p->srctype) {
    case HTTPMPSRC_String:
        strDestroy(&p->src.text);
        break;
    case HTTPMPSRC_Bytes:
        bufDestroy(&p->src.bytes);
        break;
    case HTTPMPSRC_VFSFile:
        if (p->close && p->src.vfile)
            vfsClose(p->src.vfile);
        break;
    case HTTPMPSRC_Stream:
        if (p->src.stream) {
            // cxhttp drove this one as the consumer, so it is the side that ends it
            sbufClose(p->src.stream);
            sbufRelease(&p->src.stream);
        }
        break;
    }

    xaFree(p);
}

// How much of an open file is still ahead of it, so a part can declare its length. A file that
// cannot seek answers -1, which is what sends the whole body chunked instead.
static int64 vfileRemaining(VFSFile* file)
{
    int64 cur = vfsTell(file);
    if (cur < 0)
        return -1;

    int64 end = vfsSeek(file, 0, FS_End);
    if (end < 0 || vfsSeek(file, cur, FS_Set) != cur)
        return -1;

    return end - cur;
}

// ---------------------------------------------------------------------------------------------
// The producer
//
// Walks part header -> content -> CRLF for each part in turn and then the closing boundary,
// emitting at most the slice it was asked for and remembering where it stopped.
// ---------------------------------------------------------------------------------------------

typedef enum {
    MPS_Header,    // the literal that introduces the current part
    MPS_Content,   // the current part's content
    MPS_Trailer,   // the CRLF that ends the current part
    MPS_Close,     // the closing boundary
    MPS_Done,
} MpStage;

typedef struct MpProducer {
    sa_ptr parts;     // HttpMultipartPart*, taken from the HttpMultipart
    string closing;   // the closing boundary line, built once
    int32 idx;        // part being emitted
    MpStage stage;
    int64 pos;        // bytes of the current stage emitted so far
} MpProducer;

static void mpCleanup(_Pre_opt_valid_ void* ctx)
{
    MpProducer* mpp = (MpProducer*)ctx;
    if (!mpp)
        return;

    for (int32 i = 0; i < saSize(mpp->parts); i++) {
        destroyPart((HttpMultipartPart*)mpp->parts.a[i]);
    }

    saDestroy(&mpp->parts);
    strDestroy(&mpp->closing);
    xaFree(mpp);
}

// Move the parts onto a producer of their own. Both terminal calls do this, so the multipart is
// left empty and releasing it afterwards is always safe no matter how long the body takes to send.
static MpProducer* takeParts(HttpMultipart* mp)
{
    MpProducer* mpp = xaAllocStruct(MpProducer, XA_Zero);

    mpp->parts  = mp->parts;
    mp->parts.a = NULL;

    strAppend(&mpp->closing, _SL("--"));
    strAppend(&mpp->closing, mp->boundary);
    strAppend(&mpp->closing, _SL("--\r\n"));

    mpp->stage   = saSize(mpp->parts) > 0 ? MPS_Header : MPS_Close;
    mp->finished = true;
    return mpp;
}

static size_t emitLiteral(MpProducer* mpp, strref lit, uint8* buf, size_t sz, bool* done)
{
    size_t total = strLen(lit);
    size_t n     = min(sz, total - (size_t)mpp->pos);

    if (n > 0)
        memcpy(buf, (const uint8*)strC(lit) + mpp->pos, n);

    mpp->pos += (int64)n;
    *done = (size_t)mpp->pos >= total;
    return n;
}

static size_t emitContent(MpProducer* mpp, HttpMultipartPart* p, uint8* buf, size_t sz, bool* done,
                          bool* err)
{
    *done = false;
    *err  = false;

    // A part that declared a length is held to it, whatever the source goes on to say: the head
    // has already promised that many bytes.
    if (p->len >= 0) {
        int64 remain = p->len - mpp->pos;
        if (remain <= 0) {
            *done = true;
            return 0;
        }
        if ((int64)sz > remain)
            sz = (size_t)remain;
    }

    size_t n = 0;
    switch (p->srctype) {
    case HTTPMPSRC_String:
        memcpy(buf, (const uint8*)strC(p->src.text) + mpp->pos, sz);
        n = sz;
        break;

    case HTTPMPSRC_Bytes:
        memcpy(buf, p->src.bytes->data + mpp->pos, sz);
        n = sz;
        break;

    case HTTPMPSRC_VFSFile:
        if (!vfsRead(p->src.vfile, buf, sz, &n))
            *err = true;
        else if (n == 0)
            *done = true;
        break;

    case HTTPMPSRC_Stream:
        sbufCRead(p->src.stream, buf, sz, &n);
        if (n == 0) {
            if (sbufIsError(p->src.stream))
                *err = true;
            else if (!sbufCMore(p->src.stream))
                *done = true;
        }
        break;
    }

    // Whatever a failing source left in the output buffer is not part of the body.
    if (*err)
        return 0;

    mpp->pos += (int64)n;

    if (p->len >= 0) {
        if (mpp->pos >= p->len)
            *done = true;
        else if (*done)
            *err = true;   // the source ran out early and the length is already on the wire
    }

    return n;
}

static size_t mpStep(StreamBuffer* sb, MpProducer* mpp, uint8* buf, size_t sz)
{
    HttpMultipartPart* p = NULL;
    bool done = false, err = false;
    size_t n = 0;

    switch (mpp->stage) {
    case MPS_Header:
        p = (HttpMultipartPart*)mpp->parts.a[mpp->idx];
        n = emitLiteral(mpp, p->header, buf, sz, &done);
        if (done) {
            mpp->stage = MPS_Content;
            mpp->pos   = 0;
        }
        break;

    case MPS_Content:
        p = (HttpMultipartPart*)mpp->parts.a[mpp->idx];
        n = emitContent(mpp, p, buf, sz, &done, &err);
        if (err) {
            sbufError(sb);
            mpp->stage = MPS_Done;
        } else if (done) {
            mpp->stage = MPS_Trailer;
            mpp->pos   = 0;
        }
        break;

    case MPS_Trailer:
        n = emitLiteral(mpp, _SL("\r\n"), buf, sz, &done);
        if (done) {
            mpp->idx++;
            mpp->pos   = 0;
            mpp->stage = mpp->idx < saSize(mpp->parts) ? MPS_Header : MPS_Close;
        }
        break;

    case MPS_Close:
        n = emitLiteral(mpp, mpp->closing, buf, sz, &done);
        if (done)
            mpp->stage = MPS_Done;
        break;

    case MPS_Done:
        break;
    }

    return n;
}

static size_t mpPullCB(_Pre_valid_ StreamBuffer* sb, _Out_writes_bytes_(sz) uint8* buf, size_t sz,
                       _Pre_opt_valid_ void* ctx)
{
    MpProducer* mpp = (MpProducer*)ctx;
    if (!mpp)
        return 0;

    if (sz == 0) {
        // A status check rather than a request for data. Once the stream is over there is nothing
        // left to feed it, so hand the slot back.
        if (sbufIsClosed(sb))
            sbufPUnregister(sb);
        return 0;
    }

    size_t out = 0;
    while (out < sz && mpp->stage != MPS_Done) {
        MpStage was  = mpp->stage;
        int32 wasidx = mpp->idx;

        size_t n = mpStep(sb, mpp, buf + out, sz - out);
        out += n;

        // Nothing given and nowhere moved on to: a streamed part has no data yet. Hand back what
        // there is and wait to be asked again rather than spinning here.
        if (n == 0 && mpp->stage == was && mpp->idx == wasidx)
            break;
    }

    // out of parts: leave the slot open for another producer rather than ending the stream
    if (mpp->stage == MPS_Done)
        sbufPUnregister(sb);

    return out;
}

// Build a stream over the parts, returning the reference sbufCreate() started with.
static StreamBuffer* buildStream(HttpMultipart* mp)
{
    MpProducer* mpp  = takeParts(mp);
    StreamBuffer* sb = sbufCreate(HTTP_BODY_CHUNK);

    if (!sbufPRegisterPull(sb, mpPullCB, mpCleanup, mpp)) {
        // registration never ran, so the cleanup callback will not either
        mpCleanup(mpp);
        sbufRelease(&sb);
        return NULL;
    }

    return sb;
}

// ---------------------------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
bool httpMultipartInit(HttpMultipart* mp)
{
    if (!mp)
        return false;

    memset(mp, 0, sizeof(HttpMultipart));
    saInit(&mp->parts, ptr, 8);
    makeBoundary(&mp->boundary);
    return !strEmpty(mp->boundary);
}

_Use_decl_annotations_
bool httpMultipartAddField(HttpMultipart* mp, strref name, strref value)
{
    if (!mp || strEmpty(name) || mp->finished)
        return false;

    HttpMultipartPart* p = addPart(mp, name, NULL, NULL);
    p->srctype           = HTTPMPSRC_String;
    strDup(&p->src.text, value);
    p->len = (int64)strLen(p->src.text);
    return true;
}

_Use_decl_annotations_
bool httpMultipartAddFile(HttpMultipart* mp, strref name, strref filename, strref contentType,
                          const uint8* data, size_t len)
{
    if (!mp || strEmpty(name) || mp->finished)
        return false;

    Buffer buf = NULL;
    if (data && len > 0) {
        // A part-sized allocation is the caller's size rather than ours, so it fails rather than
        // aborting the process.
        buf = bufTryCreate(len);
        if (!buf)
            return false;

        memcpy(buf->data, data, len);
        buf->len = len;
    }

    strref fname         = strEmpty(filename) ? _SL("file") : filename;
    strref ctype         = strEmpty(contentType) ? kOctetStream : contentType;
    HttpMultipartPart* p = addPart(mp, name, fname, ctype);

    p->srctype   = HTTPMPSRC_Bytes;
    p->src.bytes = buf;
    p->len       = buf ? (int64)buf->len : 0;
    return true;
}

_Use_decl_annotations_
bool httpMultipartAddFileVFS(HttpMultipart* mp, strref name, strref filename, strref contentType,
                             VFSFile* file, bool close)
{
    if (!mp || strEmpty(name) || !file || mp->finished) {
        if (close && file)
            vfsClose(file);
        return false;
    }

    strref fname         = strEmpty(filename) ? _SL("file") : filename;
    strref ctype         = strEmpty(contentType) ? kOctetStream : contentType;
    HttpMultipartPart* p = addPart(mp, name, fname, ctype);

    p->srctype   = HTTPMPSRC_VFSFile;
    p->src.vfile = file;
    p->close     = close;
    p->len       = vfileRemaining(file);
    return true;
}

_Use_decl_annotations_
bool httpMultipartAddStream(HttpMultipart* mp, strref name, strref filename, strref contentType,
                            StreamBuffer* sb, int64 len)
{
    if (!mp || strEmpty(name) || !sb || mp->finished)
        return false;

    // cxhttp is the consumer of this buffer; the caller registered the producer. Checking the mode
    // here rather than when the part is reached means a buffer whose producer is in push mode is
    // refused now, while the caller can still do something about it.
    if (!sbufIsPull(sb))
        return false;

    strref ctype         = strEmpty(contentType) ? kOctetStream : contentType;
    HttpMultipartPart* p = addPart(mp, name, filename, ctype);

    p->srctype    = HTTPMPSRC_Stream;
    p->src.stream = sbufAcquire(sb);
    p->len        = len;
    return true;
}

_Use_decl_annotations_
int64 httpMultipartLength(const HttpMultipart* mp)
{
    if (!mp)
        return -1;

    int64 total = 0;
    for (int32 i = 0; i < saSize(mp->parts); i++) {
        HttpMultipartPart* p = (HttpMultipartPart*)mp->parts.a[i];
        if (p->len < 0)
            return -1;

        total += (int64)strLen(p->header) + p->len + 2;   // + the CRLF that ends the part
    }

    // "--" + boundary + "--\r\n"
    return total + (int64)strLen(mp->boundary) + 6;
}

_Use_decl_annotations_
bool httpMultipartAttach(HttpMultipart* mp, HttpRequest* req)
{
    if (!mp || !req || mp->finished)
        return false;

    int64 len = httpMultipartLength(mp);

    string ctype = 0;
    strAppend(&ctype, kMultipartPrefix);
    strAppend(&ctype, mp->boundary);

    bool ok          = false;
    StreamBuffer* sb = buildStream(mp);
    if (sb) {
        ok = httprequestSetBodyStream(req, sb, len, ctype);
        if (!ok)
            sbufClose(sb);   // nothing is going to read it, so let the producer go

        sbufRelease(&sb);
    }

    strDestroy(&ctype);
    return ok;
}

_Use_decl_annotations_
bool httpMultipartFinish(HttpMultipart* mp, string* body, string* contentType)
{
    if (!mp)
        return false;

    bool ok = true;
    if (!mp->finished) {
        StreamBuffer* sb = buildStream(mp);
        if (!sb)
            return false;

        // Drains the whole body, which for a streamed part means waiting for its producer.
        ok = sbufStrOut(sb, &mp->rendered);
        sbufClose(sb);
        sbufRelease(&sb);
    }

    if (body)
        strDup(body, mp->rendered);

    if (contentType) {
        strClear(contentType);
        strAppend(contentType, kMultipartPrefix);
        strAppend(contentType, mp->boundary);
    }

    return ok;
}

_Use_decl_annotations_
void httpMultipartDestroy(HttpMultipart* mp)
{
    if (!mp)
        return;

    for (int32 i = 0; i < saSize(mp->parts); i++) {
        destroyPart((HttpMultipartPart*)mp->parts.a[i]);
    }

    saDestroy(&mp->parts);
    strDestroy(&mp->boundary);
    strDestroy(&mp->rendered);
    mp->finished = false;
}
