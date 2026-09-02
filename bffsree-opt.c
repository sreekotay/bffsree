// =====================================================================
// bffsree-opt.c
// =====================================================================
#ifdef BFFSREE_OPT_IMPLEMENTATION

#include "bffsree.h"

// optimization macros
#define _bfe_(e,c)            do { (e).cmd=(uint8_t)(c); } while(0)
#define _bfe_v(e,c,v)         do { (e).cmd=(uint8_t)(c); (e).val=(int32_t)(v); } while(0)
#define _bfe_vo(e,c,v,o)      do { (e).cmd=(uint8_t)(c); (e).val=(int32_t)(v); (e).off=(bf_off_t)(o); (e).buf=0; } while(0)
#define _bfe_vob(e,c,v,o,b)   do { (e).cmd=(uint8_t)(c); (e).val=(int32_t)(v); (e).off=(bf_off_t)(o); (e).buf=(bf_op_buf_t)(b); } while(0)

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
// Convert remaining walking loops into internal superinstructions.
// NAVLOOP recognizes the two generated stack-navigation shapes
//   VAL, PTR_S, PTR_S, VAL, PTR_S [, PTR_S]
// while LOOPRUN handles straight-line arithmetic. Must run after
// bf_foldNoops so FWD jump distances are final. Body and REW ops stay
// in place as the parameter block.
// ----------------------------
static void bf_markLoopRuns(bf_op* bfo, int pc) {
    int i, j, n, c, okb;

    for (i = 0; i < pc; i++) {
        if (bfo[i].cmd != bfo_FWD) continue;
        n = bfo[i].val;
        if (n < 2 || i + n >= pc || bfo[i + n].cmd != bfo_REW) continue;
        if ((n == 6 || n == 7) &&
            bfo[i + 1].cmd == bfo_VAL &&
            bfo[i + 2].cmd == bfo_PTR_S &&
            bfo[i + 3].cmd == bfo_PTR_S &&
            bfo[i + 4].cmd == bfo_VAL &&
            bfo[i + 5].cmd == bfo_PTR_S &&
            (n == 6 || bfo[i + 6].cmd == bfo_PTR_S)) {
            bfo[i].cmd = bfo_NAVLOOP;
            continue;
        }
        okb = 1;
        for (j = i + 1; j < i + n; j++) {
            c = bfo[j].cmd;
            if (c != bfo_VAL && c != bfo_VAL_MZ && c != bfo_VAL_MUL &&
                c != bfo_VAL_ZERO && c != bfo_MUL_MUL) { okb = 0; break; }
        }
        if (okb) bfo[i].cmd = bfo_LOOPRUN;
    }
}

// ----------------------------
// Program optimization
// ----------------------------
int bf_Optimize(void** bfoptr, char* chars, int proglen, int printMetrics) {
    bf_op* bfo = (bf_op*)malloc(sizeof(bf_op) * (size_t)(proglen + 1));
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
