// =====================================================================
// bffsree.c
// =====================================================================
#ifdef BFFSREE_IMPLEMENTATION

#define _CRT_SECURE_NO_WARNINGS
#include "bffsree.h"

#ifndef _refInterp
#define _refInterp 0
#endif

// One indirect jump per opcode is the point of threaded dispatch: the
// predictor keys on the jump address. GCC's crossjumping pass merges
// the identical dispatch tails back into a handful of shared jumps (7
// of 17 at -O3) and folds the peeled first iteration of the scan
// loops back into the loop; long and factor lose 30-45% against the
// same source under Clang. Turning the pass off for this translation
// unit costs nothing elsewhere. File scope so every function here
// shares one option set and inlining decisions are unaffected.
#if defined(__GNUC__) && !defined(__clang__) && !defined(BF_KEEP_CROSSJUMPING)
#pragma GCC optimize("no-crossjumping")
#endif

// Hot functions start on a 64-byte boundary. Without this, the dispatch
// loop's position relative to fetch / uop-cache windows depended on the
// size of every function linked before it, and an edit to an unrelated
// helper moved factor by 6% and long by 15% with byte-identical Eval
// code. Aligning Eval and each out-of-line helper makes a function's
// speed a property of its own code again. BF_HOT_ALIGN_BYTES=0 turns it
// off.
#ifndef BF_HOT_ALIGN_BYTES
#define BF_HOT_ALIGN_BYTES 64
#endif
#if defined(__GNUC__) && BF_HOT_ALIGN_BYTES > 0
#define BF_HOT_ALIGN __attribute__((aligned(BF_HOT_ALIGN_BYTES)))
#else
#define BF_HOT_ALIGN
#endif
#if defined(__GNUC__)
#define BF_NOINLINE __attribute__((noinline)) BF_HOT_ALIGN
#define BF_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define BF_NOINLINE
#define BF_ALWAYS_INLINE inline
#endif

#if BF_WORD_SCAN && BF_CELL_BITS == 8 && !_refInterp
#define BF_WORD_SCAN3 1

#if defined(_WIN32) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define BF_WORD_BYTE0 UINT64_C(0x0000000000000080)
#define BF_WORD_BYTE3 UINT64_C(0x0000000080000000)
#define BF_WORD_BYTE6 UINT64_C(0x0080000000000000)
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BF_WORD_BYTE0 UINT64_C(0x8000000000000000)
#define BF_WORD_BYTE3 UINT64_C(0x0000008000000000)
#define BF_WORD_BYTE6 UINT64_C(0x0000000000008000)
#endif

static uint64_t bf_zero_bytes64(uint64_t x) {
    const uint64_t low7 = UINT64_C(0x7f7f7f7f7f7f7f7f);
    const uint64_t high = UINT64_C(0x8080808080808080);
    return ~(((x & low7) + low7) | x | low7) & high;
}

// One branch per word inside the loop: mask the three lanes the
// stride visits and test them together, then resolve which lane
// stopped us after the loop. Three in-loop tests compiled to three
// taken/not-taken branches per word on GCC and the loop ran ~20%
// slower than Clang's; with one exit the two compilers agree.
static bf_cell* bf_word_scan3_forward(bf_cell* p) {
#if BF_WORD_SCAN_GUARD
    bf_cell* scalar = p;
    while (*scalar) scalar += 3;
#endif

    for (;;) {
        uint64_t cells;
        uint64_t zeros;
        memcpy(&cells, p, sizeof(cells));
        zeros = bf_zero_bytes64(cells);
#if defined(BF_WORD_BYTE0)
        zeros &= BF_WORD_BYTE0 | BF_WORD_BYTE3 | BF_WORD_BYTE6;
        if (zeros) {
            if (!(zeros & BF_WORD_BYTE0))
                p += (zeros & BF_WORD_BYTE3) ? 3 : 6;
            break;
        }
#else
        {
            const unsigned char* z = (const unsigned char*)&zeros;
            if (z[0] | z[3] | z[6]) {
                if (!z[0]) p += z[3] ? 3 : 6;
                break;
            }
        }
#endif
        p += 9;
    }

#if BF_WORD_SCAN_GUARD
    if (p != scalar) abort();
#endif
    return p;
}

static bf_cell* bf_word_scan3_backward(bf_cell* p) {
#if BF_WORD_SCAN_GUARD
    bf_cell* scalar = p;
    while (*scalar) scalar -= 3;
#endif

    for (;;) {
        bf_cell* base = p - 6;
        uint64_t cells;
        uint64_t zeros;
        memcpy(&cells, base, sizeof(cells));
        zeros = bf_zero_bytes64(cells);
#if defined(BF_WORD_BYTE0)
        zeros &= BF_WORD_BYTE0 | BF_WORD_BYTE3 | BF_WORD_BYTE6;
        if (zeros) {
            if (!(zeros & BF_WORD_BYTE6))
                p -= (zeros & BF_WORD_BYTE3) ? 3 : 6;
            break;
        }
#else
        {
            const unsigned char* z = (const unsigned char*)&zeros;
            if (z[0] | z[3] | z[6]) {
                if (!z[6]) p -= z[3] ? 3 : 6;
                break;
            }
        }
#endif
        p -= 9;
    }

#if BF_WORD_SCAN_GUARD
    if (p != scalar) abort();
#endif
    return p;
}
#else
#define BF_WORD_SCAN3 0
#endif

// Pure IR inspection, used by the optimizer in every build.
int bf_looprun_variant(const bf_op* L) {
    if (L->val == 5 &&
        L[1].cmd == bfo_VAL_MZ && L[1].val == 1 &&
        L[2].cmd == bfo_VAL_MUL && L[2].val == 1 &&
        L[3].cmd == bfo_VAL_MZ && L[3].val == 1 &&
        L[4].cmd == bfo_VAL) {
        // Hops one 9-cell record and touches only cells inside it.
        const int foff = L->off;
        if (L[4].off == 9 && L[1].off == -foff && L[2].off == 0 && L[3].off == 0 &&
            L[1].buf == -foff && L[2].buf == foff &&
            foff >= 0 && foff <= 8 && L[3].buf >= 0 && L[3].buf <= 8)
            return BF_SEG_LOOPRUN_FRAME9;
        return BF_SEG_LOOPRUN_MZ_MUL_MZ_VAL;
    }
    if (L->val == 5 &&
        L[1].cmd == bfo_VAL &&
        L[2].cmd == bfo_VAL_MUL &&
        L[3].cmd == bfo_VAL_MZ &&
        L[4].cmd == bfo_VAL_MZ)
        return BF_SEG_LOOPRUN_VAL_MUL_MZ_MZ;
#if BF_AFFINE && BF_AFFINE_APPLY
    if (L->aux) {
        const bf_affine* m = bf_affine_get(L->aux);
        if (m) {
            switch (m->kind) {
            case BF_AFF_S1:  return BF_SEG_LOOPRUN_AFF_S1;
            case BF_AFF_S2Z: return BF_SEG_LOOPRUN_AFF_S2Z;
            case BF_AFF_S2:  return BF_SEG_LOOPRUN_AFF_S2;
            case BF_AFF_S3:  return BF_SEG_LOOPRUN_AFF_S3;
            default: break;
            }
        }
    }
#endif
    return BF_SEG_LOOPRUN;
}

