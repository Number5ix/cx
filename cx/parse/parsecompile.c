#include "parse_private.h"

#include "cx/debug/error.h"
#include "cx/stype/stconvert.h"
#include "cx/thread/atomic.h"
#include "cx/utils/compare.h"

STR_CONSTR(kTName_string, "string");
STR_CONSTR(kTName_int, "int");
STR_CONSTR(kTName_uint, "uint");
STR_CONSTR(kTName_float, "float");
STR_CONSTR(kTName_bool, "bool");
STR_CONSTR(kTName_object, "object");

strref _parseTypeNames[PARSE_count] = {
    [PARSE_string] = _SR(kTName_string), [PARSE_int] = _SR(kTName_int),
    [PARSE_uint] = _SR(kTName_uint),     [PARSE_float] = _SR(kTName_float),
    [PARSE_bool] = _SR(kTName_bool),     [PARSE_object] = _SR(kTName_object),
};

STR_CONST(kOptSkip, "skip");
STR_CONST(kOptHex, "hex");
STR_CONST(kOptOctal, "octal");
STR_CONST(kOptBinary, "binary");
STR_CONST(kOptBase, "base");
STR_CONST(kOptDigits, "digits");
STR_CONST(kOptMaxDigits, "maxdigits");
STR_CONST(kOptMin, "min");
STR_CONST(kOptMax, "max");
STR_CONST(kOptWS, "ws");
STR_CONST(kOptFixed, "fixed");
STR_CONST(kOptLen, "len");
STR_CONST(kOptUntil, "until");
STR_CONST(kOptChars, "chars");
STR_CONST(kOptNotChars, "notchars");
STR_CONST(kOptWord, "word");
STR_CONST(kOptRest, "rest");
STR_CONST(kOptQuoted, "quoted");
STR_CONST(kOptTrim, "trim");
STR_CONST(kOptUpper, "upper");
STR_CONST(kOptLower, "lower");
STR_CONST(kOptSep, ":");

// The largest branch factor the budget calculation will believe. A pattern nested deeply
// enough to exceed this is already pathological; capping here keeps the budget itself from
// overflowing, and the matcher's own attempt counter is what actually stops the runaway.
#define PARSE_MAXCOMPLEXITY 4096

static void fieldDtor(stype st, stgeneric* g, uint32 flags)
{
    ParseField* f = (ParseField*)g->st_opaque;
    strDestroy(&f->key);
    strDestroy(&f->def);
    strDestroy(&f->until);
    strDestroy(&f->chars);
    saDestroy(&f->objopts);
}

// No copy op: a field is filled in place in the array it was created in and never copied
// out of it, so saDestroy is the only stype operation this type ever sees.
stDefine(ParseField) {
    .id    = stTypeId(opaque),
    .size  = sizeof(ParseField),
    .flags = stFlag(PassPtr),
    .ops   = { .dtor = fieldDtor }
};

// These two call each other through saDestroy, which is how destroying the root alternative
// walks the whole tree. The recursion follows the pattern's own nesting, so it is as deep as
// the pattern has parentheses and no deeper.
static void elemDtor(stype st, stgeneric* g, uint32 flags)
{
    ParseElem* e = (ParseElem*)g->st_opaque;
    strDestroy(&e->lit);
    saDestroy(&e->alts);
}

static void altDtor(stype st, stgeneric* g, uint32 flags)
{
    ParseAlt* a = (ParseAlt*)g->st_opaque;
    saDestroy(&a->elems);
}

stDefine(ParseElem) {
    .id    = stTypeId(opaque),
    .size  = sizeof(ParseElem),
    .flags = stFlag(PassPtr),
    .ops   = { .dtor = elemDtor }
};

stDefine(ParseAlt) {
    .id    = stTypeId(opaque),
    .size  = sizeof(ParseAlt),
    .flags = stFlag(PassPtr),
    .ops   = { .dtor = altDtor }
};

