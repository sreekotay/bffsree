// =====================================================================
// bffsree-opt.c
// =====================================================================
#ifdef BFFSREE_OPT_IMPLEMENTATION

#include "bffsree.h"

// optimization macros
#define _bfe_(e,c)            do { (e).cmd=(uint8_t)(c); (e).sub=0; (e).aux=0; } while(0)
#define _bfe_v(e,c,v)         do { (e).cmd=(uint8_t)(c); (e).sub=0; (e).aux=0; (e).val=(int32_t)(v); } while(0)
#define _bfe_vo(e,c,v,o)      do { (e).cmd=(uint8_t)(c); (e).sub=0; (e).aux=0; (e).val=(int32_t)(v); (e).off=(bf_off_t)(o); (e).buf=0; } while(0)
#define _bfe_vob(e,c,v,o,b)   do { (e).cmd=(uint8_t)(c); (e).sub=0; (e).aux=0; (e).val=(int32_t)(v); (e).off=(bf_off_t)(o); (e).buf=(bf_op_buf_t)(b); } while(0)

static int progscan(int* ptroff, char* chars, int pc, int proglen, int plusTok, int minusTok) {
    int c, ci = 0;
    while (pc + 1 < proglen && _myabs(ci) < 126) {
        c = (unsigned char)chars[pc + 1];
        if (c == plusTok) ci++;
        else if (c == minusTok) ci--;
        else break;
        pc++;
    }
    *ptroff = ci;
    return pc;
}

#define ptrcounter(p,c,pc,pl) progscan((p),(c),(pc),(pl),bf_GT,bf_LT)
#define valcounter(p,c,pc,pl) progscan((p),(c),(pc),(pl),bf_PLUS,bf_MINUS)