int bf_mzscan_variant(const bf_op* M) {
    if (M->buf != 0 || M[1].val != 1) return BF_MZ_GENERIC;
    if (M[1].buf == 9) {
        if (M->off == 1 && M[1].off == -10) return BF_MZ_COPY9_FROM1;
        if (M->off == 2 && M[1].off == -11) return BF_MZ_COPY9_FROM2;
        return BF_MZ_SLIDE9;
    }
    return BF_MZ_COPY;
}

#if !_refInterp
#if BF_PROFILE
static unsigned long long* bf_prof_counts;
static bf_op* bf_prof_base;
#define bf_prof_hit(op) do { \
    if (bf_prof_counts) bf_prof_counts[(op) - bf_prof_base]++; \
} while (0)
#else
#define bf_prof_hit(op) do { } while (0)
#endif

#if !BF_PROFILE
// Out of line so the computed-goto Eval stays small. Inlining these
// into Eval changes register allocation for every opcode, including
// PTR_S, and that made BF_FAST slower than the checked build.
static BF_NOINLINE bf_cell* bf_mzscan_copy(bf_cell* p, int foff, int dest, int boff) {
    if (*p) {
        p += foff;
        for (;;) {
            bf_cell v = *p;
            *p = 0;
            p[dest] += v;
            p += boff;
            if (*p == 0) break;
            p += foff;
        }
    }
    return p;
}

// Hottest dest-9 slides: lane +1 / +2 with boff = -(9+lane). Walks
// backward a frame at a time, moving the lane into the next frame.
// Two frames per trip: the second move lands in the cell the first
// just cleared, so it is a plain store, and one pointer step serves
// both. On mandelbrot these run 16 frames per call.
static BF_ALWAYS_INLINE bf_cell* bf_mzscan_copy9_walk(bf_cell* p, int lane) {
    if (*p) {
        p += lane;
        for (;;) {
            bf_cell v = *p;
            *p = 0;
            p[9] += v;
            if (p[-9 - lane] == 0) { p -= 9 + lane; break; }
            v = p[-9];
            p[-9] = 0;
            *p += v;
            if (p[-18 - lane] == 0) { p -= 18 + lane; break; }
            p -= 18;
        }
    }
    return p;
}

static BF_NOINLINE bf_cell* bf_mzscan_copy9_from1(bf_cell* p) {
    return bf_mzscan_copy9_walk(p, 1);
}

static BF_NOINLINE bf_cell* bf_mzscan_copy9_from2(bf_cell* p) {
    return bf_mzscan_copy9_walk(p, 2);
}

// Slide one lane into the next 9-cell frame: p[9] += *p; *p = 0.
// Any stride-9 record; dest is fixed at 9.
static BF_NOINLINE bf_cell* bf_mzscan_slide9(bf_cell* p, int foff, int boff) {
    if (*p) {
        p += foff;
        for (;;) {
            bf_cell v = *p;
            *p = 0;
            p[9] += v;
            p += boff;
            if (*p == 0) break;
            p += foff;
        }
    }
    return p;
}
#endif

#if !BF_PROFILE
// VAL_MZ / VAL_MUL / VAL_MZ / VAL that hops one 9-cell frame. The
// working cells sit in one record; keep them in locals and step +9.
static BF_NOINLINE bf_cell* bf_frame9_mz_mul_mz_inc(
    bf_cell* p, bf_cell fbuf, int lane, int acc, bf_cell add)
{
    if (*p) {
        *p += fbuf;
        p += lane;
        for (;;) {
            bf_cell* base = p - lane;
            bf_cell t = *p;
            bf_cell v0;
            *p = 0;
            v0 = (bf_cell)(base[0] + t);
            *p = v0;
            base[acc] = (bf_cell)(base[acc] + v0);
            base[0] = add;
            p = base + 9;
            if (*p == 0) break;
            *p += fbuf;
            p += lane;
        }
    }
    return p;
}

// Hottest LOOPRUN shape: decrement/walk, then VAL_MZ, VAL_MUL, VAL_MZ,
// VAL. The 9-cell frame case is its own variant (BF_SEG_LOOPRUN_FRAME9),
// selected at optimize time.
static BF_NOINLINE bf_cell* bf_looprun_mz_mul_mz_val(
    bf_cell* p,
    bf_cell fbuf, int foff,
    int a_buf, int a_off,
    int b_buf, int b_off,
    int c_buf, int c_off,
    bf_cell add, int d_off)
{
    if (*p) {
        *p += fbuf;
        p += foff;
        for (;;) {
            bf_cell v = *p;
            *p = 0;
            p[a_buf] += v;
            p += a_off;
            p[b_buf] += *p;
            p += b_off;
            v = *p;
            *p = 0;
            p[c_buf] += v;
            p += c_off;
            *p += add;
            p += d_off;
            if (*p == 0) break;
            *p += fbuf;
            p += foff;
        }
    }
    return p;
}
#endif

static bf_cell* bf_exec_ops(bf_cell* p, bf_op* b, bf_op* end);

static BF_HOT_ALIGN bf_cell* bf_exec_fwd(bf_cell* p, bf_op* P) {
    if (*p) {
        *p += (bf_cell)P->buf;
        p += P->off;
        for (;;) {
            p = bf_exec_ops(p, P + 1, P + P->val);
            if (*p == 0) break;
            bf_prof_hit(P);
            *p += (bf_cell)P->buf;
            p += P->off;
        }
    }
    return p;
}

static BF_NOINLINE bf_cell* bf_looprun_generic(bf_cell* p, bf_op* P) {
    return bf_exec_fwd(p, P);
}


// Stride scan, four cells per iteration. The four loads are
// independent, so the not-taken tests overlap; the scalar loop is
// bound by one taken branch per cell. Reads up to three strides past
// the first zero: strides are under 128 cells and the sentinel pads
// are BF_TAPE_PAD, so that stays inside the allocation. Eight-way and
// a branchless zero-mask combine were both measured slower.
//
// Two entry points. Eval calls the out-of-line bf_apply_ptr_s so the
// scan never shares registers with computed-goto dispatch (fib/tree
// spend almost all their time here). Nest templates, whose bodies are
// plain loops with the parameters in locals, inline bf_scan_stride:
// on mandelbrot they make 22M scans of ~20 cells, and the call plus
// prologue was a sixth of each.
#ifndef BF_SCAN_UNROLL
#define BF_SCAN_UNROLL 1
#endif
static BF_ALWAYS_INLINE bf_cell* bf_scan_stride(bf_cell* p, int stride) {
#if BF_WORD_SCAN3
    if (stride == 3) return bf_word_scan3_forward(p);
    if (stride == -3) return bf_word_scan3_backward(p);
#endif
#if BF_SCAN_UNROLL
    // One induction variable: the exits record an offset and fall out
    // through a single return. Written as four `return p + k*stride`
    // exits, GCC keeps four pointers live across the loop (one add
    // each per iteration) and spills two callee-saved registers.
    const intptr_t s = stride, s2 = 2 * s, s3 = 3 * s, s4 = 4 * s;
    intptr_t k;
    for (;;) {
        bf_cell a = p[0], b = p[s], c = p[s2], d = p[s3];
        if (a == 0) { k = 0; break; }
        if (b == 0) { k = s; break; }
        if (c == 0) { k = s2; break; }
        if (d == 0) { k = s3; break; }
        p += s4;
    }
    return p + k;
#else
    while (*p) p += stride;
    return p;
#endif
}

