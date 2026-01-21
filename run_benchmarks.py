#!/usr/bin/env python3
"""
run_benchmarks.py - Cross-platform benchmark runner for bfsree
Works on Linux, macOS, and Windows
"""

import subprocess
import sys
import os
import time
import platform

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BENCH_DIR = os.path.join(SCRIPT_DIR, "BFBench-1.4")

# Determine executable name based on platform
if platform.system() == "Windows":
    BFSREE = os.path.join(SCRIPT_DIR, "bfsree.exe")
else:
    BFSREE = os.path.join(SCRIPT_DIR, "bfsree")

# ANSI colors (disabled on Windows unless using Windows Terminal)
USE_COLOR = sys.stdout.isatty() and (platform.system() != "Windows" or "WT_SESSION" in os.environ)
GREEN = "\033[0;32m" if USE_COLOR else ""
RED = "\033[0;31m" if USE_COLOR else ""
YELLOW = "\033[1;33m" if USE_COLOR else ""
NC = "\033[0m" if USE_COLOR else ""

def build_if_needed(force=False):
    """Build bfsree if executable doesn't exist or force is True"""
    if force or not os.path.exists(BFSREE):
        print("Building bfsree...")
        if platform.system() == "Windows":
            # Try make first, fall back to direct gcc
            try:
                subprocess.run(["make", "release"], cwd=SCRIPT_DIR, check=True)
            except FileNotFoundError:
                subprocess.run(["gcc", "-Wall", "-O3", "-o", "bfsree.exe", "main.c"], 
                             cwd=SCRIPT_DIR, check=True)
        else:
            subprocess.run(["make", "release"], cwd=SCRIPT_DIR, check=True)
        print()

def run_benchmark(name, bfile, input_data="", expected_output=None, expected_file=None):
    """Run a single benchmark and return (elapsed_time, passed)"""
    print(f"{name:25}", end="", flush=True)
    
    bf_path = os.path.join(BENCH_DIR, bfile)
    
    start = time.perf_counter()
    try:
        # Use binary mode for subprocess to handle all outputs
        result = subprocess.run(
            [BFSREE, bf_path],
            input=input_data.encode() if input_data else None,
            capture_output=True,
            timeout=300
        )
        output_bytes = result.stdout
    except subprocess.TimeoutExpired:
        elapsed = time.perf_counter() - start
        print(f"{elapsed:8.3f}s  [{RED}TIMEOUT{NC}]")
        return elapsed, False
    except Exception as e:
        elapsed = time.perf_counter() - start
        print(f"{elapsed:8.3f}s  [{RED}ERROR{NC}]")
        return elapsed, False
    
    elapsed = time.perf_counter() - start
    
    # Try to decode as text, filter // lines
    try:
        output = output_bytes.decode('utf-8', errors='replace')
        output_lines = [line for line in output.split('\n') if not line.startswith('//')]
        output = '\n'.join(output_lines)
        is_text = True
    except:
        output = output_bytes
        is_text = False
    
    # Determine expected output
    expected = None
    if expected_output is not None:
        expected = expected_output
    elif expected_file is not None:
        exp_path = os.path.join(BENCH_DIR, expected_file)
        # Try binary read first, then text
        with open(exp_path, 'rb') as f:
            expected_bytes = f.read()
        # Normalize CRLF to LF
        expected_bytes = expected_bytes.replace(b'\r\n', b'\n').replace(b'\r', b'\n')
        try:
            expected = expected_bytes.decode('utf-8')
        except:
            expected = expected_bytes
            is_text = False
    
    # Compare (ignore trailing whitespace/newlines)
    if expected is not None:
        if is_text and isinstance(expected, str):
            output_stripped = output.rstrip()
            expected_stripped = expected.rstrip()
            passed = output_stripped == expected_stripped
        else:
            # Binary comparison - strip trailing newlines
            out_bytes = output_bytes.rstrip(b'\n\r')
            exp_bytes = expected_bytes.rstrip(b'\n\r') if isinstance(expected, bytes) else expected.encode().rstrip(b'\n\r')
            passed = out_bytes == exp_bytes
        status = f"{GREEN}PASS{NC}" if passed else f"{RED}FAIL{NC}"
    else:
        passed = True
        status = f"{YELLOW}DONE{NC}"
    
    print(f"{elapsed:8.3f}s  [{status}]")
    return elapsed, passed

def main():
    force_build = "-b" in sys.argv or "--build" in sys.argv
    
    print("==============================================")
    print("         bfsree Benchmark Suite")
    print("==============================================")
    print()
    
    build_if_needed(force_build)
    
    if not os.path.exists(BFSREE):
        print(f"{RED}Error: bfsree executable not found{NC}")
        sys.exit(1)
    
    print("Running benchmarks...")
    print("----------------------------------------------")
    print(f"{'Test':25} {'Time':>9}  Status")
    print("----------------------------------------------")
    
    benchmarks = [
        ("Mandelbrot", "mandelbrot.b", "", None, "mandelbrot.out"),
        ("Factoring", "factor.b", "123456789123456789\n", 
         "123456789123456789: 3 3 7 11 13 19 3607 3803 52579\n", None),
        ("Long Run", "long.b", "", None, "long.out"),
        ("Golden Ratio", "golden.b", "", "1.618033988749894848204586834365638117\n", None),
        ("Hanoi", "hanoi.b", "", None, "hanoi.out"),
        ("99 Bottles of Beer", "beer.b", "", None, "beer.out"),
        ("Simple Benchmark", "bench.b", "", "OK\n", None),
    ]
    
    total_time = 0
    all_passed = True
    
    for name, bfile, input_data, expected_output, expected_file in benchmarks:
        elapsed, passed = run_benchmark(name, bfile, input_data, expected_output, expected_file)
        total_time += elapsed
        if not passed:
            all_passed = False
    
    print("----------------------------------------------")
    print(f"Total time: {total_time:.3f}s")
    print("Benchmarks complete!")
    
    sys.exit(0 if all_passed else 1)

if __name__ == "__main__":
    main()