// =====================================================================
// brainfuck - loop optimization (original version)
// =====================================================================
static int optimizeLoop(bf_op* bfo, int s) {
    int pc = s;
    int canopt = 1;
    int lc = 0;
    int skip, rc, sp, nsp, haszero = 0;
    enum ebfo_CMD cmd;
    bf_op *opptr, opstack[128];
    char vtrackspace[512] = {0};
    char *vtrack = vtrackspace + 256;

    // ============================
    // Can we optimize this loop?
    // ============================
    if (bfo[pc].cmd != bfo_FWD) {
        return -1;
    }
    lc = bfo[pc].buf;
    sp = bfo[pc].off;
    pc++;

    // read = 1, modify = 2, set = 4
    #define _loop_var(a) (((a) & 7) > 2)  // once modified, we can't read safely
    #define _vt_bad(i)   ((i) < -255 || (i) > 255)  // outside vtrack window
    while (canopt && (cmd = (enum ebfo_CMD)bfo[pc].cmd) != bfo_REW) {
        if (_vt_bad(sp)) { canopt = 0; break; }
        switch (cmd) {
        case bfo_VAL:
            if (sp == 0) lc += bfo[pc].val;
            else vtrack[sp] |= 2 | 1;
            break;

        case bfo_VAL_MUL:
            if (_vt_bad(sp + bfo[pc].buf)) { canopt = 0; break; }
            if (_loop_var(vtrack[sp])) canopt = 0;
            if (_loop_var(vtrack[sp + bfo[pc].buf])) canopt = 0;
            vtrack[sp + bfo[pc].buf] |= 2 | 1;
            vtrack[sp] |= 1;
            if (sp == 0) canopt = 0;
            break;

        case bfo_VAL_MZ:
            if (_vt_bad(sp + bfo[pc].buf)) { canopt = 0; break; }
            if (_loop_var(vtrack[sp + bfo[pc].buf])) canopt = 0;
            // fall through

        case bfo_VAL_ZERO:
            vtrack[sp] |= 4;
            if (cmd == bfo_VAL_MZ) {
                vtrack[sp] |= 1;
                vtrack[sp + bfo[pc].buf] |= 2 | 1;
            }
            if (sp == 0 || sp + bfo[pc].buf == 0) canopt = 0;
            if (sp == 0) lc = 0;
            haszero = 1;
            break;

        case bfo_NOOP:
        case bfo_PTR_S:
        case bfo_FWD:
        case bfo_REW:
        case bfo_GET:
        case bfo_PUT:
        default:
            canopt = 0;
            break;
        }

        sp += bfo[pc].off;
        pc++;
    }

    // has to be a simple loop -- balanced and dec by 1
    if (canopt == 0 || sp != 0 || lc != -1 || (vtrack[0] & 2) == 2)
        return -1;

    // ============================
    // Optimize the loop
    // ============================
    rc = (int)(sizeof(*opptr) * (size_t)(pc - s + 1));
    opptr = (rc <= (int)sizeof(opstack)) ? opstack : (bf_op*)malloc((size_t)rc);
    memcpy(opptr, bfo + s, (size_t)rc);

    // first - open
    pc = s;
    rc = 0;
    bfo[pc] = opptr[rc++];
    lc = bfo[pc].buf;
    sp = bfo[pc].off;
    if (haszero) {
        bfo[pc].buf = 0;
        bfo[pc].off = 0;
        pc++;
    }

    // already strengthened = 8, zeroed = 16
    while ((cmd = (enum ebfo_CMD)opptr[rc].cmd) != bfo_REW) {
        skip = 0;
        bfo[pc] = opptr[rc++];
        switch (cmd) {
        default:
        case bfo_NOOP:
            break;

        case bfo_VAL:
            if (sp == 0) { skip = 1; break; }
            nsp = sp;
            sp += bfo[pc].off;
            _bfe_vob(bfo[pc], bfo_VAL_MUL, bfo[pc].val, 0, nsp);
            vtrack[nsp] |= 8;
            break;

        case bfo_VAL_MUL:
        case bfo_VAL_MZ:
            nsp = sp;
            sp += bfo[pc].off + nsp;
            bfo[pc + 1] = bfo[pc];
            if (vtrack[nsp] & 8) {
                _bfe_vob(bfo[pc], bfo_NOOP, 0, nsp, 0);
                vtrack[nsp] |= 16;
            } else {
                _bfe_vob(bfo[pc], bfo_MUL_MUL, 1, nsp, nsp);
                vtrack[nsp] |= 16 | 8;
            }
            pc++;
            bfo[pc].off = (bf_off_t)(-nsp);
            break;

        case bfo_VAL_ZERO:
            if (sp == 0) { skip = 1; break; }
            nsp = sp;
            sp += bfo[pc].off + nsp;
            bfo[pc + 1] = bfo[pc];
            _bfe_vob(bfo[pc], bfo_NOOP, 0, nsp, 0);
            vtrack[nsp] |= 16;
            pc++;
            bfo[pc].off = (bf_off_t)(-nsp);
            break;
        }

        sp += bfo[pc].off;
        if (skip == 0)
            pc++;
    }

    // add zero (append to existing if possible)
    if (pc != sp &&
        bfo[pc - 1].cmd == bfo_VAL_MUL &&
        bfo[pc - 1].off == 0)
        bfo[pc - 1].cmd = bfo_VAL_MZ;
    else {
        _bfe_vob(bfo[pc], bfo_VAL_ZERO, 0, 0, 0);
        pc++;
    }

    // last one
    bfo[pc] = opptr[rc++];
    lc = bfo[pc].buf;
    sp = bfo[pc].off;
    if (haszero) {
        bfo[s].val = pc - s;
        bfo[pc].val = s - pc;
        pc++;
    } else if (sp || lc) {
        if (lc || pc == s) {
            if (lc && pc && bfo[pc - 1].cmd == bfo_VAL_ZERO && bfo[pc - 1].off == 0) {
                pc--;
                _bfe_vob(bfo[pc], bfo_VAL_ZERO, lc, sp, 0);
            } else
                _bfe_vob(bfo[pc], lc == 0 ? bfo_NOOP : bfo_VAL, lc, sp, 0);
            pc++;
        } else
            bfo[pc - 1].off = (bf_off_t)(bfo[pc - 1].off + sp);
    }

    // done
    if (opptr != opstack) free(opptr);
    return pc;
    #undef _loop_var
    #undef _vt_bad
}

// ----------------------------
// Peephole: fold NOOP pointer moves into the preceding op.
// Safe because jumps only target FWD/REW ops (never NOOPs) and an
// op's off applies only on its fall-through path. Jump distances
// (val) are renumbered for the deleted slots. Returns the new length.
// ----------------------------
// Remove del-marked slots and renumber FWD/REW jump distances. Jumps
// only ever target FWD/REW ops, which no pass marks for deletion.
static int bf_compact(bf_op* bfo, int pc, const char* del) {
    int* nidx = (int*)malloc(sizeof(int) * (size_t)(pc + 1));
    int i, w;
    if (!nidx) return pc;
    for (i = 0, w = 0; i < pc; i++) {
        nidx[i] = w;
        if (!del[i]) w++;
    }
    nidx[pc] = w;
    for (i = 0; i < pc; i++)
        if (bfo[i].cmd == bfo_FWD || bfo[i].cmd == bfo_REW)
            bfo[i].val = nidx[i + bfo[i].val] - nidx[i];
    for (i = 0, w = 0; i < pc; i++)
        if (!del[i]) bfo[w++] = bfo[i];
    free(nidx);
    return w;
}

static int bf_foldNoops(bf_op* bfo, int pc) {
    char* del = (char*)malloc((size_t)(pc + 1));
    int i, last = -1;

    if (del) {
        // Fold each deletable NOOP's off into the previous surviving
        // op; the fold target's accumulated off must fit bf_off_t.
        for (i = 0; i < pc; i++) {
            if (bfo[i].cmd == bfo_NOOP && last >= 0 &&
                bfo[last].off + bfo[i].off >= -32768 &&
                bfo[last].off + bfo[i].off <= 32767) {
                del[i] = 1;
                bfo[last].off = (bf_off_t)(bfo[last].off + bfo[i].off);
                continue;
            }
            del[i] = 0;
            last = i;
        }
        pc = bf_compact(bfo, pc, del);
    }
    _myfree(del);
    return pc;
}