static BF_NOINLINE bf_cell* bf_apply_ptr_s(bf_cell* p, int stride) {
    return bf_scan_stride(p, stride);
}

#if !BF_PROFILE
// Common 4-op LOOPRUN: VAL, VAL_MUL, VAL_MZ, VAL_MZ. *p != 0 on entry.
// The body is a forced-inline function so the nest template that
// contains this loop can hoist its thirteen parameters into locals
// once instead of marshaling them (seven on the stack, six saved
// registers) on every entry.
static BF_ALWAYS_INLINE bf_cell* bf_walk_val_mul_mz_mz(
    bf_cell* p,
    bf_cell fbuf, int foff,
    bf_cell add, int a_off,
    int b_val, int b_buf, int b_off,
    int c_val, int c_buf, int c_off,
    int d_val, int d_buf, int d_off)
{
    *p += fbuf;
    p += foff;
    for (;;) {
        *p += add;
        p += a_off;
        p[b_buf] += (bf_cell)(b_val * *p);
        p += b_off;
        p[c_buf] += (bf_cell)(c_val * *p);
        *p = 0;
        p += c_off;
        p[d_buf] += (bf_cell)(d_val * *p);
        *p = 0;
        p += d_off;
        if (*p == 0) break;
        *p += fbuf;
        p += foff;
    }
    return p;
}

static BF_NOINLINE bf_cell* bf_looprun_val_mul_mz_mz(
    bf_cell* p,
    bf_cell fbuf, int foff,
    bf_cell add, int a_off,
    int b_val, int b_buf, int b_off,
    int c_val, int c_buf, int c_off,
    int d_val, int d_buf, int d_off)
{
    if (*p)
        p = bf_walk_val_mul_mz_mz(p, fbuf, foff, add, a_off,
                                  b_val, b_buf, b_off, c_val, c_buf, c_off,
                                  d_val, d_buf, d_off);
    return p;
}
#endif

static inline bf_cell* bf_apply_rew(bf_cell* p, const bf_op* rew) {
    *p += (bf_cell)rew->buf;
    return p + rew->off;
}

// ZFILL: k = +n fills p[0..n-1], k = -n fills p[-(n-1)..0]. Small
// fills (the common case) are straight-line stores; a memset call
// would cost more than the dispatches it replaces.
static inline void bf_zfill(bf_cell* p, int k, bf_cell v) {
    if (k < 0) { p += k + 1; k = -k; }
    switch (k) {
    case 8: p[7] = v; /* fall through */
    case 7: p[6] = v; /* fall through */
    case 6: p[5] = v; /* fall through */
    case 5: p[4] = v; /* fall through */
    case 4: p[3] = v; /* fall through */
    case 3: p[2] = v; /* fall through */
    case 2: p[1] = v; /* fall through */
    case 1: p[0] = v; return;
    default: break;
    }
    if (sizeof(bf_cell) == 1) {
        memset(p, (int)(unsigned char)v, (size_t)k);
    } else {
        while (k-- > 0) *p++ = v;
    }
}

#if !BF_PROFILE
#if BF_AFFINE && BF_AFFINE_APPLY
// Lane evaluator. The map's stores are lanes of one 64-bit word (see
// bf_affine_pack); a hop is four cell loads, four multiply-adds, and
// one byte extract per store. Nothing in the descriptor is read
// inside the loop, so tape stores (bf_cell is a character type and
// aliases everything) force no reloads. Three walkers by store count.
#define BF_AFF_LANE_BITS (2 * BF_CELL_BITS)
#define BF_AFF_HOP(b, p, acc) do { \
    acc = (b).bias \
        + (uint64_t)(p)[(b).soff[0]] * (b).cvec[0] \
        + (uint64_t)(p)[(b).soff[1]] * (b).cvec[1] \
        + (uint64_t)(p)[(b).soff[2]] * (b).cvec[2] \
        + (uint64_t)(p)[(b).soff[3]] * (b).cvec[3]; } while (0)
#define BF_AFF_LANE(acc, s) ((bf_cell)((acc) >> (BF_AFF_LANE_BITS * (s))))

static BF_NOINLINE bf_cell* bf_aff_walk_s1(
    bf_cell* restrict p, bf_cell fbuf, int foff, const bf_aff_bound* restrict b)
{
    const int d0 = b->dst[0], hop = b->hop;
    if (*p) {
        *p += fbuf;
        p += foff;
        for (;;) {
            uint64_t acc;
            BF_AFF_HOP(*b, p, acc);
            p[d0] = BF_AFF_LANE(acc, 0);
            p += hop;
            if (*p == 0) break;
            *p += fbuf;
            p += foff;
        }
    }
    return p;
}

static BF_NOINLINE bf_cell* bf_aff_walk_s2(
    bf_cell* restrict p, bf_cell fbuf, int foff, const bf_aff_bound* restrict b)
{
    const int d0 = b->dst[0], d1 = b->dst[1], hop = b->hop;
    if (*p) {
        *p += fbuf;
        p += foff;
        for (;;) {
            uint64_t acc;
            BF_AFF_HOP(*b, p, acc);
            p[d0] = BF_AFF_LANE(acc, 0);
            p[d1] = BF_AFF_LANE(acc, 1);
            p += hop;
            if (*p == 0) break;
            *p += fbuf;
            p += foff;
        }
    }
    return p;
}

static BF_NOINLINE bf_cell* bf_aff_walk_s3(
    bf_cell* restrict p, bf_cell fbuf, int foff, const bf_aff_bound* restrict b)
{
    const int d0 = b->dst[0], d1 = b->dst[1], d2 = b->dst[2], hop = b->hop;
    if (*p) {
        *p += fbuf;
        p += foff;
        for (;;) {
            uint64_t acc;
            BF_AFF_HOP(*b, p, acc);
            p[d0] = BF_AFF_LANE(acc, 0);
            p[d1] = BF_AFF_LANE(acc, 1);
            p[d2] = BF_AFF_LANE(acc, 2);
            p += hop;
            if (*p == 0) break;
            *p += fbuf;
            p += foff;
        }
    }
    return p;
}
#endif

