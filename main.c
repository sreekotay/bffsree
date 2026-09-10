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
#if BF_AFFINE
            if (bfo[i].cmd == bfo_LOOPRUN) {
                const bf_affine *m = bfo[i].aux ? bf_affine_get(bfo[i].aux) : 0;
                bf_affine tmp;
                char line[512];
                if (!m && i + bfo[i].val <= vm->progLen_op &&
                    bf_affine_from_body(bfo + i + 1, bfo[i].val - 1, &tmp))
                    m = &tmp;
                if (m && bf_affine_format(m, line, (int)sizeof line))
                    {
                        const char *kn = (bfo[i].aux && m->kind)
                            ? bf_affine_kind_name((int)m->kind) : 0;
                        printf("        // affine%s%s %s\n",
                               kn ? " " : "", kn ? kn : "", line);
                    }
            }
#endif
            if (bfo[i].cmd == bfo_FWD || bfo[i].cmd == bfo_NEST) {
                char sig[128];
                if (bf_nest_signature(bfo, i, sig, (int)sizeof sig) >= 0)
                    printf("        // body \"%s\"\n", sig);
            }
            if (bfo[i].cmd == bfo_NEST) {
                const bf_nest *n = bf_nest_get(bfo[i].aux);
                int s;
                if (n) {
                    printf("        // nest: template %d,", n->tmpl);
                    for (s = 0; s < n->nseg; s++) {
                        const bf_seg *g = &n->seg[s];
                        switch (g->kind) {
                        case BF_SEG_AFF:
#if BF_AFFINE
                            printf(" aff.%s", g->aff ? bf_affine_kind_name(g->aff->kind) : "?");
#endif
                            break;
                        case BF_SEG_OPS:     printf(" ops[%d,%d)", i + g->a, i + g->b); break;
                        case BF_SEG_PTRS:    printf(" scan%+d", g->a); break;
                        case BF_SEG_MZSCAN:  printf(" mzscan@%d", i + g->a); break;
                        case BF_SEG_VALSCAN: printf(" valscan@%d", i + g->a); break;
                        case BF_SEG_LOOPRUN:
                        case BF_SEG_LOOPRUN_MZ_MUL_MZ_VAL:
                        case BF_SEG_LOOPRUN_FRAME9:
                        case BF_SEG_LOOPRUN_VAL_MUL_MZ_MZ:
                        case BF_SEG_LOOPRUN_AFF_S1:
                        case BF_SEG_LOOPRUN_AFF_S2Z:
                        case BF_SEG_LOOPRUN_AFF_S2:
                        case BF_SEG_LOOPRUN_AFF_S3:
                            printf(" looprun@%d", i + g->a); break;
                        case BF_SEG_NEST:    printf(" nest@%d", i + g->a); break;
                        case BF_SEG_FWD:     printf(" fwd@%d", i + g->a); break;
                        default:             printf(" ?"); break;
                        }
                    }
                    printf("\n");
                }
            }
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
    int ntop2 = 0, top2[12];
    unsigned long long *inloop = 0;

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
        if (k != bfo_REW && k != bfo_MZSCAN && k != bfo_VALSCAN &&
            k != bfo_LOOPRUN && k != bfo_NEST) continue;
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

    // Unconverted loops ranked by ops Eval dispatched *directly* in
    // their body (not inside nested FWD/REW or block ops). This is
    // where dispatch time goes; iteration counts alone hide it.
    inloop = (unsigned long long*)calloc((size_t)n + 1, sizeof(*inloop));
    if (inloop) {
        for (i = 0; i < n; i++) {
            if (bfo[i].cmd != bfo_FWD) continue;
            {
                int e = i + bfo[i].val;
                unsigned long long sum = prof[e];  // REW back-edges
                for (j = i + 1; j < e; j++) {
                    int c = bfo[j].cmd;
                    if (c == bfo_FWD) { j += bfo[j].val; continue; }  // nested loop
                    if (c == bfo_MZSCAN || c == bfo_VALSCAN) { sum += 0; j += 2; continue; }
                    if (c == bfo_LOOPRUN || c == bfo_NEST) { j += bfo[j].val; continue; }
                    sum += prof[j];
                }
                inloop[i] = sum;
            }
        }
        for (i = 0; i < n; i++) {
            if (bfo[i].cmd != bfo_FWD || inloop[i] == 0) continue;
            for (j = 0; j < ntop2; j++) if (inloop[i] > inloop[top2[j]]) break;
            if (j < 12) {
                for (k = (ntop2 < 12 ? ntop2 : 11); k > j; k--) top2[k] = top2[k - 1];
                top2[j] = i;
                if (ntop2 < 12) ntop2++;
            }
        }
        if (ntop2) fprintf(stderr, "//-- unconverted loops by direct dispatches (body ops + back-edges):\n");
        for (k = 0; k < ntop2; k++) {
            int s = top2[k], e = s + bfo[s].val;
            fprintf(stderr, "//   [%d] %llu dispatches, %llu iterations, %d body ops:\n",
                    s, inloop[s], prof[e], bfo[s].val - 1);
            for (i = s; i <= e; i++) {
                const char *tag = "";
                if (i > s && i < e) {
                    int c = bfo[i].cmd;
                    if (c == bfo_FWD || c == bfo_REW) tag = "  (nested)";
                    else if (c == bfo_MZSCAN || c == bfo_VALSCAN ||
                             c == bfo_LOOPRUN || c == bfo_NEST) tag = "  (block)";
                }
                fprintf(stderr, "//     [%d] %-9s val=%-6d off=%-4d buf=%-4d %llu%s\n",
                        i, op_names[bfo[i].cmd], bfo[i].val, bfo[i].off, bfo[i].buf,
                        prof[i], tag);
            }
        }
        free(inloop);
    }

    // BF_PROF_DUMP=1: every op with its count, for offline analysis
    if (getenv("BF_PROF_DUMP")) {
        fprintf(stderr, "//-- op dump: idx cmd val off buf count\n");
        for (i = 0; i < n; i++)
            fprintf(stderr, "//= %d %s %d %d %d %llu\n",
                    i, op_names[bfo[i].cmd], bfo[i].val, bfo[i].off, bfo[i].buf, prof[i]);
    }
}
#endif // BF_PROFILE

// -----------------------------
// main
// -----------------------------
int main(int argc, char* argv[]) {
    return bffsree_Main(argc, argv);
}