// ----------------------------
// Peephole: a run of k>=2 VAL_ZERO ops storing the same value to
// adjacent cells (each stepping +1 or -1 onto the next) becomes one
// ZFILL: val = fill value, buf = +k (cells p[0..k-1]) or -k (cells
// p[-(k-1)..0]), off = the run's total pointer delta. Block clears
// like [-]>[-]>[-] are common; this makes them one dispatch.
// ----------------------------
static int bf_fuseZeroFills(bf_op* bfo, int pc) {
    char* del = (char*)calloc((size_t)pc + 1, 1);
    int i, j, k, dir;

    if (!del) return pc;
    for (i = 0; i < pc; ) {
        if (bfo[i].cmd != bfo_VAL_ZERO) { i++; continue; }
        dir = bfo[i].off;
        if (dir != 1 && dir != -1) { i++; continue; }
        j = i;
        while (j + 1 < pc && bfo[j].off == dir &&
               bfo[j + 1].cmd == bfo_VAL_ZERO && bfo[j + 1].val == bfo[i].val)
            j++;
        k = j - i + 1;
        if (k >= 2 && k <= 127) {
            int total = (k - 1) * dir + bfo[j].off;
            if (total >= -32768 && total <= 32767) {
                bfo[i].cmd = bfo_ZFILL;
                bfo[i].buf = (bf_op_buf_t)(dir * k);
                bfo[i].off = (bf_off_t)total;
                memset(del + i + 1, 1, (size_t)(k - 1));
            }
        }
        i = j + 1;
    }
    pc = bf_compact(bfo, pc, del);
    free(del);
    return pc;
}

// ----------------------------
// Affine reconstruction of a walking LOOPRUN body.
// Each arithmetic op is affine over the cell ring, so the composed
// body is new[i] = bias[i] + Σ_j c[i][j]*old[j]. MUL_MUL of two
// live cells is quadratic and is rejected. This is the walking
// case: each iteration lands on a different cell, so we cannot
// fold the trip count the way optimizeLoop does for [-...] with
// lc==-1. The map is applied once per hop.
// ----------------------------
#if BF_AFFINE
#define BF_AFF_SPAN 64
#define BF_AFF_MID  32

typedef struct {
    bf_cell c[BF_AFF_SPAN];
    bf_cell b;
    unsigned char used;
    unsigned char written;
} bf_aff_expr;

static bf_affine *bf_affine_pool;
static int        bf_affine_npool;
static int        bf_affine_cap;

const bf_affine *bf_affine_get(unsigned id) {
    if (id == 0 || (int)id > bf_affine_npool) return 0;
    return &bf_affine_pool[id - 1];
}

int bf_affine_count(void) {
    return bf_affine_npool;
}

static void bf_affine_reset(void) {
    free(bf_affine_pool);
    bf_affine_pool = 0;
    bf_affine_npool = 0;
    bf_affine_cap = 0;
}

#if BF_AFFINE_APPLY
static int bf_affine_intern(const bf_affine *m) {
    if (bf_affine_npool >= 0xFFFF) return 0;
    if (bf_affine_npool >= bf_affine_cap) {
        int ncap = bf_affine_cap ? bf_affine_cap * 2 : 64;
        bf_affine *n = (bf_affine *)realloc(bf_affine_pool, (size_t)ncap * sizeof(bf_affine));
        if (!n) return 0;
        bf_affine_pool = n;
        bf_affine_cap = ncap;
    }
    bf_affine_pool[bf_affine_npool] = *m;
    bf_affine_npool++;
    return bf_affine_npool;
}
#endif

static int bf_aff_idx(int off) {
    return off + BF_AFF_MID;
}

static int bf_aff_inrange(int off) {
    int i = bf_aff_idx(off);
    return i >= 0 && i < BF_AFF_SPAN;
}

static void bf_aff_identity(bf_aff_expr *e, int off) {
    memset(e, 0, sizeof *e);
    e->c[bf_aff_idx(off)] = (bf_cell)1;
    e->used = 1;
}

static int bf_aff_ensure(bf_aff_expr *cells, int off) {
    int i;
    if (!bf_aff_inrange(off)) return 0;
    i = bf_aff_idx(off);
    if (!cells[i].used) {
        bf_aff_identity(&cells[i], off);
        cells[i].used = 1;
    }
    return 1;
}

static int bf_aff_is_const(const bf_aff_expr *e) {
    int j;
    for (j = 0; j < BF_AFF_SPAN; j++)
        if (e->c[j] != 0) return 0;
    return 1;
}