// Run a LOOPRUN whose cell is known nonzero, by pre-decoded variant.
// Stops at the failed loop test; the caller applies the REW slot.
static inline bf_cell* bf_looprun_taken(bf_cell* p, bf_op* L, int variant) {
    switch (variant) {
    case BF_SEG_LOOPRUN_FRAME9:
        return bf_frame9_mz_mul_mz_inc(
            p, (bf_cell)L->buf, L->off, L[3].buf, (bf_cell)L[4].val);
    case BF_SEG_LOOPRUN_MZ_MUL_MZ_VAL:
        return bf_looprun_mz_mul_mz_val(
            p, (bf_cell)L->buf, L->off,
            L[1].buf, L[1].off, L[2].buf, L[2].off,
            L[3].buf, L[3].off, (bf_cell)L[4].val, L[4].off);
    case BF_SEG_LOOPRUN_VAL_MUL_MZ_MZ:
        return bf_looprun_val_mul_mz_mz(
            p, (bf_cell)L->buf, L->off,
            (bf_cell)L[1].val, L[1].off,
            L[2].val, L[2].buf, L[2].off,
            L[3].val, L[3].buf, L[3].off,
            L[4].val, L[4].buf, L[4].off);
#if BF_AFFINE && BF_AFFINE_APPLY
    case BF_SEG_LOOPRUN_AFF_S1:
        return bf_aff_walk_s1(p, (bf_cell)L->buf, L->off, &bf_affine_get(L->aux)->bound);
    case BF_SEG_LOOPRUN_AFF_S2Z:
    case BF_SEG_LOOPRUN_AFF_S2:
        return bf_aff_walk_s2(p, (bf_cell)L->buf, L->off, &bf_affine_get(L->aux)->bound);
    case BF_SEG_LOOPRUN_AFF_S3:
        return bf_aff_walk_s3(p, (bf_cell)L->buf, L->off, &bf_affine_get(L->aux)->bound);
#endif
    default:
        return bf_looprun_generic(p, L);
    }
}
#endif

static inline bf_cell* bf_run_looprun(bf_cell* p, bf_op* L) {
#if BF_PROFILE
    p = bf_looprun_generic(p, L);
#else
    if (*p) p = bf_looprun_taken(p, L, L->sub);
#endif
    return bf_apply_rew(p, L + L->val);
}

// Run an MZSCAN whose cell is known nonzero, by pre-decoded variant.
// Stops at the failed loop test; the caller applies the REW slot.
static inline bf_cell* bf_mzscan_taken(bf_cell* p, bf_op* M, int variant) {
#if !BF_PROFILE
    switch (variant) {
    case BF_MZ_COPY9_FROM1: return bf_mzscan_copy9_from1(p);
    case BF_MZ_COPY9_FROM2: return bf_mzscan_copy9_from2(p);
    case BF_MZ_SLIDE9:      return bf_mzscan_slide9(p, M->off, M[1].off);
    case BF_MZ_COPY:        return bf_mzscan_copy(p, M->off, M[1].buf, M[1].off);
    default: break;
    }
#else
    (void)variant;
#endif
    *p += (bf_cell)M->buf;
    p += M->off;
    for (;;) {
        p[M[1].buf] += (bf_cell)(M[1].val * *p);
        *p = 0;
        p += M[1].off;
        if (*p == 0) break;
        bf_prof_hit(M);
        *p += (bf_cell)M->buf;
        p += M->off;
    }
    return p;
}

static inline bf_cell* bf_run_mzscan(bf_cell* p, bf_op* M) {
    if (*p) p = bf_mzscan_taken(p, M, M->sub);
    return bf_apply_rew(p, M + 2);
}

static inline bf_cell* bf_run_valscan(bf_cell* p, bf_op* M) {
    if (*p) {
        *p += (bf_cell)M->buf;
        p += M->off;
        for (;;) {
            *p += (bf_cell)M[1].val;
            p += M[1].off;
            if (*p == 0) break;
            bf_prof_hit(M);
            *p += (bf_cell)M->buf;
            p += M->off;
        }
    }
    return bf_apply_rew(p, M + 2);
}

// ---------------------------------------------------------------------
// NEST runner. The body was compiled to segments once; each segment
// is a bound helper call or a straight-line affine block, so an
// iteration costs one small switch per segment instead of one Eval
// dispatch per op. Child nests recurse through bf_nest_loop.
// ---------------------------------------------------------------------
#if BF_PROFILE
static bf_cell* bf_nest_run(bf_cell* p, bf_op* L) {
    return bf_exec_fwd(p, L);
}
#else
static BF_NOINLINE bf_cell* bf_nest_run(bf_cell* p, bf_op* L);

#if BF_AFFINE && BF_AFFINE_APPLY
// One application of a compiled affine tree, by kind.
static inline bf_cell* bf_aff_apply(bf_cell* p, const bf_affine* m) {
    const bf_aff_bound* b = &m->bound;
    uint64_t acc;
    BF_AFF_HOP(*b, p, acc);
    switch (m->kind) {
    case BF_AFF_S3:
        p[b->dst[2]] = BF_AFF_LANE(acc, 2);
        /* fall through */
    case BF_AFF_S2Z:
    case BF_AFF_S2:
        p[b->dst[1]] = BF_AFF_LANE(acc, 1);
        /* fall through */
    case BF_AFF_S1:
        p[b->dst[0]] = BF_AFF_LANE(acc, 0);
        break;
    default:
        break;
    }
    return p + b->hop;
}
#endif

// Every loop-carrying segment tests its cell before calling anything:
// inner loops are entered far more often than they are taken, and an
// untaken entry must cost about what Eval's inline FWD test costs.
static inline bf_cell* bf_nest_seg(bf_cell* p, bf_op* L, const bf_seg* g) {
    bf_op* C;
    switch (g->kind) {
#if BF_AFFINE && BF_AFFINE_APPLY
    case BF_SEG_AFF:
        return bf_aff_apply(p, g->aff);
#endif
    case BF_SEG_OPS:
        return bf_exec_ops(p, L + g->a, L + g->b);
    case BF_SEG_PTRS:
        if (*p) p = bf_apply_ptr_s(p, g->a);
        return p + g->off;
    case BF_SEG_MZSCAN:
        return bf_run_mzscan(p, L + g->a);
    case BF_SEG_VALSCAN:
        return bf_run_valscan(p, L + g->a);
    case BF_SEG_LOOPRUN:
    case BF_SEG_LOOPRUN_MZ_MUL_MZ_VAL:
    case BF_SEG_LOOPRUN_FRAME9:
    case BF_SEG_LOOPRUN_VAL_MUL_MZ_MZ:
    case BF_SEG_LOOPRUN_AFF_S1:
    case BF_SEG_LOOPRUN_AFF_S2Z:
    case BF_SEG_LOOPRUN_AFF_S2:
    case BF_SEG_LOOPRUN_AFF_S3:
        C = L + g->a;
        if (*p) p = bf_looprun_taken(p, C, g->kind);
        return bf_apply_rew(p, C + C->val);
    case BF_SEG_NEST:
        C = L + g->a;
        if (*p) p = bf_nest_run(p, C);
        return bf_apply_rew(p, C + C->val);
    case BF_SEG_FWD:
        C = L + g->a;
        if (*p) p = bf_exec_fwd(p, C);
        return bf_apply_rew(p, C + C->val);
    default:
        return p;
    }
}

