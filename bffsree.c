// =====================================================================
// bffsree.c
// =====================================================================
#ifdef BFFSREE_IMPLEMENTATION

#define _CRT_SECURE_NO_WARNINGS
#include "bffsree.h"

#ifndef _refInterp
#define _refInterp 0
#endif

// =====================================================================
// main VM loop for bfi
// =====================================================================
int bffsree_Eval(bf_VM* vm, char* inp, int ocount) {
    bf_cell* ptr = vm->tape;
    bf_op* bfo   = (bf_op*)vm->prog_op;
    int ptrLen = vm->tapeLen;
#if _refInterp
    char* chars  = vm->prog;
    bf_VM_help* ph = vm->progHelper;
#endif
    int pc = vm->pc;
    int sp = vm->sp;
    int c, icount = ocount;
    bf_cell* tp;

    if (ptr == 0) {
        if (ptrLen == 0) ptrLen = bf_MAXCELLS;
        ptr = (bf_cell*)malloc((size_t)ptrLen * sizeof(bf_cell));
        memset(ptr, 0, (size_t)ptrLen * sizeof(bf_cell));
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
        vm->putcp(vm->putdata, 10);
        if (ptr && vm->tape == 0) free(ptr);
    } else {
        vm->pc = pc;
        vm->sp = sp;
    }
#else
    bfo += pc;
    do {
        c = bfo->cmd;
        switch (c) {
        case bfo_NOOP:      /*nothing*/                                     break;
        case bfo_VAL:       ptr[sp] += (bf_cell)bfo->val;                      break;
        case bfo_PUT:       vm->putcp(vm->putdata, ptr[sp]);                break;
        case bfo_GET:       ptr[sp] = (inp && *inp) ? (bf_cell)*inp++ : (bf_cell)vm->getcp(vm->getdata); break;
        case bfo_FWD:       if (ptr[sp] == 0) bfo += bfo->val;
                            ptr[sp] += (bf_cell)bfo->buf;
                            break;
        case bfo_REW:       if (ptr[sp] != 0) bfo += bfo->val;
                            ptr[sp] += (bf_cell)bfo->buf;
                            break;
        case bfo_PTR_S:     c = bfo->val; tp = ptr + sp; while (*tp) tp += c; sp = (int)(tp - ptr);
                            if (_mybounds(sp, ptrLen)) goto ERROR_BF;
                            break;
        case bfo_VAL_MZ:    ptr[sp + bfo->buf] += (bf_cell)(bfo->val * ptr[sp]);
                            ptr[sp] = 0;
                            break;
        case bfo_VAL_MUL:   ptr[sp + bfo->buf] += (bf_cell)(bfo->val * ptr[sp]);
                            break;
        case bfo_VAL_ZERO:  ptr[sp] = (bf_cell)bfo->val;
                            break;
        case bfo_MUL_MUL:   ptr[sp + bfo->buf] *= (bf_cell)(bfo->val * ptr[sp]);
                            break;
        case bfo_EOP:       bfo = 0; goto DONE;
        }

        sp += bfo->off;
        bfo++;
        if (_mybounds(sp, ptrLen)) goto ERROR_BF;
    } while (1 || icount--);

DONE:
    if (bfo == 0) {
        vm->pc = -1;
        vm->putcp(vm->putdata, 10);
        if (ptr && vm->tape == 0) free(ptr);
    } else {
        vm->pc = (int)(bfo - (bf_op*)vm->prog_op);
        vm->sp = sp;
    }
#endif

    return ocount - icount + 1;

ERROR_BF:
    printf("// memory exception\n");
    bfo = 0; pc = -1;
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
                if (_refInterp && ci && prog[ci - 1] == c) { ci--; progHelp[ci].v++; }
                else                                        { progHelp[ci].v = 1; }
                break;

            default:
                progHelp[ci].v = 1;
                break;
            }
            prog[ci++] = (char)c;
        }
    }
    prog[ci++] = 0;
    proglen = ci;
    prog = (char*)realloc(prog, (size_t)ci);
    if (c == '!') bf_readfile(&inp, fh);
    if (fh && fh != stdin) fclose(fh);

    // run
    bf_VM_alloc(&vm);
    bf_VM_tape(&vm, bf_MAXCELLS);
    vm.prog       = prog;
    vm.progLen    = proglen;
    vm.progHelper = progHelp;
    vm.progLen_op = bf_Optimize(&vm.prog_op, vm.prog, vm.progLen, metric);
    if (printBF == 2)        bffsree_Print(&vm, inp, 0);
    else if (printBF == 1)   bffsree_Print(&vm, inp, 1);
    else {
        carg = 0;
        do { carg += bffsree_Eval(&vm, inp, 10000); } while (vm.pc > 0);
        if (carg != 1) printf("//instructions: [%d]\n", carg);
    }
    bf_VM_free(&vm);

    // done
    if (inp) free(inp);
    return 0;
}

#endif // BFFSREE_IMPLEMENTATION
