#!/bin/bash
# =====================================================================
# run_benchmarks.sh - Run all brainfuck benchmarks with bffsree
# =====================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BENCH_DIR="$SCRIPT_DIR/BFBench-1.4"
BFFSREE="$SCRIPT_DIR/bffsree"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=============================================="
echo "         bffsree Benchmark Suite"
echo "=============================================="
echo

# Build if needed
if [ ! -f "$BFFSREE" ] || [ "$1" = "-b" ] || [ "$1" = "--build" ]; then
    echo "Building bffsree..."
    make -C "$SCRIPT_DIR" release
    echo
fi

# Check if executable exists
if [ ! -f "$BFFSREE" ]; then
    echo -e "${RED}Error: bffsree executable not found${NC}"
    exit 1
fi

# Function to run a single benchmark
run_benchmark() {
    local name="$1"
    local bfile="$2"
    local input="$3"
    local expected_out="$4"
    local timeout_sec="${5:-60}"

    printf "%-25s" "$name"
    
    local start_time=$(python3 -c 'import time; print(time.time())')
    
    local tmpfile=$(mktemp)
    if [ -n "$input" ]; then
        echo -n "$input" | "$BFFSREE" "$BENCH_DIR/$bfile" 2>/dev/null > "$tmpfile" || true
    else
        "$BFFSREE" "$BENCH_DIR/$bfile" 2>/dev/null > "$tmpfile" || true
    fi
    
    local end_time=$(python3 -c 'import time; print(time.time())')
    local elapsed
    elapsed=$(START_TIME="$start_time" END_TIME="$end_time" python3 - <<'PY'
import os
start = float(os.environ["START_TIME"])
end = float(os.environ["END_TIME"])
print(f"{end - start:.3f}")
PY
)
    
    # Check output if expected output provided (ignore trailing whitespace)
    local status=""
    if [ -n "$expected_out" ]; then
        local expected_file=$(mktemp)
        echo -n "$expected_out" > "$expected_file"
        if diff -qwB "$tmpfile" "$expected_file" > /dev/null 2>&1; then
            status="${GREEN}PASS${NC}"
        else
            status="${RED}FAIL${NC}"
        fi
        rm -f "$expected_file"
    else
        status="${YELLOW}DONE${NC}"
    fi
    
    rm -f "$tmpfile"
    printf "%8ss  [%b]\n" "$elapsed" "$status"
}

# Function to run benchmark with output file
run_benchmark_file() {
    local name="$1"
    local bfile="$2"
    local outfile="$3"
    local timeout_sec="${4:-60}"

    printf "%-25s" "$name"
    
    local start_time=$(python3 -c 'import time; print(time.time())')
    
    local tmpfile=$(mktemp)
    local expectedfile=$(mktemp)
    "$BFFSREE" "$BENCH_DIR/$bfile" 2>/dev/null > "$tmpfile" || true
    LC_ALL=C tr -d '\r' < "$BENCH_DIR/$outfile" > "$expectedfile"
    
    local end_time=$(python3 -c 'import time; print(time.time())')
    local elapsed
    elapsed=$(START_TIME="$start_time" END_TIME="$end_time" python3 - <<'PY'
import os
start = float(os.environ["START_TIME"])
end = float(os.environ["END_TIME"])
print(f"{end - start:.3f}")
PY
)
    
    # Check output against expected file (ignore trailing whitespace)
    local status=""
    if diff -qwB "$tmpfile" "$expectedfile" > /dev/null 2>&1; then
        status="${GREEN}PASS${NC}"
    else
        status="${RED}FAIL${NC}"
    fi
    
    rm -f "$tmpfile" "$expectedfile"
    printf "%8ss  [%b]\n" "$elapsed" "$status"
}

echo "Running benchmarks..."
echo "----------------------------------------------"
printf "%-25s %9s  %s\n" "Test" "Time" "Status"
echo "----------------------------------------------"

# Run all benchmarks
run_benchmark_file "Mandelbrot" "mandelbrot.b" "mandelbrot.result" 200
run_benchmark "Factoring" "factor.b" "123456789123456789
" "123456789123456789: 3 3 7 11 13 19 3607 3803 52579
" 50
run_benchmark_file "Long Run" "long.b" "long.out" 200
run_benchmark "Golden Ratio" "golden.b" "" "1.618033988749894848204586834365638117" 10
run_benchmark_file "Hanoi" "hanoi.b" "hanoi.out" 80
run_benchmark_file "99 Bottles of Beer" "beer.b" "beer.out" 10
run_benchmark "Simple Benchmark" "bench.b" "" "OK
" 5

echo "----------------------------------------------"
echo "Benchmarks complete!"