typedef struct PCtx {
    strref pat;
    int32 len;
    int32 pos;
    flags_t flags;

    sa_ParseField fields;
    int32 npos;    // how many positional fields there have been
    int32 depth;   // group nesting depth; a placeholder in a group must be keyed

    // Which already-compiled fields the current point in the pattern is mutually exclusive
    // with: those from gstart (where the innermost group's fields begin, -1 outside any
    // group) up to altstart (where the current alternative's begin). Everything in that
    // window sits in an earlier alternative of the same group and so can never match
    // alongside what is being compiled now.
    int32 gstart;
    int32 altstart;
    int32 nelems;
    int64 complexity;

    bool err;
} PCtx;

static bool parseSeq(PCtx* c, ParseAlt* out, bool nested);

static bool fail(PCtx* c)
{
    c->err = true;
    cxerr  = CX_InvalidArgument;
    return false;
}

// Grow by one and hand back the new slot to fill in place. saSetSize zeroes what it adds,
// so only the non-zero defaults need setting here.
static ParseElem* elemPush(sa_ParseElem* elems)
{
    int32 n = saSize(*elems);
    saSetSize(elems, n + 1);
    return &elems->a[n];
}

static ParseField* fieldPush(sa_ParseField* fields)
{
    int32 n = saSize(*fields);
    saSetSize(fields, n + 1);

    ParseField* f = &fields->a[n];
    f->posidx     = -1;
    f->base       = 10;
    return f;
}

// Copy [b,e) of the pattern with backtick escapes resolved: the backtick disappears and
// the character after it becomes ordinary text.
static void unescape(strhandle out, strref s, int32 b, int32 e)
{
    strClear(out);

    for (int32 i = b; i < e; i++) {
        uint8 ch = strGetChar(s, i);
        if (ch == '`' && i + 1 < e)
            ch = strGetChar(s, ++i);
        strAppendBytes(out, &ch, 1);
    }
}

// Turn accumulated literal text into elements. Unless STRPAT_ExactWS was asked for, runs
// of whitespace become their own element so that one space in a pattern matches any run
// of spacing in the input.
static void flushLit(PCtx* c, sa_ParseElem* b, strref lit)
{
    if (strEmpty(lit))
        return;

    if (c->flags & STRPAT_ExactWS) {
        ParseElem* e = elemPush(b);
        e->kind      = PE_Literal;
        strDup(&e->lit, lit);
        c->nelems++;
        return;
    }

    int32 n = (int32)strLen(lit);
    for (int32 i = 0; i < n;) {
        bool ws = isspace(strGetChar(lit, i)) != 0;
        int32 j = i;
        while (j < n && (isspace(strGetChar(lit, j)) != 0) == ws) j++;

        ParseElem* e = elemPush(b);
        c->nelems++;
        if (ws) {
            e->kind = PE_WS;
        } else {
            e->kind = PE_Literal;
            strSubStr(&e->lit, lit, i, j);
        }
        i = j;
    }
}

// parse options ---------------------------------------------------------------------------

static bool optInt32(strref val, int32* out)
{
    return strToInt32(out, val, 10, STRNUM_NoTrailing | STRNUM_NoWS | STRNUM_NoPrefix);
}

static bool optInt64(strref val, int64* out)
{
    return strToInt64(out, val, 10, STRNUM_NoTrailing | STRNUM_NoWS | STRNUM_NoPrefix);
}

// Hand an option the compiler has no meaning for to the object that will do the parsing,
// exactly as written. Only the object type does this; for every other type an unknown
// option is a mistake worth catching at compile time.
static void objOpt(ParseField* f, strref name, strref val, bool hasval)
{
    if (!f->objopts.a)
        saInit(&f->objopts, string, 4);

    string opt = 0;
    if (hasval)
        strNConcat(&opt, name, kOptSep, val);
    else
        strDup(&opt, name);

    saPushC(&f->objopts, string, &opt);
}