static int bf_aff_is_identity(const bf_aff_expr *e, int off) {
    int j, self;
    if (!bf_aff_inrange(off) || e->b != 0) return 0;
    self = bf_aff_idx(off);
    for (j = 0; j < BF_AFF_SPAN; j++) {
        bf_cell want = (j == self) ? (bf_cell)1 : 0;
        if (e->c[j] != want) return 0;
    }
    return 1;
}

static void bf_aff_add_scaled(bf_aff_expr *dst, const bf_aff_expr *src, bf_cell k) {
    int j;
    if (k == 0) return;
    for (j = 0; j < BF_AFF_SPAN; j++)
        dst->c[j] = (bf_cell)(dst->c[j] + k * src->c[j]);
    dst->b = (bf_cell)(dst->b + k * src->b);
}

static void bf_aff_scale(bf_aff_expr *dst, bf_cell k) {
    int j;
    for (j = 0; j < BF_AFF_SPAN; j++)
        dst->c[j] = (bf_cell)(k * dst->c[j]);
    dst->b = (bf_cell)(k * dst->b);
}

int bf_affine_from_body(const bf_op *body, int n, bf_affine *out) {
    bf_aff_expr cells[BF_AFF_SPAN];
    int cur = 0, i, j, off, src_i, dst_i, sid;
    int src_map[BF_AFF_SPAN];

    memset(cells, 0, sizeof cells);
    memset(out, 0, sizeof *out);
    memset(src_map, 0xff, sizeof src_map);

    for (i = 0; i < n; i++) {
        const bf_op *op = &body[i];
        bf_cell k = (bf_cell)op->val;
        int dest;

        if (!bf_aff_ensure(cells, cur)) return 0;

        switch (op->cmd) {
        case bfo_VAL:
            cells[bf_aff_idx(cur)].b = (bf_cell)(cells[bf_aff_idx(cur)].b + k);
            cells[bf_aff_idx(cur)].written = 1;
            break;

        case bfo_VAL_ZERO:
            memset(&cells[bf_aff_idx(cur)], 0, sizeof cells[0]);
            cells[bf_aff_idx(cur)].b = k;
            cells[bf_aff_idx(cur)].used = 1;
            cells[bf_aff_idx(cur)].written = 1;
            break;

        case bfo_ZFILL: {
            int cnt = op->buf < 0 ? -op->buf : op->buf;
            int dir = op->buf < 0 ? -1 : 1, t;
            for (t = 0; t < cnt; t++) {
                int at = cur + t * dir;
                if (!bf_aff_inrange(at)) return 0;
                memset(&cells[bf_aff_idx(at)], 0, sizeof cells[0]);
                cells[bf_aff_idx(at)].b = k;
                cells[bf_aff_idx(at)].used = 1;
                cells[bf_aff_idx(at)].written = 1;
            }
            break;
        }

        case bfo_VAL_MUL:
        case bfo_VAL_MZ:
            dest = cur + (int)op->buf;
            if (!bf_aff_ensure(cells, dest)) return 0;
            bf_aff_add_scaled(&cells[bf_aff_idx(dest)],
                              &cells[bf_aff_idx(cur)], k);
            cells[bf_aff_idx(dest)].written = 1;
            if (op->cmd == bfo_VAL_MZ) {
                memset(&cells[bf_aff_idx(cur)], 0, sizeof cells[0]);
                cells[bf_aff_idx(cur)].used = 1;
                cells[bf_aff_idx(cur)].written = 1;
            }
            break;

        case bfo_MUL_MUL:
            dest = cur + (int)op->buf;
            if (!bf_aff_ensure(cells, dest)) return 0;
            // dst *= val * src. Affine only when src is a constant.
            if (!bf_aff_is_const(&cells[bf_aff_idx(cur)])) return 0;
            bf_aff_scale(&cells[bf_aff_idx(dest)],
                         (bf_cell)(k * cells[bf_aff_idx(cur)].b));
            cells[bf_aff_idx(dest)].written = 1;
            break;

        case bfo_NOOP:
            break;

        default:
            return 0;
        }
        cur += (int)op->off;
    }

    if (cur < -32768 || cur > 32767) return 0;
    out->hop = (int16_t)cur;

    for (off = -BF_AFF_MID; off < BF_AFF_SPAN - BF_AFF_MID; off++) {
        bf_aff_expr *e;
        if (!bf_aff_inrange(off)) continue;
        e = &cells[bf_aff_idx(off)];
        if (!e->used || !e->written) continue;
        if (bf_aff_is_identity(e, off)) continue;
        if (out->nstore >= BF_AFFINE_MAX_STORE) return 0;

        dst_i = out->nstore;
        out->store[dst_i].dst = (int16_t)off;
        out->store[dst_i].bias = e->b;
        out->store[dst_i].t0 = out->nterm;
        out->store[dst_i].nt = 0;

        for (j = 0; j < BF_AFF_SPAN; j++) {
            if (e->c[j] == 0) continue;
            src_i = j - BF_AFF_MID;
            sid = src_map[j];
            if (sid < 0) {
                if (out->nsrc >= BF_AFFINE_MAX_SRC) return 0;
                sid = out->nsrc;
                src_map[j] = sid;
                out->src_off[sid] = (int16_t)src_i;
                out->nsrc++;
            }
            if (out->nterm >= BF_AFFINE_MAX_TERM) return 0;
            out->term[out->nterm].src = (uint8_t)sid;
            out->term[out->nterm].k = e->c[j];
            out->nterm++;
            out->store[dst_i].nt++;
        }
        out->nstore++;
    }
    bf_affine_classify(out);
    return 1;
}

