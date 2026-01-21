// =====================================================================
// main.c - Entry point for bfsree Brainfuck interpreter
// =====================================================================

#define BFSREE_IMPLEMENTATION
#define BFSREE_OPT_IMPLEMENTATION

#include "bfsree.h"
#include "bfsree.c"
#include "bfsree-opt.c"

// -----------------------------
// bfsree_Print - Debug/output helper
// -----------------------------
void bfsree_Print(bf_VM* vm, char* inp, int lang) {
    bf_op* bfo = (bf_op*)vm->prog_op;
    int i;

    (void)inp;

    if (!bfo) {
        printf("// No optimized program available\n");
        return;
    }

    static const char* op_names[] = {
        "NOOP", "VAL", "VAL_M", "PUT", "GET", "FWD", "REW",
        "PTR_S", "MUL_MUL", "VAL_MZ", "VAL_MUL", "VAL_ZERO", "DEBUG", "EOP"
    };

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

// -----------------------------
// main
// -----------------------------
int main(int argc, char* argv[]) {
    return bfsree_Main(argc, argv);
}
