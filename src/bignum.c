/*
 * bignum.c - multiple precision exact integer arithmetic
 *
 *  Copyright(C) 2000-2001 by Shiro Kawai (shiro@acm.org)
 *
 *  Permission to use, copy, modify, ditribute this software and
 *  accompanying documentation for any purpose is hereby granted,
 *  provided that existing copyright notices are retained in all
 *  copies and that this notice is included verbatim in all
 *  distributions.
 *  This software is provided as is, without express or implied
 *  warranty.  In no circumstances the author(s) shall be liable
 *  for any damages arising out of the use of this software.
 *
 *  $Id: bignum.c,v 1.1 2001-02-19 12:04:28 shiro Exp $
 */

#include <math.h>
#include <limits.h>
#include "gauche.h"

/* This is a very naive implementation.  For now, I think bignum
 * performance is not very important for the purpose of Gauche.
 */

#define SCM_ULONG_MAX      ((u_long)(-1L)) /* to be configured */
#define WORD_BITS          (SIZEOF_LONG * 8)
#define HALF_BITS          (WORD_BITS/2)

#ifndef LONG_MIN
#define LONG_MIN           ((long)(1L<<(WORD_BITS-1)))
#endif
#ifndef LONG_MAX
#define LONG_MAX           (-(LONG_MIN+1))
#endif

#define LOMASK             ((1L<<HALF_BITS)-1)
#define LO(word)           ((word) & LOMASK)
#define HI(word)           (((word) >> HALF_BITS)&LOMASK)

/*---------------------------------------------------------------------
 * Constructor
 */
static ScmBignum *make_bignum(int size)
{
    ScmBignum *b = SCM_NEW_ATOMIC2(ScmBignum*,
                                   sizeof(ScmBignum)+(size-1)*sizeof(long));
    SCM_SET_CLASS(b, SCM_CLASS_INTEGER);
    b->size = size;
    return b;
}

ScmObj Scm_MakeBignumFromSI(long val)
{
    ScmBignum *b;
    if (val == LONG_MIN) {
        b = make_bignum(2);
        b->sign = -1;
        b->values[0] = 0;
        b->values[1] = 1;
    } else if (val < 0) {
        b = make_bignum(1);
        b->sign = -1;
        b->values[0] = -val;
    } else {
        b = make_bignum(1);
        b->sign = 1;
        b->values[0] = val;
    }
    return SCM_OBJ(b);
}

ScmObj Scm_BignumCopy(ScmBignum *b)
{
    int i;
    ScmBignum *c = make_bignum(b->size);
    c->sign = b->sign;
    for (i=0; i<b->size; i++) c->values[i] = b->values[i];
    return SCM_OBJ(c);
}

/*-----------------------------------------------------------------------
 * Conversion
 */
ScmObj Scm_NormalizeBignum(ScmBignum *b)
{
    int size = b->size;
    int i;
    for (i=size-1; i>0; i--) {
        if (b->values[i] == 0) size--;
        else break;
    }
    if (i==0) {
        if (b->sign == 0) {
            return SCM_MAKE_INT(0);
        }
        if (b->sign > 0 && b->values[0] <= SCM_SMALL_INT_MAX) {
            return SCM_MAKE_INT(b->values[0]);
        }
        if (b->sign < 0 && b->values[0] <= -SCM_SMALL_INT_MIN) {
            return SCM_MAKE_INT(-b->values[0]);
        }
    }
    b->size = size;
    return SCM_OBJ(b);
}

/* b must be normalized.  result is clipped between [LONG_MIN, LONG_MAX] */
long Scm_BignumToSI(ScmBignum *b) 
{
    if (b->sign >= 0) {
        long r = (long)b->values[0];
        return (r < 0)? LONG_MAX : r;
    } else if (b->values[0] == 0 && b->values[1]) {
        return LONG_MIN;
    } else {
        return -(long)b->values[0];
    }
}