int bf_affine_classify(bf_affine *m) {
    int i, maxnt = 0, nzero = 0, nsmall = 0;

    m->kind = BF_AFF_NONE;
    if (!m || m->nsrc > 4) return BF_AFF_NONE;
    for (i = 0; i < m->nstore; i++) {
        int nt = m->store[i].nt;
        if (nt > 3) return BF_AFF_NONE;
        if (nt > maxnt) maxnt = nt;
        if (nt == 0) nzero++;
        else if (nt <= 2) nsmall++;
    }
    if (m->nstore == 1)
        m->kind = BF_AFF_S1;
    else if (m->nstore == 2 && nzero == 1 && nsmall == 1)
        m->kind = BF_AFF_S2Z;
    else if (m->nstore == 2)
        m->kind = BF_AFF_S2;
    else if (m->nstore == 3)
        m->kind = BF_AFF_S3;
    return (int)m->kind;
}

const char *bf_affine_kind_name(int kind) {
    switch (kind) {
    case BF_AFF_S1:  return "s1";
    case BF_AFF_S2Z: return "s2z";
    case BF_AFF_S2:  return "s2";
    case BF_AFF_S3:  return "s3";
    default:         return 0;
    }
}

int bf_affine_format(const bf_affine *m, char *buf, int buflen) {
    int i, j, n = 0;
    if (!m || !buf || buflen <= 0) return 0;
    buf[0] = 0;
    n += snprintf(buf + n, (size_t)(buflen - n), "hop=%d", (int)m->hop);
    for (i = 0; i < m->nstore && n < buflen - 1; i++) {
        const char *plus = "";
        n += snprintf(buf + n, (size_t)(buflen - n), "; p[%d]=",
                      (int)m->store[i].dst);
        if (m->store[i].bias || m->store[i].nt == 0) {
            n += snprintf(buf + n, (size_t)(buflen - n), "%d",
                          (int)m->store[i].bias);
            plus = "+";
        }
        for (j = 0; j < m->store[i].nt && n < buflen - 1; j++) {
            int k = (int)m->term[m->store[i].t0 + j].k;
            int src = (int)m->src_off[m->term[m->store[i].t0 + j].src];
            if (k == 1)
                n += snprintf(buf + n, (size_t)(buflen - n), "%sp[%d]", plus, src);
            else if (k == -1)
                n += snprintf(buf + n, (size_t)(buflen - n), "-p[%d]", src);
            else
                n += snprintf(buf + n, (size_t)(buflen - n), "%s%d*p[%d]",
                              plus, k, src);
            plus = "+";
        }
    }
    return n;
}
#endif // BF_AFFINE

// ----------------------------
// Convert remaining walking loops whose bodies are straight-line
// arithmetic into LOOPRUN superinstructions. Must run after
// bf_foldNoops so FWD jump distances are final. Body and REW ops stay
// in place as the parameter block.
// Nested scans/walks stay as separate ops: a generic nest interpreter
// was slower than dispatching the specialized helpers from Eval.
// ----------------------------
static void bf_markLoopRuns(bf_op* bfo, int pc) {
    int i, j, n, c, okb;
#if BF_AFFINE && BF_AFFINE_APPLY
    bf_affine map;
#endif

    for (i = 0; i < pc; i++) {
        if (bfo[i].cmd != bfo_FWD) continue;
        n = bfo[i].val;
        if (n < 2 || i + n >= pc || bfo[i + n].cmd != bfo_REW) continue;
        okb = 1;
        for (j = i + 1; j < i + n; j++) {
            c = bfo[j].cmd;
            if (c != bfo_VAL && c != bfo_VAL_MZ && c != bfo_VAL_MUL &&
                c != bfo_VAL_ZERO && c != bfo_MUL_MUL && c != bfo_ZFILL) { okb = 0; break; }
        }
        if (!okb) continue;
        bfo[i].cmd = bfo_LOOPRUN;
#if BF_AFFINE && BF_AFFINE_APPLY
        if (bf_affine_from_body(bfo + i + 1, n - 1, &map) && map.kind) {
            int id = bf_affine_intern(&map);
            if (id > 0) bfo[i].aux = (uint16_t)id;
        }
#endif
        bfo[i].sub = (uint8_t)bf_looprun_variant(bfo + i);
    }
}