static bool applyOpt(ParseField* f, strref name, strref val, bool hasval)
{
    int32 n32;
    int64 n64;

    if (strEq(name, kOptSkip)) {
        f->skip = true;
        return true;
    }

    // trim/upper/lower reshape whatever text came out, so they mean the same thing for a
    // string and for an object that is handed one
    if (f->ptype == PARSE_string || f->ptype == PARSE_object) {
        if (strEq(name, kOptTrim)) {
            f->trim = true;
            return true;
        }
        if (strEq(name, kOptUpper)) {
            f->upper = true;
            return true;
        }
        if (strEq(name, kOptLower)) {
            f->lower = true;
            return true;
        }
    }

    switch (f->ptype) {
    case PARSE_int:
    case PARSE_uint:
        if (strEq(name, kOptHex)) {
            f->base = 16;
            return true;
        }
        if (strEq(name, kOptOctal)) {
            f->base = 8;
            return true;
        }
        if (strEq(name, kOptBinary)) {
            f->base = 2;
            return true;
        }
        if (strEq(name, kOptWS)) {
            f->ws = true;
            return true;
        }
        if (!hasval)
            return false;
        if (strEq(name, kOptBase)) {
            if (!optInt32(val, &n32) || n32 < 2 || n32 > 36)
                return false;
            f->base = n32;
            return true;
        }
        if (strEq(name, kOptDigits)) {
            if (!optInt32(val, &n32) || n32 < 1)
                return false;
            f->digits = n32;
            return true;
        }
        if (strEq(name, kOptMaxDigits)) {
            if (!optInt32(val, &n32) || n32 < 1)
                return false;
            f->maxdigits = n32;
            return true;
        }
        if (strEq(name, kOptMin)) {
            if (!optInt64(val, &n64))
                return false;
            f->hasmin = true;
            f->minval = n64;
            return true;
        }
        if (strEq(name, kOptMax)) {
            if (!optInt64(val, &n64))
                return false;
            f->hasmax = true;
            f->maxval = n64;
            return true;
        }
        return false;

    case PARSE_float:
        if (strEq(name, kOptFixed)) {
            f->fixed = true;
            return true;
        }
        return false;

    case PARSE_string:
        if (strEq(name, kOptWord)) {
            f->word = true;
            return true;
        }
        if (strEq(name, kOptRest)) {
            f->rest = true;
            return true;
        }
        if (strEq(name, kOptQuoted)) {
            f->quoted = true;
            return true;
        }
        if (!hasval)
            return false;
        if (strEq(name, kOptLen)) {
            if (!optInt32(val, &n32) || n32 < 0)
                return false;
            f->len = n32;
            return true;
        }
        if (strEq(name, kOptUntil)) {
            strDup(&f->until, val);
            return true;
        }
        if (strEq(name, kOptChars) || strEq(name, kOptNotChars)) {
            if (strEmpty(val))
                return false;
            strDup(&f->chars, val);
            f->notchars = strEq(name, kOptNotChars);
            return true;
        }
        return false;

    case PARSE_object:
        objOpt(f, name, val, hasval);
        return true;

    default:
        return false;
    }
}

static bool parseOpts(PCtx* c, ParseField* f, int32 ostart, int32 oend)
{
    string name = 0, val = 0;
    bool ok = true;
    int32 i = ostart;

    while (i < oend) {
        int32 e = i, colon = -1;
        while (e < oend) {
            uint8 ch = strGetChar(c->pat, e);
            if (ch == '`') {
                e += 2;
                continue;
            }
            if (ch == ',')
                break;
            if (ch == ':' && colon < 0)
                colon = e;
            e++;
        }
        e = clamphigh(e, oend);

        if (colon >= 0 && colon < e) {
            unescape(&name, c->pat, i, colon);
            unescape(&val, c->pat, colon + 1, e);
        } else {
            unescape(&name, c->pat, i, e);
            strClear(&val);
        }

        if (!strEmpty(name) && !applyOpt(f, name, val, colon >= 0 && colon < e)) {
            ok = false;
            break;
        }

        i = e + 1;
    }

    strDestroy(&name);
    strDestroy(&val);
    return ok ? true : fail(c);
}

