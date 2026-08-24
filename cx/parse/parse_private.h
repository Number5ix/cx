#pragma once

#include "parse.h"

#include "cx/container.h"
#include "cx/string.h"

// Text patterns the matcher knows how to recognize. This is about how much text a
// placeholder eats, not about what it eventually turns into -- the destination decides
// that, which is why the same "${uint}" can fill a uint16 or a string.
enum PARSE_TYPES {
    PARSE_string,
    PARSE_int,
    PARSE_uint,
    PARSE_float,
    PARSE_bool,
    PARSE_object,
    PARSE_count
};

extern strref _parseTypeNames[PARSE_count];

typedef struct ParseField {
    string key;     // binding key, or NULL for a positional field
    int32 posidx;   // ordinal among positional fields, or -1 if keyed
    uint8 ptype;    // PARSE_TYPES
    bool isgroup;   // records which alternative of a group matched, not text
    bool skip;      // matched but never bound anywhere

    string def;     // default text
    bool hasdef;

    // int / uint
    int32 base;
    int32 digits;      // exact digit count, or 0 for any
    int32 maxdigits;   // 0 for no limit
    bool hasmin, hasmax;
    int64 minval, maxval;
    bool ws;   // allow whitespace before the number

    // float
    bool fixed;   // refuse scientific notation

    // string
    int32 len;      // exact byte count, or 0
    string until;
    string chars;   // character set for chars / notchars
    bool notchars;
    bool word, rest, quoted, trim, upper, lower;

    // object
    sa_string objopts;   // options the compiler did not recognize, passed through verbatim
} ParseField;

stDeclare(ParseField);
saDeclare(ParseField);

typedef enum {
    PE_Literal,   // fixed text
    PE_WS,        // a run of one or more whitespace bytes
    PE_Field,     // a placeholder
    PE_Group      // ( ... | ... )?
} ParseElemKind;

// Elements and alternatives nest through each other, so both are declared before either is
// defined. Their destructors recurse the same way, which is what lets one saDestroy on the
// root take a whole compiled pattern apart.
typedef struct ParseElem ParseElem;
stDeclare(ParseElem);
saDeclare(ParseElem);

typedef struct ParseAlt {
    sa_ParseElem elems;
} ParseAlt;

stDeclare(ParseAlt);
saDeclare(ParseAlt);

struct ParseElem {
    uint8 kind;

    string lit;         // PE_Literal
    int32 field;        // PE_Field, or PE_Group's own key field (-1 if it has none)

    sa_ParseAlt alts;   // PE_Group
    bool optional;
};

struct StrPattern {
    ParseAlt root;
    sa_ParseField fields;
    int32 npositional;
    flags_t flags;

    int32 nelems;       // total elements anywhere in the pattern
    int32 complexity;   // product of the branch counts, saturated -- feeds the match budget
};

// One recorded field position. The matcher produces only these; nothing is written into a
// caller's variable until the whole pattern has matched.
//
// For a normal field, off/len are a span of the input, and an off of -1 means "the field
// did not match, use its default". For a group's key field, off is the 1-based number of
// the alternative that matched, or 0 if the group did not match at all.
typedef struct StrPatSpan {
    int32 field;
    int32 off;
    int32 len;
} StrPatSpan;

// parsematch.c
//
// Match the pattern against s starting at `start`. On success fills spans (which must
// have room for saSize(pat->fields) entries), sets *nspans and *endpos, and returns true.
bool _parseRun(_In_ const StrPattern* pat, _In_opt_ strref s, int32 start, bool requireEnd,
               _Out_ StrPatSpan* spans, _Out_ int32* nspans, _Out_ int32* endpos);

// parsebind.c
//
// Write the matched spans into the caller's destinations. Conversions that can fail are
// all checked before anything is written.
bool _parseBind(_In_ const StrPattern* pat, _In_opt_ strref s, _In_ const StrPatSpan* spans,
                int32 nspans, int n, _In_ stvp* dests);

// Text a span stands for, with the field's own transforms applied.
void _parseSpanText(_Inout_ strhandle out, _In_ const ParseField* f, _In_opt_ strref s,
                    _In_ const StrPatSpan* sp);
