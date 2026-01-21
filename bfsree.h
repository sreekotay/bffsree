// =====================================================================
// bfsree.h
// =====================================================================
#ifndef _BF_SREE_H_
#define _BF_SREE_H_

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
#else
  #error "Unsupported BF_CELL_BITS (use 8, 16, or 32)"
#endif

#if !BF_CELL_SIGNED && (BF_CELL_BITS == 8 || BF_CELL_BITS == 16 || BF_CELL_BITS == 32)
  #define BF_CELL_MOD_POW2 1
#else
  #define BF_CELL_MOD_POW2 0
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

// Max scan distance when searching for matching REW during optimization.
#ifndef BF_OPT_LOOP_RUNAWAY
#define BF_OPT_LOOP_RUNAWAY 65536
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

    void*   debugProg;
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
    bf_DEBUG  = '#',
    bf_EOP    = 'e',
};

// -----------------------------
// IR
// -----------------------------
enum ebfo_CMD {
    bfo_NOOP = 0,
    bfo_VAL,
    bfo_PUT,
    bfo_GET,
    bfo_FWD,
    bfo_REW,
    bfo_PTR_S,
    bfo_MUL_MUL,
    bfo_VAL_MZ,
    bfo_VAL_MUL,
    bfo_VAL_ZERO,
    bfo_DEBUG,
    bfo_EOP,
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
#define _myfree(a)            do{ if(a){ free(a); (a)=0; } }while(0)
#define _myabs(a)             (((a)<0)?-(a):(a))
#define _myresize(a,b,i)      do{ if((i)>(b)){ (b)=((i)>(b))?(i):((b)?(b)*2:64); (a)=(a)?realloc((a),(b)*sizeof(*(a))):malloc((b)*sizeof(*(a))); } }while(0)
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
    _myfree(bp->tape);
    _myfree(bp->prog_op);
    _myfree(bp->debugProg);
    _myfree(bp->progHelper);
    return 0;
}

static int bf_VM_tape(bf_VM* bp, int len) {
    if (len) {
        if (bp->tape && bp->tapeLen == len) return 0;
        bp->tape = bp->tape ? (bf_cell*)realloc(bp->tape, (size_t)len * sizeof(bf_cell))
                            : (bf_cell*)malloc ((size_t)len * sizeof(bf_cell));
        if (!bp->tape) return -1;
        if (len > bp->tapeLen) {
            memset(bp->tape + bp->tapeLen, 0, (size_t)(len - bp->tapeLen) * sizeof(bf_cell));
        }
        bp->tapeLen = len;
    } else {
        _myfree(bp->tape);
        bp->tapeLen = 0;
    }
    return 0;
}

// -----------------------------
// Public API
// -----------------------------
int  bfsree_Main(int argc, char* argv[]);
int  bfsree_Eval(bf_VM* vm, char* inp, int icount);
void bfsree_Print(bf_VM* vm, char* inp, int lang);

int  bf_Optimize(void** bfoptr, char* chars, int proglen, int printMetrics);

#endif // _BF_SREE_H_