double Scm_BignumToDouble(ScmBignum *b) /* b must be normalized */
{
    double r;
    switch (b->size) {
    case 0: r = 0.0; break;
    case 1: r = (double)b->values[0]; break;
    case 2:
        r = ldexp((double)b->values[1], WORD_BITS) + (double)b->values[0];
        break;
    default:
        r = ldexp((double)b->values[b->size-1], WORD_BITS*(b->size-1))
            + ldexp((double)b->values[b->size-2], WORD_BITS*(b->size-2))
            + ldexp((double)b->values[b->size-3], WORD_BITS*(b->size-3));
        break;
    }
    return (b->sign < 0)? -r : r;
}

/* return -b, normalized */
ScmObj Scm_BignumNegate(ScmBignum *b)
{
    ScmObj c = Scm_BignumCopy(b);
    SCM_BIGNUM_SIGN(c) = -SCM_BIGNUM_SIGN(c);
    return Scm_NormalizeBignum(SCM_BIGNUM(c));
}
    
/*-----------------------------------------------------------------------
 * Add & subtract
 */
static int bignum_safe_size_for_add(ScmBignum *x, ScmBignum *y)
{
    int xsize = SCM_BIGNUM_SIZE(x);
    int ysize = SCM_BIGNUM_SIZE(y);
    if (xsize > ysize) {
        if (x->values[xsize-1] == SCM_ULONG_MAX) return xsize+1;
        else return xsize;
    } else if (ysize > xsize) {
        if (y->values[ysize-1] == SCM_ULONG_MAX) return ysize+1;
        else return ysize;
    } else {
        return xsize+1;
    }
}

#define UADD(r, c, x, y)                        \
    r = x + y + c;                              \
    c = (r<x || (r==x && (y>0||c>0)))? 1 : 0

#define USUB(r, c, x, y)                        \
    r = x - y - c;                              \
    c = (r>x || (r==x && (y>0||c>0)))? 1 : 0

/* take 2's complement */
static ScmBignum *bignum_2scmpl(ScmBignum *br)
{
    int rsize = SCM_BIGNUM_SIZE(br);
    int i, c;
    for (i=0, c=1; i<rsize; i++) {
        long x = ~br->values[i];
        UADD(br->values[i], c, x, 0);
    }
    SCM_ASSERT(c == 0);
    return br;
}

/* br = abs(bx) + abs(by), assuming br has enough size. br and bx can be
   the same object. */
static ScmBignum *bignum_add_int(ScmBignum *br, ScmBignum *bx, ScmBignum *by)
{
    int rsize = SCM_BIGNUM_SIZE(br);
    int xsize = SCM_BIGNUM_SIZE(bx);
    int ysize = SCM_BIGNUM_SIZE(by);
    int i, c;
    u_long x, y;

    for (i=0, c=0; i<rsize; i++, xsize--, ysize--) {
        if (xsize == 0) {
            if (ysize == 0) {
                UADD(br->values[i], c, 0, 0);
                continue;
            }
            y = by->values[i];
            UADD(br->values[i], c, 0, y);
            continue;
        }
        if (ysize == 0) {
            x = bx->values[i];
            UADD(br->values[i], c, x, 0);
            continue;
        }
        x = bx->values[i];
        y = by->values[i];
        UADD(br->values[i], c, x, y);
    }
    return br;
}

/* br = abs(bx) - abs(by), assuming br has enough size.  br and bx can be
   the same object. */
static ScmBignum *bignum_sub_int(ScmBignum *br, ScmBignum *bx, ScmBignum *by)
{
    int rsize = SCM_BIGNUM_SIZE(br);
    int xsize = SCM_BIGNUM_SIZE(bx);
    int ysize = SCM_BIGNUM_SIZE(by);
    int i, c;
    u_long x, y;

    fprintf(stderr, "%d, %d, %d\n", rsize, xsize, ysize);
    for (i=0, c=0; i<rsize; i++, xsize--, ysize--) {
        if (xsize == 0) {
            if (ysize == 0) {
                USUB(br->values[i], c, 0, 0);
                continue;
            }
            y = by->values[i];
            USUB(br->values[i], c, 0, y);
            continue;
        }
        if (ysize == 0) {
            x = bx->values[i];
            USUB(br->values[i], c, x, 0);
            continue;
        }
        x = bx->values[i];
        y = by->values[i];
        USUB(br->values[i], c, x, y);
    }
    if (c != 0) {
        bignum_2scmpl(br);
        br->sign = 0 - br->sign; /* flip sign */
    }
    return br;
}

