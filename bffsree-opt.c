// =====================================================================
// bffsree-opt.c
// =====================================================================
#ifdef BFFSREE_OPT_IMPLEMENTATION

#include "bffsree.h"

// optimization macros
#define _bfe_(e,c)            do { (e).cmd=(uint8_t)(c); (e).aux=0; } while(0)
#define _bfe_v(e,c,v)         do { (e).cmd=(uint8_t)(c); (e).aux=0; (e).val=(int32_t)(v); } while(0)
#define _bfe_vo(e,c,v,o)      do { (e).cmd=(uint8_t)(c); (e).aux=0; (e).val=(int32_t)(v); (e).off=(bf_off_t)(o); (e).buf=0; } while(0)
#define _bfe_vob(e,c,v,o,b)   do { (e).cmd=(uint8_t)(c); (e).aux=0; (e).val=(int32_t)(v); (e).off=(bf_off_t)(o); (e).buf=(bf_op_buf_t)(b); } while(0)

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
static int bf_foldNoops(bf_op* bfo, int pc) {
    int*  nidx = (int*)malloc(sizeof(int) * (size_t)(pc + 1));
    char* del  = (char*)malloc((size_t)(pc + 1));
    int i, w, acc = 0;

    if (nidx && del) {
        // pass 1: mark deletable NOOPs and assign new indices.
        // acc tracks the fold target's accumulated off so the
        // result is guaranteed to fit bf_off_t.
        for (i = 0, w = 0; i < pc; i++) {
            nidx[i] = w;
            if (bfo[i].cmd == bfo_NOOP && w > 0 &&
                acc + bfo[i].off >= -32768 && acc + bfo[i].off <= 32767) {
                del[i] = 1;
                acc += bfo[i].off;
                continue;
            }
            del[i] = 0;
            acc = bfo[i].off;
            w++;
        }
        nidx[pc] = w;

        // pass 2: renumber jump distances against new indices
        for (i = 0; i < pc; i++) {
            if (bfo[i].cmd == bfo_FWD || bfo[i].cmd == bfo_REW)
                bfo[i].val = nidx[i + bfo[i].val] - nidx[i];
        }

        // pass 3: compact, folding each deleted NOOP's off backward
        for (i = 0, w = 0; i < pc; i++) {
            if (del[i]) {
                bfo[w - 1].off = (bf_off_t)(bfo[w - 1].off + bfo[i].off);
                continue;
            }
            bfo[w++] = bfo[i];
        }
        pc = w;
    }
    _myfree(nidx);
    _myfree(del);
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
                c != bfo_VAL_ZERO && c != bfo_MUL_MUL) { okb = 0; break; }
        }
        if (!okb) continue;
        bfo[i].cmd = bfo_LOOPRUN;
#if BF_AFFINE && BF_AFFINE_APPLY
        if (bf_affine_from_body(bfo + i + 1, n - 1, &map) && map.kind) {
            int id = bf_affine_intern(&map);
            if (id > 0) bfo[i].aux = (uint16_t)id;
        }
#endif
    }
}

// ----------------------------
// Program optimization
// ----------------------------
int bf_Optimize(void** bfoptr, char* chars, int proglen, int printMetrics) {
#if BF_AFFINE
    bf_affine_reset();
#endif
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
    bf_markLoopRuns(bfo, pc);

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