// ----------------------------
// Nest compilation. A FWD/REW loop with no I/O in its range becomes a
// NEST: the body is cut into segments at every non-arithmetic op.
// Arithmetic runs bind to an affine shape evaluator when they have
// one, else stay an op range for the switch walker. Scans and block
// ops bind to their existing helpers. Nested loops were compiled
// first (we walk headers from the inside out), so they appear here
// as NEST blocks; anything that failed to compile stays a generic
// FWD segment. The FWD/body/REW ops stay in place as the parameter
// block, exactly like LOOPRUN.
// ----------------------------
static bf_nest *bf_nest_pool;
static int      bf_nest_npool;
static int      bf_nest_cap;

const bf_nest *bf_nest_get(unsigned id) {
    if (id == 0 || (int)id > bf_nest_npool) return 0;
    return &bf_nest_pool[id - 1];
}

int bf_nest_count(void) {
    return bf_nest_npool;
}

static void bf_nest_reset(void) {
    free(bf_nest_pool);
    bf_nest_pool = 0;
    bf_nest_npool = 0;
    bf_nest_cap = 0;
}

// Structural signature of a loop body (see BF_TMPL_* in the header).
// Returns the length, or -1 if it does not fit or the body contains
// an op with no letter.
int bf_nest_signature(const bf_op *bfo, int s, char *buf, int buflen) {
    int e = s + bfo[s].val, j = s + 1, n = 0;
    while (j < e) {
        int c = bfo[j].cmd;
        char ch;
        switch (c) {
        case bfo_VAL:      ch = 'V'; break;
        case bfo_VAL_MZ:   ch = 'M'; break;
        case bfo_VAL_MUL:  ch = 'X'; break;
        case bfo_VAL_ZERO: ch = 'Z'; break;
        case bfo_ZFILL:    ch = 'F'; break;
        case bfo_MUL_MUL:  ch = 'Q'; break;
        case bfo_NOOP:     ch = 'N'; break;
        case bfo_PTR_S:    ch = 'S'; break;
        case bfo_LOOPRUN:  ch = 'R'; break;
        case bfo_MZSCAN:   ch = 'm'; break;
        case bfo_VALSCAN:  ch = 'v'; break;
        case bfo_FWD:
        case bfo_NEST:     ch = '{'; break;
        default:           return -1;
        }
        if (n + 2 >= buflen) return -1;
        buf[n++] = ch;
        if (ch == '{') {
            int k = bf_nest_signature(bfo, j, buf + n, buflen - n);
            if (k < 0 || n + k + 2 >= buflen) return -1;
            n += k;
            buf[n++] = '}';
            j += bfo[j].val + 1;
        } else if (ch == 'R') {
            j += bfo[j].val + 1;
        } else if (ch == 'm' || ch == 'v') {
            j += 3;
        } else {
            j++;
        }
    }
    buf[n] = 0;
    return n;
}

#if BF_NEST
static int bf_nest_intern(const bf_nest *n) {
    if (bf_nest_npool >= 0xFFFF) return 0;
    if (bf_nest_npool >= bf_nest_cap) {
        int ncap = bf_nest_cap ? bf_nest_cap * 2 : 64;
        bf_nest *p = (bf_nest *)realloc(bf_nest_pool, (size_t)ncap * sizeof(bf_nest));
        if (!p) return 0;
        bf_nest_pool = p;
        bf_nest_cap = ncap;
    }
    bf_nest_pool[bf_nest_npool] = *n;
    bf_nest_npool++;
    return bf_nest_npool;
}

static int bf_isArith(int c) {
    return c == bfo_VAL || c == bfo_VAL_MZ || c == bfo_VAL_MUL ||
           c == bfo_VAL_ZERO || c == bfo_MUL_MUL || c == bfo_NOOP ||
           c == bfo_ZFILL;
}

static int bf_nest_compile(bf_op *bfo, int s, bf_nest *n) {
    int e = s + bfo[s].val, j = s + 1;

    memset(n, 0, sizeof *n);
    while (j < e) {
        int c = bfo[j].cmd;
        bf_seg *g;
        if (n->nseg >= BF_NEST_MAX_SEG) return 0;
        g = &n->seg[n->nseg];
        if (bf_isArith(c)) {
            int k = j;
            while (k < e && bf_isArith(bfo[k].cmd)) k++;
            g->kind = BF_SEG_OPS;
            g->a = j - s;
            g->b = k - s;
#if BF_AFFINE && BF_AFFINE_APPLY && BF_NEST_AFF
            {
                bf_affine map;
                if (bf_affine_from_body(bfo + j, k - j, &map) && map.kind) {
                    int id = bf_affine_intern(&map);
                    if (id > 0) { g->kind = BF_SEG_AFF; g->a = id; }
                }
            }
#endif
            j = k;
        } else if (c == bfo_PTR_S) {
            g->kind = BF_SEG_PTRS;
            g->a = bfo[j].val;
            g->off = bfo[j].off;
            j++;
        } else if (c == bfo_MZSCAN || c == bfo_VALSCAN) {
            g->kind = (c == bfo_MZSCAN) ? BF_SEG_MZSCAN : BF_SEG_VALSCAN;
            g->a = j - s;
            j += 3;
        } else if (c == bfo_LOOPRUN || c == bfo_NEST || c == bfo_FWD) {
            g->kind = (c == bfo_LOOPRUN) ? (uint8_t)bfo[j].sub
                    : (c == bfo_NEST)    ? BF_SEG_NEST : BF_SEG_FWD;
            g->a = j - s;
            j += bfo[j].val + 1;
        } else {
            return 0;
        }
        n->nseg++;
    }
    return j == e;
}

