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

// Reconstruct walking LOOPRUN bodies as affine maps over the cell ring
// (new[i] = bias[i] + Σ c[i][j]*old[j]). ./bffsree -c prints the maps.
// Small compiled trees (1-3 stores) get a straight-line evaluator
// picked by shape — compile once, bind the window, eval many times.
// Not an AST walk and not a coefficient-loop interpreter.
#ifndef BF_AFFINE
#define BF_AFFINE 1
#endif
#ifndef BF_AFFINE_APPLY
#define BF_AFFINE_APPLY 1
#endif

enum {
    BF_AFF_NONE = 0,
    BF_AFF_S1   = 1,  /* 1 store, <=3 terms */
    BF_AFF_S2Z  = 2,  /* 2 stores: one constant, one 1-2 term dest */
    BF_AFF_S2   = 3,  /* 2 stores, each <=3 terms */
    BF_AFF_S3   = 4   /* 3 stores, each <=3 terms */
};

#if BF_AFFINE
#define BF_AFFINE_MAX_STORE 24
#define BF_AFFINE_MAX_TERM  64
#define BF_AFFINE_MAX_SRC   24
#endif

// Nest: a loop with no I/O whose body runs inside one helper call
// instead of one Eval dispatch per op. Covers the scan-carrying loops
// LOOPRUN cannot take. Two runners:
//   templates — whole-body straight-line C for op-kind signatures
//     that dominate the corpus, parameters hoisted into locals once
//     per entry (the LOOPRUN helpers, one level up). Measured win.
//   generic — the body cut into segments (affine block bound to a
//     shape evaluator, scan, block helper, child nest) and run by a
//     small switch. Measured *slower* than Eval's computed goto:
//     interpreting a map descriptor once per iteration costs more
//     than dispatching the 2-3 trivial ops it replaces. Off by
//     default (BF_NEST_GENERIC=0); loops without a template stay in
//     Eval. -DBF_NEST=0 disables nests entirely.
#ifndef BF_NEST
#define BF_NEST 1
#endif
#ifndef BF_NEST_GENERIC
#define BF_NEST_GENERIC 0
#endif
// Generic runner only: bind arithmetic runs to the affine shape
// evaluator (1) or leave them as op ranges for the switch walker (0).
#ifndef BF_NEST_AFF
#define BF_NEST_AFF 1
#endif
#define BF_NEST_MAX_SEG 16

enum {
    BF_SEG_AFF = 1,   /* affine block, straight-line by kind */
    BF_SEG_OPS,       /* arithmetic op range [a, b) — switch walk */
    BF_SEG_PTRS,      /* stride scan: a = stride, then off */
    BF_SEG_MZSCAN,    /* block op at header+a, helper applies its REW */
    BF_SEG_VALSCAN,
    BF_SEG_LOOPRUN,   /* LOOPRUN, generic walker; variants bound at compile time: */
    BF_SEG_LOOPRUN_MZ_MUL_MZ_VAL,
    BF_SEG_LOOPRUN_FRAME9,   /* MZ_MUL_MZ_VAL whose working set is one 9-cell record */
    BF_SEG_LOOPRUN_VAL_MUL_MZ_MZ,
    BF_SEG_LOOPRUN_AFF_S1,
    BF_SEG_LOOPRUN_AFF_S2Z,
    BF_SEG_LOOPRUN_AFF_S2,
    BF_SEG_LOOPRUN_AFF_S3,
    BF_SEG_NEST,      /* child nest at header+a */
    BF_SEG_FWD,       /* nested loop the compiler could not nest */
    BF_SEG_Total
};

// Profiling build (-DBF_PROFILE=1, or `make prof`): counts executions
// per IR op (iterations for loop-carrying ops) and dumps a dynamic op
// histogram plus the hottest loop sites to stderr after the run.
// Costs a counter increment per dispatch, so it is off by default.
#ifndef BF_PROFILE
#define BF_PROFILE 0
#endif

// Portable 64-bit word-at-a-time stride-3 scans for 8-bit cells.
#ifndef BF_WORD_SCAN
#define BF_WORD_SCAN 1
#endif

// Debug-only shadow check: compare every word-scan result with scalar.
#ifndef BF_WORD_SCAN_GUARD
#define BF_WORD_SCAN_GUARD 0
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
    X(VAL_MZ) X(VAL_MUL) X(VAL_ZERO) X(ZFILL) X(MZSCAN) X(VALSCAN) X(LOOPRUN) X(NEST) X(EOP)

enum ebfo_CMD {
#define X(n) bfo_##n,
    BF_OP_LIST(X)
#undef X
    bfo_Total
};

