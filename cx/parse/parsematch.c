#include "parse_private.h"

#include "cx/utils/compare.h"

// Hard ceiling on how much work one match may do. Alternation without repetition keeps
// backtracking bounded by the number of branches rather than exponential in the input,
// but nesting still multiplies, so a pathological pattern has to fail rather than hang.
#define PARSE_MAXBUDGET (1 << 22)

// What is still left to match after the sequence currently being walked. Groups make this
// necessary: a branch has to know what follows the group before it can tell whether it is
// the branch that works.
typedef struct MCont {
    const ParseAlt* seq;
    int32 idx;
    const struct MCont* next;
} MCont;

typedef struct MCtx {
    const StrPattern* pat;
    strref s;
    int32 len;

    StrPatSpan* spans;
    int32 nspans;
    int32 maxspans;

    int32 budget;
    bool requireEnd;
    bool casei;

    int32 endpos;
    int32 furthest;   // best position any attempt reached, for reporting where it broke
} MCtx;

static bool matchSeq(MCtx* m, const ParseAlt* seq, int32 idx, int32 pos, const MCont* k);

static bool pushSpan(MCtx* m, int32 field, int32 off, int32 len)
{
    // A skipped field matched, but nothing is ever going to be written from it, so there is
    // no reason to remember where it was
    if (m->pat->fields.a[field].skip)
        return true;

    if (m->nspans >= m->maxspans)
        return false;

    m->spans[m->nspans].field = field;
    m->spans[m->nspans].off   = off;
    m->spans[m->nspans].len   = len;
    m->nspans++;
    return true;
}

static int digitVal(uint8 c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    return -1;
}

static bool inSet(strref set, uint8 c, bool casei)
{
    uint32 n = strLen(set);
    for (uint32 i = 0; i < n; i++) {
        uint8 sc = strGetChar(set, (int32)i);
        if (sc == c || (casei && tolower(sc) == tolower(c)))
            return true;
    }
    return false;
}

// numeric and boolean fields ------------------------------------------------------------

// How far a numeric field reaches from `pos`, or -1 if there is no number of the right
// shape there. `vstart` comes back as the first byte of the number itself, which is not
// `pos` when the field allowed whitespace in front of it.
static int32 delimitNum(MCtx* m, const ParseField* f, int32 pos, int32* vstart)
{
    int32 i = pos;
    if (f->ws)
        while (i < m->len && isspace(strGetChar(m->s, i))) i++;

    int32 start = i;
    bool neg    = false;

    if (f->ptype == PARSE_int && i < m->len) {
        uint8 c = strGetChar(m->s, i);
        if (c == '-' || c == '+') {
            neg = (c == '-');
            i++;
        }
    }

    int32 dstart = i;
    uint64 acc   = 0;
    bool ovf     = false;

    while (i < m->len) {
        int d = digitVal(strGetChar(m->s, i));
        if (d < 0 || d >= f->base)
            break;
        if (f->maxdigits && (i - dstart) >= f->maxdigits)
            break;

        if (acc > (MAX_UINT64 - (uint64)d) / (uint64)f->base)
            ovf = true;
        else
            acc = acc * (uint64)f->base + (uint64)d;

        i++;
    }

    int32 ndigits = i - dstart;
    if (ndigits == 0 || ovf)
        return -1;
    if (f->digits && ndigits != f->digits)
        return -1;

    // The range check belongs here rather than at binding time: a value outside min/max
    // has to fail the *match*, so that an alternative branch still gets its turn.
    if (f->ptype == PARSE_uint) {
        if (f->hasmin && f->minval > 0 && acc < (uint64)f->minval)
            return -1;
        if (f->hasmax && (f->maxval < 0 || acc > (uint64)f->maxval))
            return -1;
    } else {
        if (acc > (uint64)MAX_INT64 + (neg ? 1 : 0))
            return -1;
        int64 v = (int64)(neg ? (uint64)0 - acc : acc);
        if (f->hasmin && v < f->minval)
            return -1;
        if (f->hasmax && v > f->maxval)
            return -1;
    }

    *vstart = start;
    return i;
}

static int32 delimitFloat(MCtx* m, const ParseField* f, int32 pos, int32* vstart)
{
    int32 i = pos;
    if (f->ws)
        while (i < m->len && isspace(strGetChar(m->s, i))) i++;

    int32 start = i;
    if (i < m->len && (strGetChar(m->s, i) == '-' || strGetChar(m->s, i) == '+'))
        i++;

    if (strRangeEqi(m->s, _SL("inf"), i, 3) || strRangeEqi(m->s, _SL("nan"), i, 3)) {
        *vstart = start;
        return i + 3;
    }

    int32 digits = 0;
    while (i < m->len && isdigit(strGetChar(m->s, i))) {
        i++;
        digits++;
    }

    if (i < m->len && strGetChar(m->s, i) == '.') {
        i++;
        while (i < m->len && isdigit(strGetChar(m->s, i))) {
            i++;
            digits++;
        }
    }

    if (digits == 0)
        return -1;

    // An exponent only counts when it actually has digits behind it, so "1e" is the number
    // 1 followed by a letter rather than a broken float.
    if (!f->fixed && i < m->len && (strGetChar(m->s, i) == 'e' || strGetChar(m->s, i) == 'E')) {
        int32 e = i + 1;
        if (e < m->len && (strGetChar(m->s, e) == '-' || strGetChar(m->s, e) == '+'))
            e++;
        if (e < m->len && isdigit(strGetChar(m->s, e))) {
            while (e < m->len && isdigit(strGetChar(m->s, e))) e++;
            i = e;
        }
    }

    *vstart = start;
    return i;
}

