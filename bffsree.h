// =====================================================================
// bffsree.h
// =====================================================================
#ifndef _BFF_SREE_H_
#define _BFF_SREE_H_

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// -----------------------------
// Configuration (compile-time)
// -----------------------------
#ifndef BF_CELL_BITS
#define BF_CELL_BITS 8
#endif

#ifndef BF_CELL_SIGNED
#define BF_CELL_SIGNED 0
#endif

#if BF_CELL_BITS == 8
  #if BF_CELL_SIGNED
    typedef int8_t  bf_cell;
  #else
    typedef uint8_t bf_cell;
  #endif
#elif BF_CELL_BITS == 16
  #if BF_CELL_SIGNED
    typedef int16_t  bf_cell;
  #else
    typedef uint16_t bf_cell;
  #endif
#elif BF_CELL_BITS == 32
  #if BF_CELL_SIGNED
    typedef int32_t  bf_cell;
  #else
    typedef uint32_t bf_cell;
  #endif
#elif BF_CELL_BITS == 64
  #if BF_CELL_SIGNED
    typedef int64_t  bf_cell;
  #else
    typedef uint64_t bf_cell;
  #endif
#else
  #error "Unsupported BF_CELL_BITS (use 8, 16, 32, or 64)"
#endif

// IR argument width for bf_op.buf (NOT a tape cell).
#ifndef BF_OP_BUF_BITS
#define BF_OP_BUF_BITS 16
#endif

#if BF_OP_BUF_BITS == 8
  typedef int8_t bf_op_buf_t;
#elif BF_OP_BUF_BITS == 16
  typedef int16_t bf_op_buf_t;
#elif BF_OP_BUF_BITS == 32
  typedef int32_t bf_op_buf_t;
#else
  #error "Unsupported BF_OP_BUF_BITS (use 8, 16, or 32)"
#endif

typedef int16_t bf_off_t;

// Profiling build (-DBF_PROFILE=1, or `make prof`): counts executions
// per IR op (iterations for loop-carrying ops) and dumps a dynamic op
// histogram plus the hottest loop sites to stderr after the run.
// Costs a counter increment per dispatch, so it is off by default.
#ifndef BF_PROFILE
#define BF_PROFILE 0
#endif

// Threaded (computed-goto) dispatch: on by default for GCC/Clang, which
// support labels-as-values. MSVC and others fall back to switch dispatch.
// Override with -DBF_USE_CGOTO=0/1.
#ifndef BF_USE_CGOTO
  #if defined(__GNUC__)
    #define BF_USE_CGOTO 1
  #else
    #define BF_USE_CGOTO 0
  #endif
#endif

typedef int (*bf_putcharProc)(void* data, int ch);
typedef int (*bf_getcharProc)(void* data);

// -----------------------------
// VM structures
// -----------------------------
typedef struct bf_VM_help { int v; } bf_VM_help;

typedef struct bf_VM {
    int pc, sp;

    bf_cell*    tape;
    int         tapeLen;
    char*       prog;
    int         progLen;
    bf_VM_help* progHelper;

    bf_getcharProc  getcp;
    void*           getdata;
    bf_putcharProc  putcp;
    void*           putdata;

    void*   prog_op;
    int     progLen_op;

#if BF_PROFILE
    unsigned long long* prof;   // per-IR-op execution counts
#endif
} bf_VM;

// -----------------------------
// BF tokens
// -----------------------------
enum {
    bf_MEMDEFAULT = 1024,
    bf_MAXCELLS   = 65536,

    bf_GT     = '>',
    bf_LT     = '<',
    bf_PLUS   = '+',
    bf_MINUS  = '-',
    bf_PERIOD = '.',
    bf_COMMA  = ',',
    bf_OPEN   = '[',
    bf_CLOSE  = ']',
    bf_EOP    = 'e',
};

// -----------------------------
// IR
// -----------------------------
// The op set, defined once and expanded everywhere it is needed:
// the enum here, both dispatch tables and the label array in
// bffsree.c, and the printable names in main.c. Order is the enum
// order; add ops here and give them an _op_* body in bffsree.c.
#define BF_OP_LIST(X) \
    X(NOOP) X(VAL) X(PUT) X(GET) X(FWD) X(REW) X(PTR_S) X(MUL_MUL) \
    X(VAL_MZ) X(VAL_MUL) X(VAL_ZERO) X(MZSCAN) X(VALSCAN) X(LOOPRUN) \
    X(NAVLOOP) X(EOP)

enum ebfo_CMD {
#define X(n) bfo_##n,
    BF_OP_LIST(X)
#undef X
    bfo_Total
};

typedef struct bf_op {
    uint8_t     cmd;
    bf_op_buf_t buf;   // IR argument: loop-inline delta OR target offset
    bf_off_t    off;   // pointer delta after op
    int32_t     val;   // jump distance, immediate value, multiplier
} bf_op;

// -----------------------------
// Helpers/macros
// -----------------------------
#define BF_TAPE_PAD 65536
#define _myfree(a)            do{ if(a){ free(a); (a)=0; } }while(0)
#define _myabs(a)             (((a)<0)?-(a):(a))
#define _myresize(a,b,i)      do{ if((i)>(b)){ (b)=((b)?(b)*2:64); if((i)>(b)) (b)=(i); (a)=(a)?realloc((a),(b)*sizeof(*(a))):malloc((b)*sizeof(*(a))); } }while(0)
#define _mybounds(a,b)        ((unsigned long)(a)>=(unsigned long)(b))

// -----------------------------
// VM API (header-only like original)
// -----------------------------
static int bf_putc(void* f, int c) { (void)f; return putchar(c); }
static int bf_getc(void* f)        { return f ? getc((FILE*)f) : getchar(); }

static int bf_VM_alloc(bf_VM* bp) {
    memset(bp, 0, sizeof(*bp));
    bp->getcp = bf_getc;
    bp->putcp = bf_putc;
    return 0;
}

static int bf_VM_free(bf_VM* bp) {
    _myfree(bp->prog);
    if (bp->tape) { free(bp->tape - BF_TAPE_PAD); bp->tape = 0; }
    _myfree(bp->prog_op);
    _myfree(bp->progHelper);
#if BF_PROFILE
    _myfree(bp->prof);
#endif
    return 0;
}

static int bf_VM_tape(bf_VM* bp, int len) {
    if (len) {
        if (bp->tape && bp->tapeLen == len) return 0;
        {   // padded allocation: permanently-zero sentinel zones
            bf_cell* base = (bf_cell*)calloc((size_t)len + 2u * BF_TAPE_PAD, sizeof(bf_cell));
            if (!base) return -1;
            if (bp->tape) free(bp->tape - BF_TAPE_PAD);
            bp->tape = base + BF_TAPE_PAD;
        }
        bp->tapeLen = len;
    } else {
        if (bp->tape) { free(bp->tape - BF_TAPE_PAD); bp->tape = 0; }
        bp->tapeLen = 0;
    }
    return 0;
}

// -----------------------------
// Public API
// -----------------------------
int  bffsree_Main(int argc, char* argv[]);
int  bffsree_Eval(bf_VM* vm, char* inp, int icount);
void bffsree_Print(bf_VM* vm, char* inp, int lang);
#if BF_PROFILE
void bffsree_ProfileReport(bf_VM* vm);
#endif

int  bf_Optimize(void** bfoptr, char* chars, int proglen, int printMetrics);

#endif // _BF_SREE_H_