// A default has to survive the trip into the placeholder's own type, and there is no
// reason to wait until some later call to discover that it will not.
static bool validDefault(const ParseField* f)
{
    switch (f->ptype) {
    case PARSE_int: {
        int64 v;
        return strToInt64(&v, f->def, f->base, STRNUM_NoTrailing | STRNUM_NoWS | STRNUM_NoPrefix);
    }
    case PARSE_uint: {
        uint64 v;
        return strToUInt64(&v,
                           f->def,
                           f->base,
                           STRNUM_NoTrailing | STRNUM_NoWS | STRNUM_NoPrefix | STRNUM_NoSign);
    }
    case PARSE_float: {
        float64 v;
        return strToFloat64(&v, f->def, STRNUM_NoTrailing | STRNUM_NoWS | STRNUM_NoPrefix);
    }
    case PARSE_bool: {
        bool b;
        return stConvert(bool, &b, string, f->def);
    }
    default:
        return true;
    }
}

// placeholders ------------------------------------------------------------------------------

static bool dupKey(PCtx* c, strref key)
{
    for (int32 i = 0; i < saSize(c->fields); i++) {
        if (!c->fields.a[i].key || !strEq(c->fields.a[i].key, key))
            continue;

        // Reusing a key across the alternatives of one group is how a field that is spelled
        // differently in each form still binds to one destination. Only one alternative can
        // match, so only one of them can ever produce a value. Anywhere else, both fields
        // could match at once and there would be no saying which one the destination got.
        if (c->gstart >= 0 && i >= c->gstart && i < c->altstart)
            continue;

        return true;
    }
    return false;
}

// Enters with c->pos on the '$' of a "${".
static bool parsePlaceholder(PCtx* c, sa_ParseElem* b)
{
    int32 bstart = c->pos + 2;
    int32 bend   = -1;

    for (int32 i = bstart; i < c->len; i++) {
        uint8 ch = strGetChar(c->pat, i);
        if (ch == '`') {
            i++;
            continue;
        }
        if (ch == '}') {
            bend = i;
            break;
        }
    }

    if (bend < 0)
        return fail(c);   // no closing brace

    c->pos = bend + 1;

    int32 tstart = bstart, tend = -1;
    int32 kstart = -1, kend = -1;
    int32 ostart = -1, oend = -1;
    int32 dstart = -1;
    int phase    = 0;

    for (int32 i = bstart; i < bend; i++) {
        uint8 ch = strGetChar(c->pat, i);
        if (ch == '`') {
            i++;
            continue;
        }

        if (phase == 1) {
            if (ch == ')') {
                oend  = i;
                phase = 0;
            }
            continue;
        }

        if (ch == ';') {
            if (tend < 0)
                tend = i;
            if (kstart >= 0 && kend < 0)
                kend = i;
            dstart = i + 1;
            break;   // everything left is the default, verbatim
        }

        if (oend > 0)
            return fail(c);   // only a default may follow the options

        if (ch == ':') {
            if (kstart >= 0)
                return fail(c);   // one key per placeholder
            if (tend < 0)
                tend = i;
            kstart = i + 1;
        } else if (ch == '(') {
            if (tend < 0)
                tend = i;
            if (kstart >= 0 && kend < 0)
                kend = i;
            ostart = i + 1;
            phase  = 1;
        } else if (ch == ')') {
            return fail(c);
        }
    }

    if (phase == 1)
        return fail(c);   // no closing paren on the options
    if (tend < 0)
        tend = bend;
    if (kstart >= 0 && kend < 0)
        kend = bend;

    string tmp = 0;
    unescape(&tmp, c->pat, tstart, tend);

    int ptype = -1;
    for (int i = 0; i < PARSE_count; i++) {
        if (strEq(tmp, _parseTypeNames[i])) {
            ptype = i;
            break;
        }
    }

    strDestroy(&tmp);
    if (ptype < 0)
        return fail(c);   // no such placeholder type

    string key = 0;
    if (kstart >= 0) {
        unescape(&key, c->pat, kstart, kend);
        if (strEmpty(key) || dupKey(c, key)) {
            strDestroy(&key);
            return fail(c);
        }
    } else if (c->depth > 0) {
        // Inside a group a field may or may not appear at all, which makes counting
        // positions meaningless -- so there is nothing an unkeyed placeholder could bind to.
        return fail(c);
    }

    int32 fidx    = saSize(c->fields);
    ParseField* f = fieldPush(&c->fields);
    f->ptype      = (uint8)ptype;
    f->key        = key;

    if (ostart >= 0 && !parseOpts(c, f, ostart, oend < 0 ? bend : oend))
        return false;

    // The position is handed out after the options are known, because a skipped field is
    // never bound and so must not use up a positional slot that the next one is counting on.
    if (!key && !f->skip)
        f->posidx = c->npos++;

    if (dstart >= 0) {
        // A group already says "this may be absent". A default inside one would be a second
        // way to say it, and worse, it would let a branch succeed on nothing at all, which
        // makes every later alternative unreachable for reasons the pattern does not show.
        if (c->depth > 0)
            return fail(c);

        unescape(&f->def, c->pat, dstart, bend);
        f->hasdef = true;
        if (!validDefault(f))
            return fail(c);
    }

    ParseElem* e = elemPush(b);
    e->kind      = PE_Field;
    e->field     = fidx;
    c->nelems++;

    return true;
}