typedef struct bf_op {
    uint8_t     cmd;
    uint8_t     sub;   // LOOPRUN: helper variant (BF_SEG_LOOPRUN*), decoded once
    bf_op_buf_t buf;   // IR argument: loop-inline delta OR target offset
    bf_off_t    off;   // pointer delta after op
    uint16_t    aux;   // 1-based affine map id (LOOPRUN) / nest id (NEST)
    int32_t     val;   // jump distance, immediate value, multiplier
} bf_op;

// One compiled body segment. Op references (a, b) are relative to
// the nest's header op so the runner needs only the header pointer.
typedef struct bf_seg {
    uint8_t  kind;    /* BF_SEG_* */
    int16_t  off;     /* PTRS: pointer delta after the scan */
    int32_t  a, b;
    const struct bf_affine *aff;   /* AFF: bound map (resolved after compile) */
} bf_seg;

// Body templates: whole-body straight-line runners for op-kind
// signatures that dominate the corpus. Parameters (vals/offs/bufs)
// are read from the parameter block into locals once per entry.
// Signatures are structural strings over the body: V VAL, M VAL_MZ,
// X VAL_MUL, Z VAL_ZERO, Q MUL_MUL, N NOOP, S PTR_S, R LOOPRUN,
// m MZSCAN, v VALSCAN, {..} nested loop.
enum {
    BF_TMPL_NONE = 0,
    BF_TMPL_ZV_R_MV,        /* "ZVRMV"        */
    BF_TMPL_VM_VRS_S_m_MV,  /* "VM{VRS}SmMV"  */
    BF_TMPL_S_V_F_S_V,      /* "SVFSV"        */
    BF_TMPL_S_V_S_V         /* "SVSV"         */
};

int bf_nest_signature(const bf_op *bfo, int s, char *buf, int buflen);

typedef struct bf_nest {
    uint8_t nseg;
    uint8_t tmpl;     /* BF_TMPL_*; 0 = generic segment runner */
    bf_seg  seg[BF_NEST_MAX_SEG];
} bf_nest;

const bf_nest *bf_nest_get(unsigned id);
int            bf_nest_count(void);

// Which internal helper a LOOPRUN header selects. Pure IR inspection;
// shared by the Eval arm and the nest compiler so the choice is made
// once per site rather than once per entry.
int bf_looprun_variant(const bf_op *L);

// Same for MZSCAN (stored in the header op's sub).
enum {
    BF_MZ_GENERIC = 0,  /* parameterized walk from the op fields */
    BF_MZ_COPY9_FROM1,  /* lane +1 into the next 9-cell record */
    BF_MZ_COPY9_FROM2,  /* lane +2 */
    BF_MZ_SLIDE9,       /* any lane, dest 9 */
    BF_MZ_COPY          /* buf == 0, val == 1: plain move */
};
int bf_mzscan_variant(const bf_op *M);

#if BF_AFFINE
// Sparse affine map for one LOOPRUN body, relative to the pointer at
// the start of the body (after the LOOPRUN header's buf/off).
// Lane-packed form of a small map (kind != NONE), built once at
// classify time by bf_affine_pack. Every store of the map is one lane
// of a 64-bit word: acc = bias + old0*cvec[0] + ... + old3*cvec[3]
// evaluates all of them with four multiply-adds, and store s takes
// the low cell bits of lane s. Unused sources read p[0] with cvec 0.
typedef struct bf_aff_bound {
    int16_t  hop;
    int16_t  soff[4];
    int16_t  dst[4];
    uint64_t cvec[4];
    uint64_t bias;
} bf_aff_bound;

typedef struct bf_affine {
    int16_t hop;
    int16_t src_off[BF_AFFINE_MAX_SRC];
    uint8_t nsrc, nstore, nterm;
    uint8_t kind;   /* BF_AFF_S1 / S2Z / S2 / S3, or NONE */
    struct {
        int16_t dst;
        bf_cell bias;
        uint8_t t0, nt;
    } store[BF_AFFINE_MAX_STORE];
    struct {
        uint8_t src;
        bf_cell k;
    } term[BF_AFFINE_MAX_TERM];
    bf_aff_bound bound;
} bf_affine;

const bf_affine *bf_affine_get(unsigned id);
int              bf_affine_count(void);
int              bf_affine_from_body(const bf_op *body, int n, bf_affine *out);
int              bf_affine_classify(bf_affine *m);
int              bf_affine_pack(bf_affine *m);
int              bf_affine_format(const bf_affine *m, char *buf, int buflen);
const char      *bf_affine_kind_name(int kind);
#endif

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