// Template "ZVRMV": VAL_ZERO VAL | LOOPRUN | VAL_MZ VAL. Everything
// the body needs is in locals. When the LOOPRUN is the VAL_MUL_MZ_MZ
// shape (mandelbrot's innermost loop) its walk is inlined with its
// parameters hoisted too, so the body makes no calls at all; other
// shapes go through the specialized helper, and only when the cell
// is nonzero.
#define BF_T_ZV_R_MV_LOOP(RUN) do { \
    *p += fbuf; \
    p += foff; \
    for (;;) { \
        *p = zval; \
        p += zoff; \
        *p += v1; \
        p += v1off; \
        if (*p) RUN; \
        *p += rbuf; \
        p += roff; \
        p[mbuf] += (bf_cell)(mval * *p); \
        *p = 0; \
        p += moff; \
        *p += v2; \
        p += v2off; \
        if (*p == 0) break; \
        *p += fbuf; \
        p += foff; \
    } } while (0)

static BF_NOINLINE bf_cell* bf_nest_t_zv_r_mv(bf_cell* p, bf_op* L) {
    bf_op* Z  = L + 1;
    bf_op* V1 = L + 2;
    bf_op* R  = L + 3;
    bf_op* RR = R + R->val;
    bf_op* M  = RR + 1;
    bf_op* V2 = RR + 2;
    const bf_cell fbuf = (bf_cell)L->buf;  const int foff  = L->off;
    const bf_cell zval = (bf_cell)Z->val;  const int zoff  = Z->off;
    const bf_cell v1   = (bf_cell)V1->val; const int v1off = V1->off;
    const int     rsub = R->sub;
    const bf_cell rbuf = (bf_cell)RR->buf; const int roff  = RR->off;
    const int     mbuf = M->buf;           const int moff  = M->off;
    const bf_cell mval = (bf_cell)M->val;
    const bf_cell v2   = (bf_cell)V2->val; const int v2off = V2->off;

    if (rsub == BF_SEG_LOOPRUN_VAL_MUL_MZ_MZ) {
        const bf_cell Rbuf = (bf_cell)R->buf;    const int Roff  = R->off;
        const bf_cell aadd = (bf_cell)R[1].val;  const int aoff  = R[1].off;
        const int bval = R[2].val, bbuf = R[2].buf, boff = R[2].off;
        const int cval = R[3].val, cbuf = R[3].buf, coff = R[3].off;
        const int dval = R[4].val, dbuf = R[4].buf, doff = R[4].off;
        BF_T_ZV_R_MV_LOOP(
            p = bf_walk_val_mul_mz_mz(p, Rbuf, Roff, aadd, aoff,
                                      bval, bbuf, boff, cval, cbuf, coff,
                                      dval, dbuf, doff));
        return p;
    }
    BF_T_ZV_R_MV_LOOP(p = bf_looprun_taken(p, R, rsub));
    return p;
}
#undef BF_T_ZV_R_MV_LOOP

// Template "VM{VRS}SmMV": VAL VAL_MZ | { VAL LOOPRUN PTR_S } | PTR_S
// MZSCAN | VAL_MZ VAL. The nested loop is inlined; every loop-carrying
// piece tests its cell before calling its helper.
static BF_NOINLINE bf_cell* bf_nest_t_vm_vrs_s_m_mv(bf_cell* p, bf_op* L) {
    bf_op* V1 = L + 1;
    bf_op* M1 = L + 2;
    bf_op* F  = L + 3;
    bf_op* V2 = F + 1;
    bf_op* R  = F + 2;
    bf_op* RR = R + R->val;
    bf_op* S1 = RR + 1;
    bf_op* FR = F + F->val;
    bf_op* S2 = FR + 1;
    bf_op* Z  = S2 + 1;
    bf_op* M2 = Z + 3;
    bf_op* V3 = Z + 4;
    const bf_cell fbuf  = (bf_cell)L->buf;  const int foff  = L->off;
    const bf_cell v1    = (bf_cell)V1->val; const int v1off = V1->off;
    const int     m1buf = M1->buf;          const int m1off = M1->off;
    const bf_cell m1val = (bf_cell)M1->val;
    const bf_cell Fbuf  = (bf_cell)F->buf;  const int Foff  = F->off;
    const bf_cell v2    = (bf_cell)V2->val; const int v2off = V2->off;
    const int     rsub  = R->sub;
    const bf_cell rbuf  = (bf_cell)RR->buf; const int roff  = RR->off;
    const int     s1    = S1->val;          const int s1off = S1->off;
    const bf_cell FRbuf = (bf_cell)FR->buf; const int FRoff = FR->off;
    const int     s2    = S2->val;          const int s2off = S2->off;
    const int     zsub  = Z->sub;
    const bf_cell zrbuf = (bf_cell)Z[2].buf; const int zroff = Z[2].off;
    const int     m2buf = M2->buf;          const int m2off = M2->off;
    const bf_cell m2val = (bf_cell)M2->val;
    const bf_cell v3    = (bf_cell)V3->val; const int v3off = V3->off;

    *p += fbuf;
    p += foff;
    for (;;) {
        *p += v1;
        p += v1off;
        p[m1buf] += (bf_cell)(m1val * *p);
        *p = 0;
        p += m1off;
        if (*p) {
            *p += Fbuf;
            p += Foff;
            for (;;) {
                *p += v2;
                p += v2off;
                if (*p) p = bf_looprun_taken(p, R, rsub);
                *p += rbuf;
                p += roff;
                if (*p) p = bf_scan_stride(p, s1);
                p += s1off;
                if (*p == 0) break;
                *p += Fbuf;
                p += Foff;
            }
        }
        *p += FRbuf;
        p += FRoff;
        if (*p) p = bf_scan_stride(p, s2);
        p += s2off;
        if (*p) p = bf_mzscan_taken(p, Z, zsub);
        *p += zrbuf;
        p += zroff;
        p[m2buf] += (bf_cell)(m2val * *p);
        *p = 0;
        p += m2off;
        *p += v3;
        p += v3off;
        if (*p == 0) break;
        *p += fbuf;
        p += foff;
    }
    return p;
}

