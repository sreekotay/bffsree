// =====================================================================
// bffsree.c
// =====================================================================
#ifdef BFFSREE_IMPLEMENTATION

#define _CRT_SECURE_NO_WARNINGS
#include "bffsree.h"

#ifndef _refInterp
#define _refInterp 0
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

static bf_cell* bf_word_scan3_forward(bf_cell* p) {
#if BF_WORD_SCAN_GUARD
    bf_cell* scalar = p;
    while (*scalar) scalar += 3;
#endif

    if (*p) {
        for (;;) {
            uint64_t cells;
            uint64_t zeros;
            memcpy(&cells, p, sizeof(cells));
            zeros = bf_zero_bytes64(cells);
#if defined(BF_WORD_BYTE0)
            if (zeros & BF_WORD_BYTE0) break;
            if (zeros & BF_WORD_BYTE3) { p += 3; break; }
            if (zeros & BF_WORD_BYTE6) { p += 6; break; }
#else
            {
                const unsigned char* z = (const unsigned char*)&zeros;
                if (z[0]) break;
                if (z[3]) { p += 3; break; }
                if (z[6]) { p += 6; break; }
            }
#endif
            p += 9;
        }
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

    if (*p) {
        for (;;) {
            bf_cell* base = p - 6;
            uint64_t cells;
            uint64_t zeros;
            memcpy(&cells, base, sizeof(cells));
            zeros = bf_zero_bytes64(cells);
#if defined(BF_WORD_BYTE0)
            if (zeros & BF_WORD_BYTE6) break;
            if (zeros & BF_WORD_BYTE3) { p -= 3; break; }
            if (zeros & BF_WORD_BYTE0) { p -= 6; break; }
#else
            {
                const unsigned char* z = (const unsigned char*)&zeros;
                if (z[6]) break;
                if (z[3]) { p -= 3; break; }
                if (z[0]) { p -= 6; break; }
            }
#endif
            p -= 9;
        }
    }

#if BF_WORD_SCAN_GUARD
    if (p != scalar) abort();
#endif
    return p;
}
#else
#define BF_WORD_SCAN3 0
#endif

#if !_refInterp
#if defined(__GNUC__)
#define BF_NOINLINE __attribute__((noinline))
#else
#define BF_NOINLINE
#endif

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

// Hottest dest-9 slides: lane +1 / +2 with boff = -(9+lane).
static BF_NOINLINE bf_cell* bf_mzscan_copy9_from1(bf_cell* p) {
    if (*p) {
        p += 1;
        for (;;) {
            bf_cell v = *p;
            *p = 0;
            p[9] += v;
            p -= 10;
            if (*p == 0) break;
            p += 1;
        }
    }
    return p;
}

static BF_NOINLINE bf_cell* bf_mzscan_copy9_from2(bf_cell* p) {
    if (*p) {
        p += 2;
        for (;;) {
            bf_cell v = *p;
            *p = 0;
            p[9] += v;
            p -= 11;
            if (*p == 0) break;
            p += 2;
        }
    }
    return p;
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
// VAL. Uses a 9-cell frame walk when the hop and working set fit.
static BF_NOINLINE bf_cell* bf_looprun_mz_mul_mz_val(
    bf_cell* p,
    bf_cell fbuf, int foff,
    int a_buf, int a_off,
    int b_buf, int b_off,
    int c_buf, int c_off,
    bf_cell add, int d_off)
{
    if (d_off == 9 && a_off == -foff && b_off == 0 && c_off == 0 &&
        a_buf == -foff && b_buf == foff &&
        foff >= 0 && foff <= 8 && c_buf >= 0 && c_buf <= 8)
        return bf_frame9_mz_mul_mz_inc(p, fbuf, foff, c_buf, add);

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

static bf_cell* bf_exec_fwd(bf_cell* p, bf_op* P) {
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

int bf_looprun_variant(const bf_op* L) {
    if (L->val == 5 &&
        L[1].cmd == bfo_VAL_MZ && L[1].val == 1 &&
        L[2].cmd == bfo_VAL_MUL && L[2].val == 1 &&
        L[3].cmd == bfo_VAL_MZ && L[3].val == 1 &&
        L[4].cmd == bfo_VAL)
        return BF_SEG_LOOPRUN_MZ_MUL_MZ_VAL;
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

// Own translation-unit-style clone of PTR_S: the stride scan must not
// share registers with computed-goto dispatch. Fib/tree spend almost
// all their time here.
static BF_NOINLINE bf_cell* bf_apply_ptr_s(bf_cell* p, int stride) {
#if BF_WORD_SCAN3
    if (stride == 3) return bf_word_scan3_forward(p);
    if (stride == -3) return bf_word_scan3_backward(p);
#endif
    while (*p) p += stride;
    return p;
}

#if !BF_PROFILE
// Common 4-op LOOPRUN: VAL, VAL_MUL, VAL_MZ, VAL_MZ.
static BF_NOINLINE bf_cell* bf_looprun_val_mul_mz_mz(
    bf_cell* p,
    bf_cell fbuf, int foff,
    bf_cell add, int a_off,
    int b_val, int b_buf, int b_off,
    int c_val, int c_buf, int c_off,
    int d_val, int d_buf, int d_off)
{
    if (*p) {
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
    }
    return p;
}
#endif

static inline bf_cell* bf_apply_rew(bf_cell* p, const bf_op* rew) {
    *p += (bf_cell)rew->buf;
    return p + rew->off;
}

#if !BF_PROFILE
#if BF_AFFINE && BF_AFFINE_APPLY
// Straight-line eval of a compiled affine tree. Snapshot the bound
// window, write dests from old[], hop. Shape is picked once per
// LOOPRUN entry (kind), not per hop and not by program name.
static inline void bf_aff_load4(const bf_cell* p, const bf_affine* m, bf_cell old[4])
{
    old[0] = old[1] = old[2] = old[3] = 0;
    if (m->nsrc > 0) old[0] = p[m->src_off[0]];
    if (m->nsrc > 1) old[1] = p[m->src_off[1]];
    if (m->nsrc > 2) old[2] = p[m->src_off[2]];
    if (m->nsrc > 3) old[3] = p[m->src_off[3]];
}

static inline bf_cell bf_aff_acc(const bf_affine* m, int s, const bf_cell old[4])
{
    bf_cell v = m->store[s].bias;
    unsigned t0 = m->store[s].t0, nt = m->store[s].nt;
    if (nt > 0) v = (bf_cell)(v + m->term[t0].k * old[m->term[t0].src]);
    if (nt > 1) v = (bf_cell)(v + m->term[t0 + 1].k * old[m->term[t0 + 1].src]);
    if (nt > 2) v = (bf_cell)(v + m->term[t0 + 2].k * old[m->term[t0 + 2].src]);
    return v;
}

static BF_NOINLINE bf_cell* bf_aff_walk_s1(
    bf_cell* p, bf_cell fbuf, int foff, const bf_affine* m)
{
    if (*p) {
        *p += fbuf;
        p += foff;
        for (;;) {
            bf_cell old[4];
            bf_aff_load4(p, m, old);
            p[m->store[0].dst] = bf_aff_acc(m, 0, old);
            p += m->hop;
            if (*p == 0) break;
            *p += fbuf;
            p += foff;
        }
    }
    return p;
}

static BF_NOINLINE bf_cell* bf_aff_walk_s2z(
    bf_cell* p, bf_cell fbuf, int foff, const bf_affine* m)
{
    int z = (m->store[0].nt == 0) ? 0 : 1;
    int a = 1 - z;
    if (*p) {
        *p += fbuf;
        p += foff;
        for (;;) {
            bf_cell old[4], v;
            unsigned t0 = m->store[a].t0, nt = m->store[a].nt;
            bf_aff_load4(p, m, old);
            p[m->store[z].dst] = m->store[z].bias;
            v = m->store[a].bias;
            v = (bf_cell)(v + m->term[t0].k * old[m->term[t0].src]);
            if (nt > 1)
                v = (bf_cell)(v + m->term[t0 + 1].k * old[m->term[t0 + 1].src]);
            p[m->store[a].dst] = v;
            p += m->hop;
            if (*p == 0) break;
            *p += fbuf;
            p += foff;
        }
    }
    return p;
}

static BF_NOINLINE bf_cell* bf_aff_walk_s2(
    bf_cell* p, bf_cell fbuf, int foff, const bf_affine* m)
{
    if (*p) {
        *p += fbuf;
        p += foff;
        for (;;) {
            bf_cell old[4], n0, n1;
            bf_aff_load4(p, m, old);
            n0 = bf_aff_acc(m, 0, old);
            n1 = bf_aff_acc(m, 1, old);
            p[m->store[0].dst] = n0;
            p[m->store[1].dst] = n1;
            p += m->hop;
            if (*p == 0) break;
            *p += fbuf;
            p += foff;
        }
    }
    return p;
}

static BF_NOINLINE bf_cell* bf_aff_walk_s3(
    bf_cell* p, bf_cell fbuf, int foff, const bf_affine* m)
{
    if (*p) {
        *p += fbuf;
        p += foff;
        for (;;) {
            bf_cell old[4], n0, n1, n2;
            bf_aff_load4(p, m, old);
            n0 = bf_aff_acc(m, 0, old);
            n1 = bf_aff_acc(m, 1, old);
            n2 = bf_aff_acc(m, 2, old);
            p[m->store[0].dst] = n0;
            p[m->store[1].dst] = n1;
            p[m->store[2].dst] = n2;
            p += m->hop;
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
        return bf_aff_walk_s1(p, (bf_cell)L->buf, L->off, bf_affine_get(L->aux));
    case BF_SEG_LOOPRUN_AFF_S2Z:
        return bf_aff_walk_s2z(p, (bf_cell)L->buf, L->off, bf_affine_get(L->aux));
    case BF_SEG_LOOPRUN_AFF_S2:
        return bf_aff_walk_s2(p, (bf_cell)L->buf, L->off, bf_affine_get(L->aux));
    case BF_SEG_LOOPRUN_AFF_S3:
        return bf_aff_walk_s3(p, (bf_cell)L->buf, L->off, bf_affine_get(L->aux));
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

static inline bf_cell* bf_run_mzscan(bf_cell* p, bf_op* M) {
    if (*p == 0) return bf_apply_rew(p, M + 2);
#if !BF_PROFILE
    if (M->buf == 0 && M[1].val == 1) {
        if (M[1].buf == 9) {
            if (M->off == 1 && M[1].off == -10)
                p = bf_mzscan_copy9_from1(p);
            else if (M->off == 2 && M[1].off == -11)
                p = bf_mzscan_copy9_from2(p);
            else
                p = bf_mzscan_slide9(p, M->off, M[1].off);
        } else
            p = bf_mzscan_copy(p, M->off, M[1].buf, M[1].off);
        return bf_apply_rew(p, M + 2);
    }
#endif
    if (*p) {
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
    }
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
    bf_cell old[4];
    bf_aff_load4(p, m, old);
    switch (m->kind) {
    case BF_AFF_S1:
        p[m->store[0].dst] = bf_aff_acc(m, 0, old);
        break;
    case BF_AFF_S2Z: {
        int z = (m->store[0].nt == 0) ? 0 : 1;
        int a = 1 - z;
        unsigned t0 = m->store[a].t0, nt = m->store[a].nt;
        bf_cell v = m->store[a].bias;
        v = (bf_cell)(v + m->term[t0].k * old[m->term[t0].src]);
        if (nt > 1)
            v = (bf_cell)(v + m->term[t0 + 1].k * old[m->term[t0 + 1].src]);
        p[m->store[z].dst] = m->store[z].bias;
        p[m->store[a].dst] = v;
        break;
    }
    case BF_AFF_S2: {
        bf_cell n0 = bf_aff_acc(m, 0, old);
        bf_cell n1 = bf_aff_acc(m, 1, old);
        p[m->store[0].dst] = n0;
        p[m->store[1].dst] = n1;
        break;
    }
    case BF_AFF_S3: {
        bf_cell n0 = bf_aff_acc(m, 0, old);
        bf_cell n1 = bf_aff_acc(m, 1, old);
        bf_cell n2 = bf_aff_acc(m, 2, old);
        p[m->store[0].dst] = n0;
        p[m->store[1].dst] = n1;
        p[m->store[2].dst] = n2;
        break;
    }
    default:
        break;
    }
    return p + m->hop;
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
// the body needs is in locals; the only call is the (already
// specialized) LOOPRUN helper, and only when its cell is nonzero.
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

    *p += fbuf;
    p += foff;
    for (;;) {
        *p = zval;
        p += zoff;
        *p += v1;
        p += v1off;
        if (*p) p = bf_looprun_taken(p, R, rsub);
        *p += rbuf;
        p += roff;
        p[mbuf] += (bf_cell)(mval * *p);
        *p = 0;
        p += moff;
        *p += v2;
        p += v2off;
        if (*p == 0) break;
        *p += fbuf;
        p += foff;
    }
    return p;
}

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
                if (*p) p = bf_apply_ptr_s(p, s1);
                p += s1off;
                if (*p == 0) break;
                *p += Fbuf;
                p += Foff;
            }
        }
        *p += FRbuf;
        p += FRoff;
        if (*p) p = bf_apply_ptr_s(p, s2);
        p += s2off;
        p = bf_run_mzscan(p, Z);
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

// *p != 0 on entry. Stops at the failed loop test (REW not applied).
static BF_NOINLINE bf_cell* bf_nest_run(bf_cell* p, bf_op* L) {
    const bf_nest* n = bf_nest_get(L->aux);
    if (!n) return bf_exec_fwd(p, L);
    switch (n->tmpl) {
    case BF_TMPL_ZV_R_MV:       return bf_nest_t_zv_r_mv(p, L);
    case BF_TMPL_VM_VRS_S_m_MV: return bf_nest_t_vm_vrs_s_m_mv(p, L);
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
static bf_cell* bf_exec_ops(bf_cell* p, bf_op* b, bf_op* end) {
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
int bffsree_Eval(bf_VM* vm, char* inp, int ocount) {
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
            if ((P)->buf == 0 && (P)[1].val == 1) { \
                tp = ptr + sp; \
                if ((P)[1].buf == 9) { \
                    if ((P)->off == 1 && (P)[1].off == -10) \
                        tp = bf_mzscan_copy9_from1(tp); \
                    else if ((P)->off == 2 && (P)[1].off == -11) \
                        tp = bf_mzscan_copy9_from2(tp); \
                    else \
                        tp = bf_mzscan_slide9(tp, (P)->off, (P)[1].off); \
                } else \
                    tp = bf_mzscan_copy(tp, (P)->off, (P)[1].buf, (P)[1].off); \
                sp = (int)(tp - ptr); \
                _bf_bound_sp(); \
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
