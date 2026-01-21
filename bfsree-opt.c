// =====================================================================
// bfsree_opt.c
// =====================================================================
#ifdef BFSREE_OPT_IMPLEMENTATION

#include "bfsree.h"

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
    int minsp, maxsp;

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
    minsp = maxsp = sp;
    while (canopt && (cmd = (enum ebfo_CMD)bfo[pc].cmd) != bfo_REW) {
        if (sp > maxsp) maxsp = sp;
        if (sp < minsp) minsp = sp;
        switch (cmd) {
        case bfo_VAL:
            if (sp == 0) lc += bfo[pc].val;
            else vtrack[sp] |= 2 | 1;
            break;

        case bfo_VAL_MUL:
            if (_loop_var(vtrack[sp])) canopt = 0;
            if (_loop_var(vtrack[sp + bfo[pc].buf])) canopt = 0;
            vtrack[sp + bfo[pc].buf] |= 2 | 1;
            vtrack[sp] |= 1;
            if (sp == 0) canopt = 0;
            break;

        case bfo_VAL_MZ:
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
    opptr = (rc <= (int)(sizeof(*opptr) * sizeof(opstack))) ? opstack : (bf_op*)malloc((size_t)rc);
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
}

// ----------------------------
// Program optimization
// ----------------------------
int bf_Optimize(void** bfoptr, char* chars, int proglen, int printMetrics) {
    bf_op* bfo = (bf_op*)malloc(sizeof(bf_op) * (size_t)(proglen + 1));
    int lstack[bf_MEMDEFAULT];
    int sstack[bf_MEMDEFAULT];

    int pc = 0, rpc = 0;
    int cci = 0, sp = 0, c;
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
                sp += off;
                pc++;
                break;
            }

            sstack[loop] = sp;
            sp = 0;
            lstack[loop++] = pc;

            rpc = valcounter(&cci, chars, rpc, proglen);
            rpc = ptrcounter(&off, chars, rpc, proglen);
            _bfe_vob(bfo[pc], bfo_FWD, pc, off, cci);
            sp += off;
            pc++;
            break;

        case bf_CLOSE:
            if (loop <= 0) goto OPT_ERROR;

            l = lstack[--loop];
            sp = sstack[loop];

            rpc = valcounter(&cci, chars, rpc, proglen);
            rpc = ptrcounter(&off, chars, rpc, proglen);

            _bfe_v(bfo[l], bfo_FWD, pc - l);
            _bfe_vob(bfo[pc], bfo_REW, l - pc, off, cci);
            sp += off;
            pc++;

            tc = optimizeLoop(bfo, l);
            if (tc > 0) pc = tc;
            break;

        case bf_GT:
        case bf_LT:
            rpc = ptrcounter(&off, chars, rpc, proglen);
            off += (c == bf_GT) ? 1 : -1;
            _bfe_vo(bfo[pc], bfo_NOOP, 0, off);
            sp += off;
            pc++;
            break;

        case bf_PLUS:
        case bf_MINUS:
            rpc = valcounter(&cci, chars, rpc, proglen);
            cci += (c == bf_PLUS) ? 1 : -1;
            rpc = ptrcounter(&off, chars, rpc, proglen);
            _bfe_vo(bfo[pc], (cci == 0) ? bfo_NOOP : bfo_VAL, cci, off);
            sp += off;
            pc++;
            break;

        case bf_PERIOD:
            rpc = ptrcounter(&off, chars, rpc, proglen);
            _bfe_vo(bfo[pc], bfo_PUT, bfo[pc].val, off);
            sp += off;
            pc++;
            break;

        case bf_COMMA:
            rpc = ptrcounter(&off, chars, rpc, proglen);
            _bfe_vo(bfo[pc], bfo_GET, bfo[pc].val, off);
            sp += off;
            pc++;
            break;

        default:
            break;
        }

        rpc++;
    }

    if (printMetrics) {
        printf("//-- Optimization: Instructions [%d -> %d] using Bytes [%d -> %d] (op=%d bytes)\n",
               proglen, pc, proglen, (int)(pc * (int)sizeof(bf_op)), (int)sizeof(bf_op));
    }

    _bfe_vo(bfo[pc], bfo_EOP, 0, 0);

    if (bfoptr) *(bf_op**)bfoptr = bfo;
    else free(bfo);

    return pc;

OPT_ERROR:
    printf("OPT_ERROR --- unbalanced '['\n");
    free(bfo);
    return -1;
}

#endif // BFSREE_OPT_IMPLEMENTATION