// groups ------------------------------------------------------------------------------------

static bool altHasBinding(const ParseAlt* alt)
{
    for (int32 i = 0; i < saSize(alt->elems); i++) {
        if (alt->elems.a[i].kind == PE_Field || alt->elems.a[i].kind == PE_Group)
            return true;
    }
    return false;
}

// A group key is a plain identifier and ends at the first character that cannot be part of
// one. A literal ':' right after a group therefore needs a backtick.
static bool keyChar(uint8 ch)
{
    return isalnum(ch) || ch == '_' || ch == '.';
}

// Enters with c->pos on the '('.
static bool parseGroup(PCtx* c, sa_ParseElem* b)
{
    c->pos++;
    c->depth++;

    sa_ParseAlt alts;
    saInit(&alts, ParseAlt, 0);
    bool ok = true;

    int32 savedg = c->gstart, saveda = c->altstart;
    c->gstart = saSize(c->fields);

    for (;;) {
        int32 n = saSize(alts);
        saSetSize(&alts, n + 1);
        c->altstart = saSize(c->fields);
        if (!parseSeq(c, &alts.a[n], true)) {
            ok = false;
            break;
        }

        if (c->pos >= c->len) {
            ok = fail(c);   // ran off the end looking for ')'
            break;
        }

        uint8 ch = strGetChar(c->pat, c->pos);
        c->pos++;
        if (ch == ')')
            break;
        if (ch != '|') {
            ok = fail(c);
            break;
        }
    }

    c->depth--;
    c->gstart   = savedg;
    c->altstart = saveda;

    if (!ok) {
        saDestroy(&alts);
        return false;
    }

    bool optional = false;
    if (c->pos < c->len && strGetChar(c->pat, c->pos) == '?') {
        optional = true;
        c->pos++;
    }

    int32 gfield = -1;
    if (c->pos < c->len && strGetChar(c->pat, c->pos) == ':') {
        int32 ks = c->pos + 1, ke = ks;
        while (ke < c->len && keyChar(strGetChar(c->pat, ke))) ke++;

        if (ke == ks) {
            saDestroy(&alts);
            return fail(c);
        }

        string key = 0;
        strSubStr(&key, c->pat, ks, ke);
        if (dupKey(c, key)) {
            strDestroy(&key);
            saDestroy(&alts);
            return fail(c);
        }

        gfield        = saSize(c->fields);
        ParseField* f = fieldPush(&c->fields);
        f->isgroup    = true;
        f->key        = key;
        c->pos        = ke;
    }

    // A group with one alternative, no '?' and no key that binds nothing cannot change
    // what this pattern matches or produces. Rejecting it is what turns a literal
    // "(none)" -- which would otherwise quietly match "none" without its parentheses --
    // into an error telling the author to escape the parens.
    int32 nalts = saSize(alts);
    if (nalts == 1 && !optional && gfield < 0 && !altHasBinding(&alts.a[0])) {
        saDestroy(&alts);
        return fail(c);
    }

    c->complexity *= (int64)nalts + (optional ? 1 : 0);
    c->complexity = clamphigh(c->complexity, PARSE_MAXCOMPLEXITY);

    ParseElem* e = elemPush(b);
    e->kind      = PE_Group;
    e->alts      = alts;
    e->optional  = optional;
    e->field     = gfield;
    c->nelems++;

    return true;
}