// Templates "SVFSV" / "SVSV": PTR_S VAL [ZFILL] PTR_S VAL — a flat
// loop that scans out along a record array, marks or clears at the
// end, scans back and counts down. No inner loop, so LOOPRUN cannot
// take it, and Eval dispatched each of its 4-5 ops per iteration.
// One body; the fill is a compile-time constant in each wrapper.
static BF_ALWAYS_INLINE bf_cell* bf_nest_t_sv_f_sv_body(bf_cell* p, bf_op* L, int fill) {
    bf_op* S1 = L + 1;
    bf_op* V1 = L + 2;
    bf_op* F  = L + 3;
    bf_op* S2 = L + 3 + fill;
    bf_op* V2 = S2 + 1;
    const bf_cell fbuf = (bf_cell)L->buf;  const int foff  = L->off;
    const int     s1   = S1->val;          const int s1off = S1->off;
    const bf_cell v1   = (bf_cell)V1->val; const int v1off = V1->off;
    const int     fk   = fill ? F->buf : 0;
    const bf_cell fv   = fill ? (bf_cell)F->val : 0;
    const int     Foff = fill ? F->off : 0;
    const int     s2   = S2->val;          const int s2off = S2->off;
    const bf_cell v2   = (bf_cell)V2->val; const int v2off = V2->off;

    *p += fbuf;
    p += foff;
    for (;;) {
        if (*p) p = bf_scan_stride(p, s1);
        p += s1off;
        *p += v1;
        p += v1off;
        if (fill) {
            bf_zfill(p, fk, fv);
            p += Foff;
        }
        if (*p) p = bf_scan_stride(p, s2);
        p += s2off;
        *p += v2;
        p += v2off;
        if (*p == 0) break;
        *p += fbuf;
        p += foff;
    }
    return p;
}

static BF_NOINLINE bf_cell* bf_nest_t_sv_f_sv(bf_cell* p, bf_op* L) {
    return bf_nest_t_sv_f_sv_body(p, L, 1);
}

static BF_NOINLINE bf_cell* bf_nest_t_sv_sv(bf_cell* p, bf_op* L) {
    return bf_nest_t_sv_f_sv_body(p, L, 0);
}

// *p != 0 on entry. Stops at the failed loop test (REW not applied).
static BF_NOINLINE bf_cell* bf_nest_run(bf_cell* p, bf_op* L) {
    const bf_nest* n = bf_nest_get(L->aux);
    if (!n) return bf_exec_fwd(p, L);
    switch (n->tmpl) {
    case BF_TMPL_ZV_R_MV:       return bf_nest_t_zv_r_mv(p, L);
    case BF_TMPL_VM_VRS_S_m_MV: return bf_nest_t_vm_vrs_s_m_mv(p, L);
    case BF_TMPL_S_V_F_S_V:     return bf_nest_t_sv_f_sv(p, L);
    case BF_TMPL_S_V_S_V:       return bf_nest_t_sv_sv(p, L);
    default: break;
    }
    *p += (bf_cell)L->buf;
    p += L->off;
    for (;;) {
        int i;
        for (i = 0; i < n->nseg; i++)
            p = bf_nest_seg(p, L, &n->seg[i]);
        if (*p == 0) break;
        *p += (bf_cell)L->buf;
        p += L->off;
    }
    return p;
}
#endif // BF_PROFILE

// Interpret a walkable IR range. Nested FWD/LOOPRUN/scans are executed
// here so Eval only dispatches the outer LOOPRUN.
static BF_HOT_ALIGN bf_cell* bf_exec_ops(bf_cell* p, bf_op* b, bf_op* end) {
    while (b < end) {
        bf_prof_hit(b);
        switch (b->cmd) {
        case bfo_VAL:
            *p += (bf_cell)b->val;
            p += b->off;
            b++;
            break;
        case bfo_VAL_MZ:
            p[b->buf] += (bf_cell)(b->val * *p);
            *p = 0;
            p += b->off;
            b++;
            break;
        case bfo_VAL_MUL:
            p[b->buf] += (bf_cell)(b->val * *p);
            p += b->off;
            b++;
            break;
        case bfo_VAL_ZERO:
            *p = (bf_cell)b->val;
            p += b->off;
            b++;
            break;
        case bfo_ZFILL:
            bf_zfill(p, b->buf, (bf_cell)b->val);
            p += b->off;
            b++;
            break;
        case bfo_MUL_MUL:
            p[b->buf] *= (bf_cell)(b->val * *p);
            p += b->off;
            b++;
            break;
        case bfo_NOOP:
            p += b->off;
            b++;
            break;
        case bfo_PTR_S:
            p = bf_apply_ptr_s(p, b->val);
            p += b->off;
            b++;
            break;
        case bfo_MZSCAN:
            p = bf_run_mzscan(p, b);
            b += 3;
            break;
        case bfo_VALSCAN:
            p = bf_run_valscan(p, b);
            b += 3;
            break;
        case bfo_LOOPRUN:
            p = bf_run_looprun(p, b);
            b += b->val + 1;
            break;
        case bfo_NEST:
            if (*p) p = bf_nest_run(p, b);
            p = bf_apply_rew(p, b + b->val);
            b += b->val + 1;
            break;
        case bfo_FWD:
            p = bf_exec_fwd(p, b);
            p = bf_apply_rew(p, b + b->val);
            b += b->val + 1;
            break;
        default:
            p += b->off;
            b++;
            break;
        }
    }
    return p;
}
#endif

// =====================================================================
// main VM loop for bfi
// =====================================================================
BF_HOT_ALIGN int bffsree_Eval(bf_VM* vm, char* inp, int ocount) {
    bf_cell* ptr = vm->tape;
    int ptrLen = vm->tapeLen;
#if _refInterp
    char* chars  = vm->prog;
    bf_VM_help* ph = vm->progHelper;
#else
    bf_op* bfo   = (bf_op*)vm->prog_op;
    bf_cell* tp;
#endif
    int pc = vm->pc;
    int sp = vm->sp;
#if _refInterp
    int c, icount = ocount;
#else
    int icount = ocount;
#endif

    if (ptr == 0) {
        if (ptrLen == 0) ptrLen = bf_MAXCELLS;
        ptr = (bf_cell*)calloc((size_t)ptrLen + 2u * BF_TAPE_PAD, sizeof(bf_cell)) + BF_TAPE_PAD;
    }

#if _refInterp
    do {
        if (_mybounds(sp, ptrLen)) goto ERROR_BF;
        switch (c = chars[pc]) {
        default:        /*nothing*/                                         break;
        case bf_GT:     sp += ph[pc].v;                                     break;
        case bf_LT:     sp -= ph[pc].v;                                     break;
        case bf_PLUS:   ptr[sp] += (bf_cell)ph[pc].v;                       break;
        case bf_MINUS:  ptr[sp] -= (bf_cell)ph[pc].v;                       break;
        case bf_PERIOD: vm->putcp(vm->putdata, ptr[sp]);                    break;
        case bf_COMMA:  ptr[sp] = (inp && *inp) ? (bf_cell)*inp++ : (bf_cell)vm->getcp(vm->getdata); break;
        case bf_OPEN:
            if (ptr[sp] != 0) break;
            else              pc = ph[pc].v;
            break;
        case bf_CLOSE:
            if (ptr[sp] == 0) break;
            else              pc = ph[pc].v;
            break;
        case bf_EOP:
        case 0:
            pc = -1;
            goto DONE;
        }
        pc++;
    } while (icount--);

DONE:
    if (pc < 0) {
        vm->pc = -1;
        if (ptr && vm->tape == 0) free(ptr - BF_TAPE_PAD);
    } else {
        vm->pc = pc;
        vm->sp = sp;
    }
#else
#if BF_PROFILE
    #define _bf_prof(op)  do { if (vm->prof) vm->prof[(op) - (bf_op*)vm->prog_op]++; } while (0)
    bf_prof_counts = vm->prof;
    bf_prof_base = (bf_op*)vm->prog_op;
#else
    #define _bf_prof(op)  do { } while (0)
#endif
#if !defined(NDEBUG)
    #define _bf_tick()  do { if (icount-- <= 0) goto DONE; } while (0)
#else
    #define _bf_tick()  do { } while (0)
#endif

    // -----------------------------------------------------------------
    // Op semantics: the single source of truth. P is the op being
    // executed (bfo in the dispatch arms, the body cursor inside
    // LOOPRUN). Each op leaves bfo on the slot whose off the dispatch
    // tail must apply: jumps land on the FWD/REW partner, block ops
    // land on their last parameter slot.
    // -----------------------------------------------------------------
    #define _op_NOOP(P)     do { } while (0)
    #define _op_VAL(P)      do { ptr[sp] += (bf_cell)(P)->val; } while (0)
    #define _op_PUT(P)      do { vm->putcp(vm->putdata, ptr[sp]); } while (0)
    #define _op_GET(P)      do { ptr[sp] = (inp && *inp) ? (bf_cell)*inp++ \
                                         : (bf_cell)vm->getcp(vm->getdata); } while (0)
    #define _op_FWD(P)      do { if (ptr[sp] == 0) (P) += (P)->val; \
                                 ptr[sp] += (bf_cell)(P)->buf; } while (0)
    #define _op_REW(P)      do { if (ptr[sp] != 0) (P) += (P)->val; \
                                 ptr[sp] += (bf_cell)(P)->buf; } while (0)
