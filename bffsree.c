// =====================================================================
// bffsree.c
// =====================================================================
#ifdef BFFSREE_IMPLEMENTATION

#define _CRT_SECURE_NO_WARNINGS
#include "bffsree.h"

#ifndef _refInterp
#define _refInterp 0
#endif

#if BF_ZERO_INDEX && !_refInterp
#if !defined(__GNUC__)
#error "BF_ZERO_INDEX currently requires GCC or Clang bit-scan builtins"
#endif

typedef struct bf_zero_index {
    uint64_t* z3;
    uint64_t* z8;
    bf_cell* base;
    size_t total;
    size_t n3;
    size_t n8;
    size_t words3;
    size_t words8;
} bf_zero_index;

static void bf_zero_set(uint64_t* bits, size_t bit, int is_zero) {
    uint64_t mask = UINT64_C(1) << (bit & 63u);
    if (is_zero) bits[bit >> 6] |= mask;
    else         bits[bit >> 6] &= ~mask;
}

static void bf_zero_update(bf_zero_index* zi, bf_cell* cell) {
    size_t a;
    if (!zi->z3) return;
    a = (size_t)(cell - zi->base);
    if (a >= zi->total) return;
    bf_zero_set(zi->z3, (a % 3u) * zi->n3 + a / 3u, *cell == 0);
    bf_zero_set(zi->z8, (a % 8u) * zi->n8 + a / 8u, *cell == 0);
}

static int bf_zero_init(bf_zero_index* zi, bf_cell* base, size_t total) {
    size_t i;
    memset(zi, 0, sizeof(*zi));
    zi->base = base;
    zi->total = total;
    zi->n3 = (total + 2u) / 3u;
    zi->n8 = (total + 7u) / 8u;
    zi->words3 = (3u * zi->n3 + 63u) / 64u;
    zi->words8 = (8u * zi->n8 + 63u) / 64u;
    zi->z3 = (uint64_t*)calloc(zi->words3, sizeof(uint64_t));
    zi->z8 = (uint64_t*)calloc(zi->words8, sizeof(uint64_t));
    if (!zi->z3 || !zi->z8) {
        free(zi->z3);
        free(zi->z8);
        zi->z3 = zi->z8 = 0;
        return -1;
    }
    for (i = 0; i < total; i++)
        if (base[i] == 0) bf_zero_update(zi, base + i);
    return 0;
}

static void bf_zero_free(bf_zero_index* zi) {
    free(zi->z3);
    free(zi->z8);
    zi->z3 = zi->z8 = 0;
}