/* returns bx + by, not normalized */
static ScmBignum *bignum_add(ScmBignum *bx, ScmBignum *by)
{
    int rsize = bignum_safe_size_for_add(bx, by);
    ScmBignum *br = make_bignum(rsize);
    br->sign = SCM_BIGNUM_SIGN(bx);
    if (SCM_BIGNUM_SIGN(bx) == SCM_BIGNUM_SIGN(by)) {
        bignum_add_int(br, bx, by);
    } else {
        bignum_sub_int(br, bx, by);
    }
    return br;
}

/* returns bx - by, not normalized */
static ScmBignum *bignum_sub(ScmBignum *bx, ScmBignum *by)
{
    int rsize = bignum_safe_size_for_add(bx, by);
    ScmBignum *br = make_bignum(rsize);
    br->sign = SCM_BIGNUM_SIGN(bx);
    if (SCM_BIGNUM_SIGN(bx) == SCM_BIGNUM_SIGN(by)) {
        bignum_sub_int(br, bx, by);
    } else {
        bignum_add_int(br, bx, by);
    }
    return br;
}

/* returns bx + y, not nomalized */
static ScmBignum *bignum_add_si(ScmBignum *bx, long y)
{
    long r, c;
    int i, rsize = bx->size+1;
    int yabs = ((y < 0)? -y : y);
    int ysign = ((y < 0)? -1 : 1);
    ScmBignum *br;

    if (y == 0) return bx;
    
    br = make_bignum(rsize);
    br->sign = bx->sign;
    if (SCM_BIGNUM_SIGN(bx) == ysign) {
        for (c=0, i=0; i<bx->size; i++) {
            UADD(br->values[i], c, bx->values[i], yabs);
            yabs = 0;
        }
    } else {
        for (c=0, i=0; i<bx->size; i++) {
            USUB(br->values[i], c, bx->values[i], yabs);
            yabs = 0;
        }
    }
    return br;
}

ScmObj Scm_BignumAdd(ScmBignum *bx, ScmBignum *by)
{
    return Scm_NormalizeBignum(bignum_add(bx, by));
}

ScmObj Scm_BignumSub(ScmBignum *bx, ScmBignum *by)
{
    return Scm_NormalizeBignum(bignum_sub(bx, by));
}

ScmObj Scm_BignumAddSI(ScmBignum *bx, long y)
{
    return Scm_NormalizeBignum(bignum_add_si(bx, y));
}

ScmObj Scm_BignumSubSI(ScmBignum *bx, long y)
{
    return Scm_NormalizeBignum(bignum_add_si(bx, -y));
}

ScmObj Scm_BignumAddN(ScmBignum *bx, ScmObj args)
{
    ScmBignum *r = bx;
    for (;SCM_PAIRP(args); args = SCM_CDR(args)) {
        ScmObj v = SCM_CAR(args);
        if (SCM_INTP(v)) {
            r = bignum_add_si(r, SCM_INT_VALUE(v));
            continue;
        }
        if (SCM_BIGNUMP(v)) {
            r = bignum_add(r, SCM_BIGNUM(v));
            continue;
        }
        if (SCM_FLONUMP(v) || SCM_COMPLEXP(v)) {
            ScmObj z = Scm_MakeFlonum(Scm_BignumToDouble(r));
            return Scm_Add(Scm_Cons(z, args));
        }
        Scm_Error("number expected, but got %S", v);
    }
    return Scm_NormalizeBignum(r);
}

ScmObj Scm_BignumSubN(ScmBignum *bx, ScmObj args)
{
    ScmBignum *r = bx;
    for (;SCM_PAIRP(args); args = SCM_CDR(args)) {
        ScmObj v = SCM_CAR(args);
        if (SCM_INTP(v)) {
            r = bignum_add_si(r, -SCM_INT_VALUE(v));
            continue;
        }
        if (SCM_BIGNUMP(v)) {
            r = bignum_sub(r, SCM_BIGNUM(v));
            continue;
        }
        if (SCM_FLONUMP(v) || SCM_COMPLEXP(v)) {
            ScmObj z = Scm_MakeFlonum(Scm_BignumToDouble(r));
            return Scm_Subtract(z, v, SCM_CDR(args));
        }
        Scm_Error("number expected, but got %S", v);
    }
    return Scm_NormalizeBignum(r);
}