static int32 delimitBool(MCtx* m, const ParseField* f, int32 pos, int32* vstart)
{
    int32 i = pos;
    if (f->ws)
        while (i < m->len && isspace(strGetChar(m->s, i))) i++;

    int32 start = i;
    while (i < m->len && isalnum(strGetChar(m->s, i))) i++;

    if (i == start)
        return -1;

    uint32 n = (uint32)(i - start);
    if (!strRangeEqi(m->s, _SL("true"), start, n) && !strRangeEqi(m->s, _SL("false"), start, n) &&
        !strRangeEqi(m->s, _SL("yes"), start, n) && !strRangeEqi(m->s, _SL("no"), start, n) &&
        !strRangeEq(m->s, _SL("1"), start, n) && !strRangeEq(m->s, _SL("0"), start, n))
        return -1;

    *vstart = start;
    return i;
}

// text fields -----------------------------------------------------------------------------

// Where a quoted string at `pos` starts and ends. The span covers the text between the
// quotes exactly as it appeared; the escapes come out at binding time.
static bool delimitQuoted(MCtx* m, int32 pos, int32* b, int32* e, int32* after)
{
    if (pos >= m->len || strGetChar(m->s, pos) != '"')
        return false;

    bool escaped = false;
    for (int32 i = pos + 1; i < m->len; i++) {
        uint8 c = strGetChar(m->s, i);
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            *b     = pos + 1;
            *e     = i;
            *after = i + 1;
            return true;
        }
    }

    return false;
}

typedef enum { DELIM_None, DELIM_Lit, DELIM_WS } DelimKind;

// What the pattern demands immediately after this element, when it is something a string
// search can jump straight to. A plain ${string} stops at the first place that thing turns
// up, which is what makes it non-greedy -- and narrowing the candidates to the places the
// next element could possibly start is what keeps it from testing every byte in turn.
static DelimKind nextDelim(const ParseAlt* seq, int32 idx, const MCont* k, strref* lit)
{
    const ParseElem* e = NULL;

    if (idx + 1 < saSize(seq->elems)) {
        e = &seq->elems.a[idx + 1];
    } else {
        while (k && k->idx >= saSize(k->seq->elems)) k = k->next;
        if (k)
            e = &k->seq->elems.a[k->idx];
    }

    if (!e)
        return DELIM_None;
    if (e->kind == PE_WS)
        return DELIM_WS;
    if (e->kind != PE_Literal)
        return DELIM_None;

    *lit = e->lit;
    return DELIM_Lit;
}

// Try one candidate span for a text field and carry on with the rest of the pattern.
static bool tryText(MCtx* m, const ParseAlt* seq, int32 idx, int32 spanoff, int32 spanlen,
                    int32 next, const MCont* k)
{
    const ParseElem* e  = &seq->elems.a[idx];
    const ParseField* f = &m->pat->fields.a[e->field];

    // With a default in play, an empty result is what "did not match" means for a text
    // field, and the default attempt at the end of matchField covers that position.
    if (spanlen == 0 && f->hasdef)
        return false;

    int32 save = m->nspans;
    if (pushSpan(m, e->field, spanoff, spanlen) && matchSeq(m, seq, idx + 1, next, k))
        return true;

    m->nspans = save;
    return false;
}

static bool matchTextField(MCtx* m, const ParseAlt* seq, int32 idx, int32 pos, const MCont* k)
{
    const ParseElem* e  = &seq->elems.a[idx];
    const ParseField* f = &m->pat->fields.a[e->field];

    if (f->quoted) {
        int32 b, qe, after;
        if (!delimitQuoted(m, pos, &b, &qe, &after))
            return false;
        return tryText(m, seq, idx, b, qe - b, after, k);
    }

    if (f->rest)
        return tryText(m, seq, idx, pos, m->len - pos, m->len, k);

    if (f->len) {
        if (pos + f->len > m->len)
            return false;
        return tryText(m, seq, idx, pos, f->len, pos + f->len, k);
    }

    if (f->until) {
        int32 hit = m->casei ? strFindi(m->s, pos, f->until) : strFind(m->s, pos, f->until);
        if (hit < 0)
            return false;
        return tryText(m, seq, idx, pos, hit - pos, hit, k);
    }

    if (f->chars) {
        int32 end = pos;
        while (end < m->len && inSet(f->chars, strGetChar(m->s, end), m->casei) != f->notchars)
            end++;
        return tryText(m, seq, idx, pos, end - pos, end, k);
    }

    if (f->word) {
        int32 end = pos;
        while (end < m->len && !isspace(strGetChar(m->s, end))) end++;
        return tryText(m, seq, idx, pos, end - pos, end, k);
    }

    strref lit   = NULL;
    DelimKind dk = nextDelim(seq, idx, k, &lit);
    if (dk != DELIM_None) {
        for (int32 p = pos;;) {
            int32 hit = (dk == DELIM_WS) ?
                strFindAny(m->s, p, _SL(" \t\r\n\v\f")) :
                (m->casei ? strFindi(m->s, p, lit) : strFind(m->s, p, lit));
            if (hit < 0)
                return false;
            if (tryText(m, seq, idx, pos, hit - pos, hit, k))
                return true;
            p = hit + 1;
        }
    }

    for (int32 end = pos; end <= m->len; end++) {
        if (tryText(m, seq, idx, pos, end - pos, end, k))
            return true;
    }

    return false;
}

