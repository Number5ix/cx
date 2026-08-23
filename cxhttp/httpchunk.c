// ---------------------------------------------------------------------------------------------
// Chunked transfer-encoding, writing half
//
// Decoding lives in httpparse.c, where it is inseparable from the framing decisions around it.
// Writing has no such entanglement: a chunk is its size in hex, the data, and two CRLFs, and the
// only judgement call is refusing to write a zero-length one by accident.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

_Use_decl_annotations_
bool httpChunkAppend(strhandle out, const uint8* data, size_t len)
{
    // A zero-length chunk *is* the end-of-body marker, so writing one here would truncate the body
    // at whatever point the producer happened to have nothing ready. Refusing is the only safe
    // answer; httpChunkFinish() is how a caller ends a body deliberately.
    if (!data || len == 0)
        return false;

    string hdr = 0;
    strFromUInt64(&hdr, (uint64)len, 16);
    strAppend(out, hdr);
    strAppend(out, _SL("\r\n"));
    strDestroy(&hdr);

    _httpAppendBytes(out, data, len);
    strAppend(out, _SL("\r\n"));
    return true;
}

_Use_decl_annotations_
bool httpChunkFinish(strhandle out)
{
    // The terminating chunk plus an empty trailer section. Both CRLFs are required: the first ends
    // the size line, the second ends the (empty) trailer block.
    strAppend(out, _SL("0\r\n\r\n"));
    return true;
}
