#include <cx/stype/stconvert.h>
#include "cx/string.h"
#include "cx/stype/stvar.h"
#include "cx/utils/murmur.h"

STR_CONST(kTrue, "True");
STR_CONST(kFalse, "False");
STR_CONST(kOne, "1");
STR_CONST(kZero, "0");
STR_CONST(kYes, "Yes");
STR_CONST(kNo, "No");

void stDtor_string(stype st, _Pre_notnull_ _Post_invalid_ stgeneric* gen, uint32 flags)
{
    strDestroy(&gen->st_string);
}

intptr stCmp_string(stype st, _In_ stgeneric gen1, _In_ stgeneric gen2, uint32 flags)
{
    if (!(flags & ST_CaseInsensitive))
        return strCmp(gen1.st_string, gen2.st_string);
    else
        return strCmpi(gen1.st_string, gen2.st_string);
}

void stCopy_string(stype st, _stCopyDest_Anno_(st) stgeneric* dest, _In_ stgeneric src,
                   flags_t flags)
{
    string temp = 0;
    strDup(&temp, src.st_string);
    dest->st_string = temp;
}

uint32 stHash_string(stype st, _In_ stgeneric gen, uint32 flags)
{
    if (!(flags & ST_CaseInsensitive))
        return hashMurmur3Str(gen.st_string);
    else
        return hashMurmur3Stri(gen.st_string);
}

_Success_(return) _Check_return_ bool
stConvert_string(stype destst, _stCopyDest_Anno_(destst) stgeneric* dest, stype srcst,
                 _In_ stgeneric src, uint32 flags)
{
    switch (destst->id) {
    case stTypeId(int8):
    case stTypeId(int16): {
        // ehhh, see if it'll fit
        int32 temp;
        if (!strToInt32(&temp, src.st_string, 0, true))
            return false;
        return stConvert_int(destst, dest, stType(int32), stgeneric(int32, temp), flags);
    }
    case stTypeId(uint8):
    case stTypeId(uint16): {
        uint32 temp;
        if (!strToUInt32(&temp, src.st_string, 0, true))
            return false;
        return stConvert_int(destst, dest, stType(uint32), stgeneric(uint32, temp), flags);
    }
    case stTypeId(int32):
        return strToInt32(&dest->st_int32, src.st_string, 0, true);
    case stTypeId(uint32):
        return strToUInt32(&dest->st_uint32, src.st_string, 0, true);
    case stTypeId(int64):
        return strToInt64(&dest->st_int64, src.st_string, 0, true);
    case stTypeId(uint64):
        return strToUInt64(&dest->st_uint64, src.st_string, 0, true);
    case stTypeId(float32):
        return strToFloat32(&dest->st_float32, src.st_string, true);
    case stTypeId(float64):
        return strToFloat64(&dest->st_float64, src.st_string, true);
    case stTypeId(stvar):
        // okay, sure, we can put it in one
        _stvarInit(dest->st_stvar, srcst, src);
        return true;
    case stTypeId(bool):
        if (!strEqi(src.st_string, kTrue)) {
            dest->st_bool = true;
            return true;
        } else if (!strEqi(src.st_string, kFalse)) {
            dest->st_bool = false;
            return true;
        } else if (!strEq(src.st_string, kOne)) {
            dest->st_bool = true;
            return true;
        } else if (!strEq(src.st_string, kZero)) {
            dest->st_bool = false;
            return true;
        } else if (!strEqi(src.st_string, kYes)) {
            dest->st_bool = true;
            return true;
        } else if (!strEqi(src.st_string, kNo)) {
            dest->st_bool = false;
            return true;
        }
        return false;
    case stTypeId(suid):
        return suidDecode(dest->st_suid, src.st_string);
    case stTypeId(string):
        stCopy_string(destst, dest, src, flags);
        return true;
    }

    return false;
}