static bf_cell* bf_zero_scan(bf_zero_index* zi, bf_cell* cell, int stride) {
    uint64_t* bits;
    uint64_t word;
    size_t a, r, q, bit, wi, n;
    int k = stride < 0 ? -stride : stride;

    if (!zi->z3 || (k != 3 && k != 8)) return 0;
    bits = (k == 3) ? zi->z3 : zi->z8;
    n = (k == 3) ? zi->n3 : zi->n8;
    a = (size_t)(cell - zi->base);
    r = a % (size_t)k;
    q = a / (size_t)k;
    bit = r * n + q;
    wi = bit >> 6;

    if (stride > 0) {
        word = bits[wi] & (UINT64_MAX << (bit & 63u));
        while (!word) word = bits[++wi];
        bit = (wi << 6) + (size_t)__builtin_ctzll(word);
    } else {
        word = bits[wi] & (UINT64_MAX >> (63u - (bit & 63u)));
        while (!word) word = bits[--wi];
        bit = (wi << 6) + 63u - (size_t)__builtin_clzll(word);
    }

    q = bit - r * n;
    return zi->base + r + (size_t)k * q;
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
#if BF_ZERO_INDEX
    bf_zero_index zi;
    unsigned int zscan_calls;
    unsigned long long zscan_steps;
    unsigned long long zwrite_count;
    int zscan_decided;
#endif
#endif
    int pc = vm->pc;
    int sp = vm->sp;
    int c, icount = ocount;

    if (ptr == 0) {
        if (ptrLen == 0) ptrLen = bf_MAXCELLS;
        ptr = (bf_cell*)calloc((size_t)ptrLen + 2u * BF_TAPE_PAD, sizeof(bf_cell)) + BF_TAPE_PAD;
    }

#if BF_ZERO_INDEX && !_refInterp
    memset(&zi, 0, sizeof(zi));
    zscan_calls = 0;
    zscan_steps = 0;
    zwrite_count = 0;
    zscan_decided = 0;
#endif

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
#else
    #define _bf_prof(op)  do { } while (0)
#endif
#if !defined(NDEBUG)
    #define _bf_tick()  do { if (icount-- <= 0) goto DONE; } while (0)
#else
    #define _bf_tick()  do { } while (0)
#endif
#if BF_ZERO_INDEX
    #define _bf_zupdate(I) \
        do { \
            if (zi.z3) bf_zero_update(&zi, ptr + (I)); \
            else if (!zscan_decided) zwrite_count++; \
        } while (0)
#else
    #define _bf_zupdate(I) do { } while (0)
#endif

#if BF_ZERO_INDEX
    #define _bf_scan(P) \
        do { \
            size_t zs = 0; \
            c = (P)->val; \
            tp = bf_zero_scan(&zi, ptr + sp, c); \
            if (!tp) { \
                tp = ptr + sp; \
                while (*tp) { tp += c; zs++; } \
                if (!zscan_decided && (c == 3 || c == -3 || c == 8 || c == -8)) { \
                    zscan_calls++; \
                    zscan_steps += zs; \
                    if (zscan_calls == 4096u) { \
                        zscan_decided = 1; \
                        if (BF_ZERO_INDEX_TRACE) \
                            fprintf(stderr, "// zero-index sample: scans=%u steps=%llu writes=%llu\\n", \
                                zscan_calls, zscan_steps, zwrite_count); \
                        if (zscan_steps > 32u * zscan_calls) \
                            bf_zero_init(&zi, ptr - BF_TAPE_PAD, \
                                (size_t)ptrLen + 2u * BF_TAPE_PAD); \
                    } \
                } \
            } \
            sp = (int)(tp - ptr); \
            if (_mybounds(sp, ptrLen)) goto ERROR_BF; \
        } while (0)
#else
    #define _bf_scan(P) \
        do { \
            c = (P)->val; \
            tp = ptr + sp; \
            while (*tp) tp += c; \
            sp = (int)(tp - ptr); \
            if (_mybounds(sp, ptrLen)) goto ERROR_BF; \
        } while (0)
#endif

    // -----------------------------------------------------------------
    // Op semantics: the single source of truth. P is the op being
    // executed (bfo in the dispatch arms, the body cursor inside
    // LOOPRUN). Each op leaves bfo on the slot whose off the dispatch
    // tail must apply: jumps land on the FWD/REW partner, block ops
    // land on their last parameter slot.
    // -----------------------------------------------------------------
    #define _op_NOOP(P)     do { } while (0)
    #define _op_VAL(P)      do { ptr[sp] += (bf_cell)(P)->val; \
                                 _bf_zupdate(sp); } while (0)
    #define _op_PUT(P)      do { vm->putcp(vm->putdata, ptr[sp]); } while (0)
    #define _op_GET(P)      do { ptr[sp] = (inp && *inp) ? (bf_cell)*inp++ \
                                         : (bf_cell)vm->getcp(vm->getdata); \
                                 _bf_zupdate(sp); } while (0)
    #define _op_FWD(P)      do { if (ptr[sp] == 0) (P) += (P)->val; \
                                 ptr[sp] += (bf_cell)(P)->buf; \
                                 _bf_zupdate(sp); } while (0)
    #define _op_REW(P)      do { if (ptr[sp] != 0) (P) += (P)->val; \
                                 ptr[sp] += (bf_cell)(P)->buf; \
                                 _bf_zupdate(sp); } while (0)
    #define _op_PTR_S(P)    _bf_scan(P)
    #define _op_VAL_MZ(P)   do { ptr[sp + (P)->buf] += (bf_cell)((P)->val * ptr[sp]); \
                                 ptr[sp] = 0; \
                                 _bf_zupdate(sp + (P)->buf); \
                                 _bf_zupdate(sp); } while (0)
    #define _op_VAL_MUL(P)  do { ptr[sp + (P)->buf] += (bf_cell)((P)->val * ptr[sp]); \
                                 _bf_zupdate(sp + (P)->buf); } while (0)
    #define _op_VAL_ZERO(P) do { ptr[sp] = (bf_cell)(P)->val; \
                                 _bf_zupdate(sp); } while (0)
    #define _op_MUL_MUL(P)  do { ptr[sp + (P)->buf] *= (bf_cell)((P)->val * ptr[sp]); \
                                 _bf_zupdate(sp + (P)->buf); } while (0)
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
                _bf_zupdate(sp); \
                sp += (P)->off; \
                for (;;) { \
                    WORK; \
                    sp += (P)[1].off; \
                    if (ptr[sp] == 0) break; \
                    _bf_prof(P); \
                    ptr[sp] += (bf_cell)(P)->buf; \
                    _bf_zupdate(sp); \
                    sp += (P)->off; \
                } \
                if (_mybounds(sp, ptrLen)) goto ERROR_BF; \
            } \
            (P) += 2; \
            ptr[sp] += (bf_cell)(P)->buf; \
            _bf_zupdate(sp); \
        } while (0)
    #define _op_MZSCAN(P)   _op_SCANLOOP(P, \
        do { \
            ptr[sp + (P)[1].buf] += (bf_cell)((P)[1].val * ptr[sp]); \
            ptr[sp] = 0; \
            _bf_zupdate(sp + (P)[1].buf); \
            _bf_zupdate(sp); \
        } while (0))
    #define _op_VALSCAN(P)  _op_SCANLOOP(P, \
        do { \
            ptr[sp] += (bf_cell)(P)[1].val; \
            _bf_zupdate(sp); \
        } while (0))

    // LOOPRUN: a walking loop whose straight-line arithmetic body is
    // interpreted internally, reusing the op bodies above (body and
    // ']' stay in place; same FWD/REW semantics and sentinel-pad exit
    // rules as the scans)
    #define _op_LOOPRUN(P) \
        do { \
            bf_op* br = (P) + (P)->val; \
            bf_op* b; \
            if (ptr[sp] != 0) { \
                ptr[sp] += (bf_cell)(P)->buf; \
                _bf_zupdate(sp); \
                sp += (P)->off; \
                for (;;) { \
                    for (b = (P) + 1; b != br; b++) { \
                        if (_mybounds(sp, ptrLen)) goto ERROR_BF; \
                        switch (b->cmd) { \
                        case bfo_VAL:      _op_VAL(b);      break; \
                        case bfo_VAL_MZ:   _op_VAL_MZ(b);   break; \
                        case bfo_VAL_MUL:  _op_VAL_MUL(b);  break; \
                        case bfo_VAL_ZERO: _op_VAL_ZERO(b); break; \
                        case bfo_MUL_MUL:  _op_MUL_MUL(b);  break; \
                        } \
                        sp += b->off; \
                    } \
                    if (_mybounds(sp, ptrLen)) goto ERROR_BF; \
                    if (ptr[sp] == 0) break; \
                    _bf_prof(P); \
                    ptr[sp] += (bf_cell)(P)->buf; \
                    _bf_zupdate(sp); \
                    sp += (P)->off; \
                } \
            } \
            (P) = br; \
            ptr[sp] += (bf_cell)(P)->buf; \
            _bf_zupdate(sp); \
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
    #undef _bf_zupdate
    #undef _bf_scan

DONE:
#if BF_ZERO_INDEX
    bf_zero_free(&zi);
#endif
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