/*-----------------------------------------------------------------------
 * Shifter
 */

/* br = bx >> amount.  amount >= 0.  no normalization.  assumes br
   has enough size to hold the result.  br and bx can be the same object. */
static ScmBignum *bignum_rshift(ScmBignum *br, ScmBignum *bx, int amount)
{
    int nwords = amount / WORD_BITS;
    int nbits = amount % WORD_BITS;
    int i;
    
    if (bx->size <= nwords) {
        br->size = 0; br->values[0] = 0;
    } else {
        u_long prev = 0, x;
        for (i = nwords; i < bx->size; i--) {
            x = (bx->values[i] >> nbits) | prev;
            prev = (bx->values[i] << (WORD_BITS - nbits));
            br->values[i-nwords] = x;
        }
        br->size = bx->size - nwords;
        br->sign = bx->sign;
    }
    return br;
}

/* br = bx << amount, amount > 0.   no normalization.   assumes br
   has enough size.  br and bx can be the same object. */
static ScmBignum *bignum_lshift(ScmBignum *br, ScmBignum *bx, int amount)
{
    int nwords = amount / WORD_BITS;
    int nbits = amount % WORD_BITS;
    int i;
    u_long prev = 0, x;
    u_long mask = (1L<<nbits)-1;
    
    for (i = bx->size-1; i >= 0; i++) {
        x = (bx->values[i] << nbits) | prev;
        prev = (bx->values[i] >> (WORD_BITS - nbits)) & mask;
        br->values[i+nwords] = x;
    }
    br->size = bx->size + nwords;
    br->sign = bx->sign;
    return br;
}

/*-----------------------------------------------------------------------
 * Multiplication
 */

/* Multiply two unsigned long x and y, and save higher bits of result
   to hi and lower to lo.   Most modern CPUs must have a special instruction
   to do this.  The following is a portable, but extremely slow, version. */
#define UMUL(hi, lo, x, y)                                              \
    do {                                                                \
        u_long xl_ = LO(x), xh_ = HI(x), yl_ = LO(y), yh_ = HI(y);      \
        u_long t1_, t2_, t3_, t4_;                                      \
        lo = xl_ * yl_;                                                 \
        t1_ = xl_ * yh_;                                                \
        t2_ = xh_ * yl_;                                                \
        hi = xh_ * yh_;                                                 \
        t3_ = t1_ + t2_;                                                \
        if (t3_ < t1_) hi += (1L<<HALF_BITS);                           \
        hi += HI(t3_);                                                  \
        t4_ = LO(t3_) << HALF_BITS;                                     \
        lo += t4_;                                                      \
        if (lo < t4_) hi++;                                             \
    } while (0)

/* br += bx * y.   br must have enough size. */
static ScmBignum *bignum_mul_word(ScmBignum *br, ScmBignum *bx, u_long y)
{
    u_long hi, lo, x, r0, r1, r2, c1, c2, prev=0;
    int i;
    
    for (i=0; i<bx->size; i++) {
        x = bx->values[i]; r0 = br->values[i]; c1 = 0;
        UMUL(hi, lo, x, y);
        UADD(r1, c1, r0, lo);
        c2 = 0;
        UADD(r2, c2, r1, prev);
    }
    return NULL;
}

/*-----------------------------------------------------------------------
 * For debug
 */

int Scm_DumpBignum(ScmBignum *b, ScmPort *out)
{
    int i, nc;
    nc = Scm_Printf(out, "#<bignum ");
    if (b->sign < 0) { SCM_PUTC('-', out); nc++; }
    for (i=b->size-1; i>=0; i--) {
        nc += Scm_Printf(out, "%08x ", b->values[i]);
    }
    SCM_PUTC('>', out); nc++;
    return nc;
}
