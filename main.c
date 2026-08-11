// =====================================================================
// main.c - Entry point for bffsree Brainfuck interpreter
// (bffsree = BrainFuck For Sree)
// =====================================================================

#define BFFSREE_IMPLEMENTATION
#define BFFSREE_OPT_IMPLEMENTATION

#include "bffsree.h"
#include "bffsree.c"
#include "bffsree-opt.c"

static const char* op_names[] = {
#define X(n) #n,
    BF_OP_LIST(X)
#undef X
};

// -----------------------------
// bffsree_Print - Debug/output helper
// -----------------------------
void bffsree_Print(bf_VM* vm, char* inp, int lang) {
    bf_op* bfo = (bf_op*)vm->prog_op;
    int i;

    (void)inp;

    if (!bfo) {
        printf("// No optimized program available\n");
        return;
    }

    if (lang == 0) {
        // JSON output
        printf("[\n");
        for (i = 0; i < vm->progLen_op; i++) {
            const char* name = (bfo[i].cmd < bfo_Total) ? op_names[bfo[i].cmd] : "???";
            printf("  { \"op\": \"%s\", \"val\": %d, \"off\": %d, \"buf\": %d }%s\n",
                   name, bfo[i].val, bfo[i].off, bfo[i].buf,
                   (i < vm->progLen_op - 1) ? "," : "");
        }
        printf("]\n");
    } else {
        // C-like output
        printf("// Optimized IR (%d ops):\n", vm->progLen_op);
        for (i = 0; i < vm->progLen_op; i++) {
            const char* name = (bfo[i].cmd < bfo_Total) ? op_names[bfo[i].cmd] : "???";
            printf("  [%3d] %-10s val=%-6d off=%-4d buf=%d\n",
                   i, name, bfo[i].val, bfo[i].off, bfo[i].buf);
        }
    }
}

#if BF_PROFILE
// -----------------------------
// bffsree_ProfileReport - dynamic op histogram + hottest loop sites,
// written to stderr after the run (stdout stays program output)
// -----------------------------
void bffsree_ProfileReport(bf_VM* vm) {
    bf_op* bfo = (bf_op*)vm->prog_op;
    unsigned long long* prof = vm->prof;
    unsigned long long bycmd[bfo_Total] = {0};
    unsigned long long tot = 0;
    int i, j, k, n = vm->progLen_op;
    int top[10], nt = 0;

    if (!prof || !bfo) return;

    for (i = 0; i < n; i++) { bycmd[bfo[i].cmd] += prof[i]; tot += prof[i]; }
    if (tot == 0) return;

    fprintf(stderr, "//-- profile: %llu op executions (loop ops count iterations)\n", tot);
    for (k = 0; k < bfo_Total; k++)
        if (bycmd[k])
            fprintf(stderr, "//   %-9s %14llu  %5.1f%%\n",
                    op_names[k], bycmd[k], 100.0 * (double)bycmd[k] / (double)tot);

    // hottest loop sites (REW = unconverted loops; the SCAN/RUN
    // superinstructions count internal iterations at their own site)
    for (i = 0; i < n; i++) {
        k = bfo[i].cmd;
        if (k != bfo_REW && k != bfo_MZSCAN && k != bfo_VALSCAN && k != bfo_LOOPRUN) continue;
        if (prof[i] == 0) continue;
        for (j = 0; j < nt; j++) if (prof[i] > prof[top[j]]) break;
        if (j < 10) {
            for (k = (nt < 10 ? nt : 9); k > j; k--) top[k] = top[k - 1];
            top[j] = i;
            if (nt < 10) nt++;
        }
    }
    if (nt) fprintf(stderr, "//-- hottest loop sites:\n");
    for (k = 0; k < nt; k++) {
        int s = top[k], e = top[k];
        if (bfo[s].cmd == bfo_REW)                                    s += bfo[s].val;  // partner FWD
        else if (bfo[s].cmd == bfo_MZSCAN || bfo[s].cmd == bfo_VALSCAN) e += 2;
        else                                                          e += bfo[s].val;  // LOOP_RUN
        fprintf(stderr, "//   [%d] %llu iterations:\n", top[k], prof[top[k]]);
        for (i = s; i <= e; i++)
            fprintf(stderr, "//     %-9s val=%-6d off=%-4d buf=%d\n",
                    op_names[bfo[i].cmd], bfo[i].val, bfo[i].off, bfo[i].buf);
    }
}
#endif // BF_PROFILE

// -----------------------------
// main
// -----------------------------
int main(int argc, char* argv[]) {
    return bffsree_Main(argc, argv);
}