#ifdef BF_FAST
    #define _bf_bound_sp()  do { } while (0)
#else
    #define _bf_bound_sp()  do { if (_mybounds(sp, ptrLen)) goto ERROR_BF; } while (0)
#endif
    #define _op_PTR_S(P)    do { tp = bf_apply_ptr_s(ptr + sp, (P)->val); \
                                 sp = (int)(tp - ptr); \
                                 _bf_bound_sp(); } while (0)
    #define _op_VAL_MZ(P)   do { ptr[sp + (P)->buf] += (bf_cell)((P)->val * ptr[sp]); \
                                 ptr[sp] = 0; } while (0)
    #define _op_VAL_MUL(P)  do { ptr[sp + (P)->buf] += (bf_cell)((P)->val * ptr[sp]); } while (0)
    #define _op_VAL_ZERO(P) do { ptr[sp] = (bf_cell)(P)->val; } while (0)
#ifdef BF_FAST
    #define _op_ZFILL(P)    do { bf_zfill(ptr + sp, (P)->buf, (bf_cell)(P)->val); } while (0)
#else
    // Checked build: the whole fill range must be on the tape, as the
    // per-cell VAL_ZERO ops it replaced would have required.
    #define _op_ZFILL(P)    do { int k_ = (P)->buf; \
                                 int far_ = sp + (k_ > 0 ? k_ - 1 : k_ + 1); \
                                 if (_mybounds(far_, ptrLen)) goto ERROR_BF; \
                                 bf_zfill(ptr + sp, k_, (bf_cell)(P)->val); } while (0)
#endif
    #define _op_MUL_MUL(P)  do { ptr[sp + (P)->buf] *= (bf_cell)((P)->val * ptr[sp]); } while (0)
    #define _op_EOP(P)      do { bfo = 0; goto DONE; } while (0)

    // MZSCAN/VALSCAN: a walking loop with a one-op body, run as a
    // single op. The original FWD/body/REW ops stay in place as the
    // parameter block ((P)[0]=FWD, (P)[1]=body, (P)[2]=REW); exact
    // FWD/REW semantics are reproduced, including buf/off application
    // on loop-back and exit, and the unchecked multiply target (parity
    // with _op_VAL_MZ). A walk off the tape stops in the zero sentinel
    // pad, so one post-loop check preserves the exact error behavior.
    // Note: internal loops do not honor the !NDEBUG chunking tick, so
    // a long walk completes within one bffsree_Eval call.
    #define _op_SCANLOOP(P, WORK) \
        do { \
            if (ptr[sp] != 0) { \
                ptr[sp] += (bf_cell)(P)->buf; \
                sp += (P)->off; \
                for (;;) { \
                    WORK; \
                    sp += (P)[1].off; \
                    if (ptr[sp] == 0) break; \
                    _bf_prof(P); \
                    ptr[sp] += (bf_cell)(P)->buf; \
                    sp += (P)->off; \
                } \
                _bf_bound_sp(); \
            } \
            (P) += 2; \
            ptr[sp] += (bf_cell)(P)->buf; \
        } while (0)
    #define _op_VALSCAN(P)  _op_SCANLOOP(P, ptr[sp] += (bf_cell)(P)[1].val)

#if BF_PROFILE
    #define _op_MZSCAN(P)   _op_SCANLOOP(P, ptr[sp + (P)[1].buf] += (bf_cell)((P)[1].val * ptr[sp]); \
                                            ptr[sp] = 0)
#else
    #define _op_MZSCAN(P) \
        do { \
            if ((P)->sub != BF_MZ_GENERIC) { \
                if (ptr[sp] != 0) { \
                    tp = bf_mzscan_taken(ptr + sp, (P), (P)->sub); \
                    sp = (int)(tp - ptr); \
                    _bf_bound_sp(); \
                } \
                (P) += 2; \
                ptr[sp] += (bf_cell)(P)->buf; \
            } else { \
                _op_SCANLOOP(P, ptr[sp + (P)[1].buf] += (bf_cell)((P)[1].val * ptr[sp]); \
                                 ptr[sp] = 0); \
            } \
        } while (0)
#endif

    // LOOPRUN: walking body interpreted internally. Sentinel pads let
    // a walk leave the logical tape; one post-loop bounds check
    // matches MZSCAN. The helper variant was decoded into sub at
    // optimize time; an untaken loop costs only the cell test.
#if BF_PROFILE
    #define _op_LOOPRUN(P) \
        do { \
            tp = bf_looprun_generic(ptr + sp, (P)); \
            sp = (int)(tp - ptr); \
            _bf_bound_sp(); \
            (P) += (P)->val; \
            ptr[sp] += (bf_cell)(P)->buf; \
        } while (0)
#else
    #define _op_LOOPRUN(P) \
        do { \
            if (ptr[sp] != 0) { \
                tp = bf_looprun_taken(ptr + sp, (P), (P)->sub); \
                sp = (int)(tp - ptr); \
                _bf_bound_sp(); \
            } \
            (P) += (P)->val; \
            ptr[sp] += (bf_cell)(P)->buf; \
        } while (0)