static bool matchField(MCtx* m, const ParseAlt* seq, int32 idx, int32 pos, const MCont* k)
{
    const ParseElem* e  = &seq->elems.a[idx];
    const ParseField* f = &m->pat->fields.a[e->field];

    switch (f->ptype) {
    case PARSE_string:
    case PARSE_object:
        if (matchTextField(m, seq, idx, pos, k))
            return true;
        break;

    default: {
        int32 vstart = pos, end;
        if (f->ptype == PARSE_float)
            end = delimitFloat(m, f, pos, &vstart);
        else if (f->ptype == PARSE_bool)
            end = delimitBool(m, f, pos, &vstart);
        else
            end = delimitNum(m, f, pos, &vstart);

        if (end >= 0) {
            int32 save = m->nspans;
            if (pushSpan(m, e->field, vstart, end - vstart) && matchSeq(m, seq, idx + 1, end, k))
                return true;
            m->nspans = save;
        }
        break;
    }
    }

    // A default is the last resort, and it consumes nothing: the literals around the
    // placeholder still have to match from exactly where the field would have started.
    if (f->hasdef) {
        int32 save = m->nspans;
        if (pushSpan(m, e->field, -1, 0) && matchSeq(m, seq, idx + 1, pos, k))
            return true;
        m->nspans = save;
    }

    return false;
}

// the walk ----------------------------------------------------------------------------------

static bool matchSeq(MCtx* m, const ParseAlt* seq, int32 idx, int32 pos, const MCont* k)
{
    if (--m->budget < 0)
        return false;

    if (pos > m->furthest)
        m->furthest = pos;

    if (idx >= saSize(seq->elems)) {
        if (k)
            return matchSeq(m, k->seq, k->idx, pos, k->next);

        if (m->requireEnd && pos != m->len)
            return false;

        m->endpos = pos;
        return true;
    }

    const ParseElem* e = &seq->elems.a[idx];

    switch (e->kind) {
    case PE_Literal: {
        uint32 n = strLen(e->lit);
        if (pos + (int32)n > m->len)
            return false;
        if (!(m->casei ? strRangeEqi(m->s, e->lit, pos, n) : strRangeEq(m->s, e->lit, pos, n)))
            return false;
        return matchSeq(m, seq, idx + 1, pos + (int32)n, k);
    }

    case PE_WS: {
        int32 p = pos;
        while (p < m->len && isspace(strGetChar(m->s, p))) p++;
        if (p == pos)
            return false;
        return matchSeq(m, seq, idx + 1, p, k);
    }

    case PE_Group: {
        MCont kk = { seq, idx + 1, k };

        int32 nalts = saSize(e->alts);
        for (int32 i = 0; i < nalts; i++) {
            int32 save = m->nspans;
            if ((e->field < 0 || pushSpan(m, e->field, i + 1, 0)) &&
                matchSeq(m, &e->alts.a[i], 0, pos, &kk))
                return true;
            m->nspans = save;
        }

        if (e->optional) {
            int32 save = m->nspans;
            if ((e->field < 0 || pushSpan(m, e->field, 0, 0)) && matchSeq(m, seq, idx + 1, pos, k))
                return true;
            m->nspans = save;
        }

        return false;
    }

    default:
        return matchField(m, seq, idx, pos, k);
    }
}

_Use_decl_annotations_
bool _parseRun(const StrPattern* pat, strref s, int32 start, bool requireEnd, StrPatSpan* spans,
               int32* nspans, int32* endpos)
{
    MCtx m = { 0 };

    m.pat        = pat;
    m.s          = s;
    m.len        = (int32)strLen(s);
    m.spans      = spans;
    m.maxspans   = saSize(pat->fields);
    m.requireEnd = requireEnd;
    m.casei      = (pat->flags & STRPAT_CaseI) != 0;

    if (start < 0 || start > m.len)
        return false;

    int64 budget = (int64)pat->complexity * (pat->nelems + 1) * (m.len + 1) + 1024;
    m.budget     = (int32)clamphigh(budget, PARSE_MAXBUDGET);

    if (!matchSeq(&m, &pat->root, 0, start, NULL)) {
        *endpos = m.furthest;
        return false;
    }

    *nspans = m.nspans;
    *endpos = m.endpos;
    return true;
}