static const struct { const char *sig; int tmpl; } bf_templates[] = {
    { "ZVRMV",       BF_TMPL_ZV_R_MV },
    { "VM{VRS}SmMV", BF_TMPL_VM_VRS_S_m_MV },
};

static int bf_nest_template(const bf_op *bfo, int s) {
    char sig[64];
    unsigned k;
    if (bf_nest_signature(bfo, s, sig, (int)sizeof sig) < 0) return BF_TMPL_NONE;
    for (k = 0; k < sizeof bf_templates / sizeof bf_templates[0]; k++)
        if (strcmp(sig, bf_templates[k].sig) == 0) return bf_templates[k].tmpl;
    return BF_TMPL_NONE;
}

static void bf_markNests(bf_op *bfo, int pc) {
    int i, j, e, c;
    bf_nest n;

    for (i = pc - 1; i >= 0; i--) {
        if (bfo[i].cmd != bfo_FWD) continue;
        e = i + bfo[i].val;
        if (e >= pc || bfo[e].cmd != bfo_REW) continue;
        for (j = i + 1; j < e; j++) {
            c = bfo[j].cmd;
            if (c == bfo_PUT || c == bfo_GET || c == bfo_EOP) break;
        }
        if (j < e) continue;
        if (!bf_nest_compile(bfo, i, &n)) continue;
        n.tmpl = (uint8_t)bf_nest_template(bfo, i);
        if (!n.tmpl && !BF_NEST_GENERIC) continue;
        {
            int id = bf_nest_intern(&n);
            if (id <= 0) continue;
            bfo[i].cmd = bfo_NEST;
            bfo[i].aux = (uint16_t)id;
        }
    }

    // Bind affine pointers now that both pools have stopped growing.
    for (i = 0; i < bf_nest_npool; i++) {
        bf_nest *m = &bf_nest_pool[i];
        for (j = 0; j < m->nseg; j++)
            if (m->seg[j].kind == BF_SEG_AFF)
#if BF_AFFINE
                m->seg[j].aff = bf_affine_get((unsigned)m->seg[j].a);
#else
                m->seg[j].aff = 0;
#endif
    }
}
#endif // BF_NEST