#endif

    // NEST: compiled segment body, run internally; same exit protocol
    // as LOOPRUN (helper stops at the loop test, Eval applies the REW).
    #define _op_NEST(P) \
        do { \
            if (ptr[sp] != 0) { \
                tp = bf_nest_run(ptr + sp, (P)); \
                sp = (int)(tp - ptr); \
                _bf_bound_sp(); \
            } \
            (P) += (P)->val; \
            ptr[sp] += (bf_cell)(P)->buf; \
        } while (0)

    // Dispatch tail, shared by both arms. BF_FAST drops the per-op
    // bounds check (sentinel pads keep accesses in-allocation).
#ifdef BF_FAST
    #define _bf_tail()  do { sp += bfo->off; bfo++; _bf_tick(); } while (0)
#else
    #define _bf_tail()  do { sp += bfo->off; bfo++; \
                             if (_mybounds(sp, ptrLen)) goto ERROR_BF; \
                             _bf_tick(); } while (0)
#endif

    bfo += pc;
#if BF_USE_CGOTO
    {
    static void* bf_labels[bfo_Total] = {
#define X(n) &&L_##n,
    BF_OP_LIST(X)
#undef X
    };
    #define _bf_next()  do { _bf_tail(); _bf_prof(bfo); \
                             goto *bf_labels[bfo->cmd]; } while (0)

    if (_mybounds(sp, ptrLen)) goto ERROR_BF;
    _bf_prof(bfo);
    goto *bf_labels[bfo->cmd];

#define X(n) L_##n: _op_##n(bfo); _bf_next();
    BF_OP_LIST(X)
#undef X

    #undef _bf_next
    }
#else
    do {
        _bf_prof(bfo);
        switch (bfo->cmd) {
#define X(n) case bfo_##n: _op_##n(bfo); break;
    BF_OP_LIST(X)
#undef X
        }
        _bf_tail();
    } while (1);
#endif // BF_USE_CGOTO
    #undef _bf_tail
    #undef _bf_tick
    #undef _bf_prof
    #undef _bf_bound_sp

DONE:
    if (bfo == 0) {
        vm->pc = -1;
        if (ptr && vm->tape == 0) free(ptr - BF_TAPE_PAD);
    } else {
        vm->pc = (int)(bfo - (bf_op*)vm->prog_op);
        vm->sp = sp;
    }
#endif

    return ocount - icount + 1;

ERROR_BF:
    printf("// memory exception\n");
#if _refInterp
    pc = -1;
#else
    bfo = 0;
#endif
    goto DONE;
}

// =====================================================================
// bf_readfile - utility function
// =====================================================================
static int bf_readfile(char** data, FILE* fh) {
    int ci = 0, c, ps = bf_MEMDEFAULT - 1;
    (*data) = (char*)malloc((size_t)ps + 1);
    while ((c = getc(fh)) > 0) {
        (*data)[ci++] = (char)c;
        _myresize(*data, ps, ci + 1);
    }
    (*data)[ci++] = 0;
    (*data) = (char*)realloc(*data, (size_t)ci);
    return ci;
}

// =====================================================================
// main
// =====================================================================
int bffsree_Main(int argc, char* argv[]) {
    int carg = 1, proglen, printBF = 0, i;
    int ci = 0, c, ps = 0, psh = 0, lc = 0, metric = 0;
    char *prog = 0, *inp = 0;
    unsigned char dc[256] = {0};
    bf_VM_help* progHelp = 0;
    bf_VM vm;
    FILE* fh = 0;

    // options
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0)      { if (i == 1) carg++; printBF = 1; }
        else if (strcmp(argv[i], "-j") == 0) { if (i == 1) carg++; printBF = 2; }
        else if (strcmp(argv[i], "-m") == 0) { if (i == 1) carg++; metric = 1; }
    }

    if (argc > carg) {
        fh = fopen(argv[carg], "r");
        if (fh == 0) { printf("//unable to open file [%s]\n", argv[carg]); return -1; }
    } else {
        fh = stdin;
    }

    // read program
    dc['>'] = bf_GT;     dc['<'] = bf_LT;    dc['+'] = bf_PLUS; dc['-'] = bf_MINUS;
    dc['.'] = bf_PERIOD; dc[','] = bf_COMMA; dc['['] = bf_OPEN; dc[']'] = bf_CLOSE;
    while ((c = getc(fh)) > 0) {
        if (c == '!') break;  // input
        if (c == '%' || c == ';')  // comment
            do { c = getc(fh); } while (c > 0 && c != '\r' && c != '\n');

        if ((c = dc[(unsigned char)c])) {
            _myresize(prog, ps, ci + 2);      // next char, plus null terminator
#if _refInterp
            // The reference interpreter needs bracket targets and RLE
            // counts precomputed; the optimizing build derives its own
            // IR and never reads progHelp.
            _myresize(progHelp, psh, ci + 1);
            switch (c) {
            case bf_CLOSE:
                carg = ci;
                while (--carg >= 0) {
                    if (prog[carg] == bf_OPEN) {
                        if (lc == 0) break; else lc--;
                    } else if (prog[carg] == bf_CLOSE) lc++;
                }
                if (carg < 0) {
                    printf("// error - unbalanced braces\n");
                } else {
                    progHelp[carg].v = ci;
                    progHelp[ci].v   = carg;
                }
                break;

            case bf_LT:     case bf_GT:
            case bf_PLUS:   case bf_MINUS:
                if (ci && prog[ci - 1] == c) { ci--; progHelp[ci].v++; }
                else                         { progHelp[ci].v = 1; }
                break;

            default:
                progHelp[ci].v = 1;
                break;
            }
#endif
            prog[ci++] = (char)c;
        }
    }
    prog[ci++] = 0;
    proglen = ci;
    prog = (char*)realloc(prog, (size_t)ci);
    (void)psh; (void)lc;
    if (c == '!') bf_readfile(&inp, fh);
    if (fh && fh != stdin) fclose(fh);

    // run
    bf_VM_alloc(&vm);
    bf_VM_tape(&vm, bf_MAXCELLS);
    vm.prog       = prog;
    vm.progLen    = proglen;
    vm.progHelper = progHelp;
    vm.progLen_op = bf_Optimize(&vm.prog_op, vm.prog, vm.progLen, metric);
    if (vm.progLen_op < 0) {  // parse error already reported; don't run
        bf_VM_free(&vm);
        if (inp) free(inp);
        return -1;
    }
#if BF_PROFILE
    vm.prof = (unsigned long long*)calloc((size_t)vm.progLen_op + 1,
                                          sizeof(unsigned long long));
#endif
    if (printBF == 2)        bffsree_Print(&vm, inp, 0);
    else if (printBF == 1)   bffsree_Print(&vm, inp, 1);
    else {
        do {
            bffsree_Eval(&vm, inp, 10000);
        } while (vm.pc > 0);
#if BF_PROFILE
        bffsree_ProfileReport(&vm);
#endif
    }
    bf_VM_free(&vm);

    // done
    if (inp) free(inp);
    return 0;
}

#endif // BFFSREE_IMPLEMENTATION