// A run of elements, stopping at '|' or ')' when it is inside a group and at the end of
// the pattern when it is not.
static bool parseSeq(PCtx* c, ParseAlt* out, bool nested)
{
    sa_ParseElem b;
    saInit(&b, ParseElem, 0);
    string lit = 0;
    bool ok    = true;

    while (c->pos < c->len) {
        uint8 ch = strGetChar(c->pat, c->pos);

        if (ch == '`') {
            if (c->pos + 1 < c->len)
                ch = strGetChar(c->pat, ++c->pos);
            strAppendBytes(&lit, &ch, 1);
            c->pos++;
            continue;
        }

        if (ch == '$' && c->pos + 1 < c->len && strGetChar(c->pat, c->pos + 1) == '{') {
            flushLit(c, &b, lit);
            strClear(&lit);
            if (!parsePlaceholder(c, &b)) {
                ok = false;
                break;
            }
            continue;
        }

        if (ch == '(') {
            flushLit(c, &b, lit);
            strClear(&lit);
            if (!parseGroup(c, &b)) {
                ok = false;
                break;
            }
            continue;
        }

        if (ch == '|' || ch == ')') {
            if (!nested)
                ok = fail(c);   // an unescaped metacharacter with no group to belong to
            break;
        }

        strAppendBytes(&lit, &ch, 1);
        c->pos++;
    }

    if (ok)
        flushLit(c, &b, lit);
    strDestroy(&lit);

    if (!ok) {
        saDestroy(&b);
        return false;
    }

    out->elems = b;
    return true;
}

// entry points --------------------------------------------------------------------------------

_Use_decl_annotations_
StrPattern* _strPatternCreate(strref pat, flags_t flags)
{
    PCtx c       = { 0 };
    c.pat        = pat;
    c.len        = (int32)strLen(pat);
    c.flags      = flags;
    c.complexity = 1;
    c.gstart     = -1;
    saInit(&c.fields, ParseField, 0);

    ParseAlt root = { 0 };
    if (!parseSeq(&c, &root, false) || c.err) {
        saDestroy(&root.elems);
        saDestroy(&c.fields);
        cxerr = CX_InvalidArgument;
        return NULL;
    }

    StrPattern* p  = xaAlloc(sizeof(StrPattern), XA_Zero);
    p->root        = root;
    p->fields      = c.fields;
    p->npositional = c.npos;
    p->flags       = flags;
    p->nelems      = c.nelems;
    p->complexity  = (int32)c.complexity;

    return p;
}

_Use_decl_annotations_
void strPatternDestroy(StrPattern** pat)
{
    StrPattern* p = *pat;
    if (!p)
        return;

    saDestroy(&p->root.elems);
    saDestroy(&p->fields);
    xaFree(p);

    *pat = NULL;
}

_Use_decl_annotations_
bool _strParse(strref s, strref pat, int n, stvp* dests)
{
    StrPattern* p = _strPatternCreate(pat, 0);
    if (!p)
        return false;

    bool ok = _strPatternMatch(p, s, n, dests);
    strPatternDestroy(&p);
    return ok;
}

#if DEBUG_LEVEL >= 1
// Keep leak checkers happy; this list isn't actually used
static atomic(ptr) _parsePatternList;
#endif

static void compileDecl(void* data)
{
    StrPatternDecl* d = (StrPatternDecl*)data;

    d->compiled = _strPatternCreate(d->pat, d->flags);
    if (!d->compiled)
        return;

#if DEBUG_LEVEL >= 1
    void* head = atomicLoad(ptr, &_parsePatternList, Relaxed);
    do {
        d->next = (StrPatternDecl*)head;
    } while (!atomicCompareExchange(ptr, weak, &_parsePatternList, &head, d, Release, Relaxed));
#endif
}

_Use_decl_annotations_
StrPattern* _strPatGet(StrPatternDecl* decl)
{
    lazyInit(&decl->state, compileDecl, decl);
    return decl->compiled;
}