// ----------------------------
// Program optimization
// ----------------------------
int bf_Optimize(void** bfoptr, char* chars, int proglen, int printMetrics) {
#if BF_AFFINE
    bf_affine_reset();
#endif
    bf_nest_reset();
    bf_op* bfo = (bf_op*)calloc((size_t)(proglen + 1), sizeof(bf_op));
    int lstack[bf_MEMDEFAULT];

    int pc = 0, rpc = 0;
    int cci = 0, c;
    int loop = 0, l;
    int off = 0, t1 = 0, tc;

    if (!bfo) return -1;
    if (bfoptr) *bfoptr = 0;

    while (rpc < proglen) {
        switch (c = (unsigned char)chars[rpc]) {
        case bf_OPEN:
            tc = ptrcounter(&t1, chars, rpc, proglen);
            if (tc != rpc && (unsigned char)chars[tc + 1] == bf_CLOSE) {
                rpc = tc + 1;
                rpc = ptrcounter(&off, chars, rpc, proglen);
                _bfe_vo(bfo[pc], bfo_PTR_S, t1, off);
                pc++;
                break;
            }

            if (loop >= bf_MEMDEFAULT) goto OPT_ERROR;  // nesting too deep
            lstack[loop++] = pc;

            rpc = valcounter(&cci, chars, rpc, proglen);
            rpc = ptrcounter(&off, chars, rpc, proglen);
            _bfe_vob(bfo[pc], bfo_FWD, pc, off, cci);
            pc++;
            break;

        case bf_CLOSE:
            if (loop <= 0) goto OPT_ERROR;

            l = lstack[--loop];

            rpc = valcounter(&cci, chars, rpc, proglen);
            rpc = ptrcounter(&off, chars, rpc, proglen);

            _bfe_v(bfo[l], bfo_FWD, pc - l);
            _bfe_vob(bfo[pc], bfo_REW, l - pc, off, cci);
            pc++;

            tc = optimizeLoop(bfo, l);
            if (tc > 0) pc = tc;
            else if (pc == l + 3) {
                // Walking loop with a one-op body (net pointer drift per
                // iteration, so not flattenable): execute it as a single
                // op with an internal loop. The FWD keeps its slot and
                // the body/REW ops become its parameter block.
                if (bfo[l + 1].cmd == bfo_VAL_MZ)   bfo[l].cmd = bfo_MZSCAN;
                else if (bfo[l + 1].cmd == bfo_VAL) bfo[l].cmd = bfo_VALSCAN;
            }
            break;

        case bf_GT:
        case bf_LT:
            rpc = ptrcounter(&off, chars, rpc, proglen);
            off += (c == bf_GT) ? 1 : -1;
            _bfe_vo(bfo[pc], bfo_NOOP, 0, off);
            pc++;
            break;

        case bf_PLUS:
        case bf_MINUS:
            rpc = valcounter(&cci, chars, rpc, proglen);
            cci += (c == bf_PLUS) ? 1 : -1;
            rpc = ptrcounter(&off, chars, rpc, proglen);
            _bfe_vo(bfo[pc], (cci == 0) ? bfo_NOOP : bfo_VAL, cci, off);
            pc++;
            break;

        case bf_PERIOD:
            rpc = ptrcounter(&off, chars, rpc, proglen);
            _bfe_vo(bfo[pc], bfo_PUT, bfo[pc].val, off);
            pc++;
            break;

        case bf_COMMA:
            rpc = ptrcounter(&off, chars, rpc, proglen);
            _bfe_vo(bfo[pc], bfo_GET, bfo[pc].val, off);
            pc++;
            break;

        default:
            break;
        }

        rpc++;
    }

    if (loop != 0) goto OPT_ERROR;  // unmatched '['

    pc = bf_foldNoops(bfo, pc);
    pc = bf_fuseZeroFills(bfo, pc);
    bf_markLoopRuns(bfo, pc);
#if BF_NEST
    bf_markNests(bfo, pc);
#endif

    if (printMetrics) {
        printf("//-- Optimization: Instructions [%d -> %d] using Bytes [%d -> %d] (op=%d bytes)\n",
               proglen, pc, proglen, (int)(pc * (int)sizeof(bf_op)), (int)sizeof(bf_op));
#if BF_AFFINE
        {
            int nrun = 0, naff = 0, napp = 0, k;
            int ns1 = 0, ns2z = 0, ns2 = 0, ns3 = 0;
            bf_affine tmp;
            for (k = 0; k < pc; k++) {
                if (bfo[k].cmd != bfo_LOOPRUN) continue;
                nrun++;
                if (bf_affine_from_body(bfo + k + 1, bfo[k].val - 1, &tmp)) {
                    naff++;
                    if (tmp.kind == BF_AFF_S1) ns1++;
                    else if (tmp.kind == BF_AFF_S2Z) ns2z++;
                    else if (tmp.kind == BF_AFF_S2) ns2++;
                    else if (tmp.kind == BF_AFF_S3) ns3++;
                }
                if (bfo[k].aux) napp++;
            }
            printf("//-- Affine: reconstructed %d/%d LOOPRUN bodies"
                   " (eval %d: s1=%d s2z=%d s2=%d s3=%d)\n",
                   naff, nrun, napp, ns1, ns2z, ns2, ns3);
        }
#endif
#if BF_NEST
        {
            int k, s, nn = 0, nrem = 0, nlr = 0, cnt[BF_SEG_Total] = {0};
            for (k = 0; k < pc; k++) {
                if (bfo[k].cmd == bfo_FWD) nrem++;
                if (bfo[k].cmd != bfo_NEST) continue;
                nn++;
                {
                    const bf_nest *m = bf_nest_get(bfo[k].aux);
                    if (!m) continue;
                    for (s = 0; s < m->nseg; s++) {
                        int kd = m->seg[s].kind;
                        cnt[kd]++;
                        if (kd >= BF_SEG_LOOPRUN && kd <= BF_SEG_LOOPRUN_AFF_S3) nlr++;
                    }
                }
            }
            printf("//-- Nest: %d loops compiled, %d left to Eval"
                   " (segments: aff=%d ops=%d scan=%d mzscan=%d valscan=%d"
                   " looprun=%d nest=%d fwd=%d)\n",
                   nn, nrem, cnt[BF_SEG_AFF], cnt[BF_SEG_OPS], cnt[BF_SEG_PTRS],
                   cnt[BF_SEG_MZSCAN], cnt[BF_SEG_VALSCAN], nlr,
                   cnt[BF_SEG_NEST], cnt[BF_SEG_FWD]);
        }
#endif
    }

    _bfe_vo(bfo[pc], bfo_EOP, 0, 0);

    if (bfoptr) *(bf_op**)bfoptr = bfo;
    else free(bfo);

    return pc;

OPT_ERROR:
    printf("// error - unbalanced braces or nesting too deep\n");
    free(bfo);
    return -1;
}

#endif // BFFSREE_OPT_IMPLEMENTATION
