#!/usr/bin/env python3
"""Benchmark a Brainfuck corpus across bffsree, bf-cpp, and Tritium."""

from __future__ import annotations

import argparse
import json
import os
import platform
import random
import shlex
import shutil
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parent
BUILD_DIR = ROOT / ".bench-build"
RUNTIME_DIR = BUILD_DIR / "runtimes"
BF_CPP_REV = "a7c99b1c98d56534c77edb7c1fe47aae9975d00e"
TRITIUM_REV = "525d346c006ea30dfc847ae3b32ed44d44fa9925"


@dataclass(frozen=True)
class Workload:
    name: str
    program: Path
    expected: bytes | None = None
    expected_file: Path | None = None
    input_data: bytes = b""

    def expected_bytes(self) -> bytes:
        if self.expected_file:
            return self.expected_file.read_bytes()
        assert self.expected is not None
        return self.expected


BF_BENCH = ROOT / "BFBench-1.4"
GENERATED = ROOT / "comparison" / "programs"
WORKLOADS = (
    Workload("bfbench-mandelbrot", BF_BENCH / "mandelbrot.b",
             expected_file=BF_BENCH / "mandelbrot.result"),
    Workload("bfbench-factor", BF_BENCH / "factor.b",
             expected=b"123456789123456789: 3 3 7 11 13 19 3607 3803 52579\n",
             input_data=b"123456789123456789\n"),
    Workload("bfbench-long", BF_BENCH / "long.b",
             expected_file=BF_BENCH / "long.out"),
    Workload("bfbench-golden", BF_BENCH / "golden.b",
             expected=b"1.618033988749894848204586834365638117\n"),
    Workload("bfbench-hanoi", BF_BENCH / "hanoi.b",
             expected_file=BF_BENCH / "hanoi.out"),
    Workload("bfbench-beer", BF_BENCH / "beer.b",
             expected_file=BF_BENCH / "beer.out"),
    Workload("bfbench-simple", BF_BENCH / "bench.b", expected=b"OK\n"),
    Workload("go2bf-fib", GENERATED / "fib.b", expected=b"121393\n"),
    Workload("go2bf-binary-tree", GENERATED / "binary_tree.b",
             expected=b"3264000\n"),
    Workload("go2bf-mandelbrot", GENERATED / "mandelbrot.b",
             expected_file=GENERATED / "mandelbrot.out"),
)


@dataclass(frozen=True)
class Runtime:
    name: str
    command: tuple[str, ...]
    trailing_args: tuple[str, ...] = ()

    def invocation(self, workload: Workload) -> list[str]:
        return [*self.command, str(workload.program), *self.trailing_args]


def env_command(name: str, fallback: Path) -> tuple[str, ...]:
    value = os.environ.get(name)
    return tuple(shlex.split(value)) if value else (str(fallback),)


def configured_runtimes() -> tuple[Runtime, ...]:
    suffix = ".exe" if os.name == "nt" else ""
    tritium = RUNTIME_DIR / "tritium" / "tritium" / "bfi.out"
    return (
        Runtime("bffsree-checked", (str(BUILD_DIR / f"bffsree-checked{suffix}"),)),
        Runtime("bffsree-fast-word64", (str(BUILD_DIR / f"bffsree-fast-word64{suffix}"),)),
        Runtime("bffsree-reference", (str(BUILD_DIR / f"bffsree-reference{suffix}"),)),
        Runtime("bf-cpp", env_command(
            "BF_CPP", RUNTIME_DIR / "bf-cpp" / "build" / "src" / "standalone" / "brainfuck"
        )),
        Runtime("tritium-interpreter", env_command("TRITIUM", tritium), ("-r",)),
        Runtime("tritium-jit", env_command("TRITIUM", tritium)),
    )


def run_checked(command: Sequence[str], cwd: Path = ROOT) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def checkout_source(url: str, revision: str, destination: Path) -> None:
    if not destination.exists():
        run_checked(("git", "clone", url, str(destination)))
    run_checked(("git", "fetch", "origin", revision), destination)
    run_checked(("git", "checkout", "--detach", revision), destination)


def prepare_external_runtimes() -> None:
    RUNTIME_DIR.mkdir(parents=True, exist_ok=True)
    bf_cpp = RUNTIME_DIR / "bf-cpp"
    tritium = RUNTIME_DIR / "tritium"
    checkout_source("https://github.com/jumbub/bf-cpp.git", BF_CPP_REV, bf_cpp)
    checkout_source("https://github.com/rdebath/Brainfuck.git", TRITIUM_REV, tritium)
    run_checked((
        "cmake", "-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_CXX_STANDARD=23",
    ), bf_cpp)
    run_checked(("cmake", "--build", "build", "--target", "standalone", "-j2"), bf_cpp)
    run_checked(("make", "-C", "tritium", "-j2"), tritium)


def build_bffsree() -> dict[str, list[str]]:
    BUILD_DIR.mkdir(exist_ok=True)
    compiler = shlex.split(os.environ.get("CC", "cc"))
    common = compiler + [
        "-Wall", "-Wextra", "-O3", "-DNDEBUG",
        "-DBF_CELL_BITS=8", "-DBF_CELL_SIGNED=0", "-DBF_OP_BUF_BITS=16",
    ]
    commands = {
        "bffsree-checked": common + [
            "-o", str(BUILD_DIR / "bffsree-checked"), str(ROOT / "main.c")
        ],
        "bffsree-fast-word64": common + [
            "-DBF_FAST=1", "-o", str(BUILD_DIR / "bffsree-fast-word64"),
            str(ROOT / "main.c"),
        ],
        "bffsree-reference": common + [
            "-D_refInterp=1", "-o", str(BUILD_DIR / "bffsree-reference"),
            str(ROOT / "main.c"),
        ],
    }
    for command in commands.values():
        run_checked(command)
    return commands


def executable_available(runtime: Runtime) -> bool:
    executable = runtime.command[0]
    return Path(executable).is_file() or shutil.which(executable) is not None


def select_runtimes(names: Sequence[str] | None, require_all: bool) -> tuple[Runtime, ...]:
    requested = set(names or (runtime.name for runtime in configured_runtimes()))
    selected, missing = [], []
    for runtime in configured_runtimes():
        if runtime.name not in requested:
            continue
        (selected if executable_available(runtime) else missing).append(runtime)
    if missing and (require_all or names):
        variables = " (set BF_CPP or TRITIUM to override executable paths)"
        raise RuntimeError("missing runtime(s): " + ", ".join(r.name for r in missing) + variables)
    if missing:
        print("Skipping unavailable runtime(s): " + ", ".join(r.name for r in missing),
              file=sys.stderr)
    if not selected:
        raise RuntimeError("no benchmark runtimes are available")
    return tuple(selected)


def normalize_output(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").rstrip(b"\n")


def run_once(runtime: Runtime, workload: Workload, timeout: float) -> tuple[float, bytes]:
    start = time.perf_counter()
    result = subprocess.run(
        runtime.invocation(workload),
        input=workload.input_data or None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
    )
    elapsed = time.perf_counter() - start
    if result.returncode:
        detail = result.stderr.decode(errors="replace").strip()
        raise RuntimeError(
            f"{runtime.name}/{workload.name} exited {result.returncode}: {detail}"
        )
    return elapsed, result.stdout


def verify_output(runtime: Runtime, workload: Workload, output: bytes) -> None:
    actual = normalize_output(output)
    expected = normalize_output(workload.expected_bytes())
    if actual != expected:
        raise RuntimeError(
            f"{runtime.name}/{workload.name} output mismatch "
            f"({len(actual)} bytes; expected {len(expected)})"
        )


def collect_samples(
    runtimes: Sequence[Runtime],
    workloads: Sequence[Workload],
    runs: int,
    warmups: int,
    timeout: float,
    seed: int,
) -> tuple[dict[str, dict[str, list[float]]], dict[str, dict[str, str]]]:
    samples = {
        workload.name: {runtime.name: [] for runtime in runtimes}
        for workload in workloads
    }
    skipped: dict[str, dict[str, str]] = {workload.name: {} for workload in workloads}
    active: set[tuple[str, str]] = set()
    print(f"Validating outputs with {warmups} warmup(s)...", flush=True)
    for workload in workloads:
        for runtime in runtimes:
            try:
                for _ in range(max(1, warmups)):
                    _, output = run_once(runtime, workload, timeout)
                    verify_output(runtime, workload, output)
                active.add((runtime.name, workload.name))
            except subprocess.TimeoutExpired:
                skipped[workload.name][runtime.name] = f"validation timeout after {timeout:g}s"

    jobs = [
        (runtime, workload)
        for workload in workloads
        for runtime in runtimes
        if (runtime.name, workload.name) in active
    ]
    rng = random.Random(seed)
    print(f"Collecting {runs} interleaved run(s)...", flush=True)
    for round_index in range(runs):
        rng.shuffle(jobs)
        for runtime, workload in jobs:
            try:
                elapsed, output = run_once(runtime, workload, timeout)
                verify_output(runtime, workload, output)
                samples[workload.name][runtime.name].append(elapsed)
            except subprocess.TimeoutExpired:
                skipped[workload.name][runtime.name] = f"timed run timeout after {timeout:g}s"
        print(f"  round {round_index + 1}/{runs}", flush=True)
    return samples, skipped


def summarize(
    samples: dict[str, dict[str, list[float]]],
    runtimes: Sequence[Runtime],
    workloads: Sequence[Workload],
) -> dict[str, object]:
    medians: dict[str, dict[str, float | None]] = {}
    relative: dict[str, dict[str, float | None]] = {}
    baseline = "bffsree-fast-word64"
    for workload in workloads:
        row = {
            runtime.name: (
                statistics.median(samples[workload.name][runtime.name])
                if samples[workload.name][runtime.name] else None
            )
            for runtime in runtimes
        }
        medians[workload.name] = row
        base = row.get(baseline)
        relative[workload.name] = {
            runtime.name: (
                base / value if base is not None and value is not None else None
            )
            for runtime in runtimes
            for value in (row[runtime.name],)
        }
    return {
        "baseline": baseline,
        "medians_seconds": medians,
        "throughput_relative_to_baseline": relative,
    }


def print_summary(
    summary: dict[str, object],
    runtimes: Sequence[Runtime],
    workloads: Sequence[Workload],
) -> None:
    medians = summary["medians_seconds"]
    assert isinstance(medians, dict)
    print("\nMedian process wall time (seconds; lower is better)")
    print(f"{'workload':>23}", *(f"{r.name:>23}" for r in runtimes))
    for workload in workloads:
        values = [
            "timeout".rjust(23) if medians[workload.name][runtime.name] is None
            else f"{medians[workload.name][runtime.name]:23.6f}"
            for runtime in runtimes
        ]
        print(f"{workload.name:>23}", *values)


def compiler_version() -> str:
    command = [*shlex.split(os.environ.get("CC", "cc")), "--version"]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    lines = result.stdout.strip().splitlines()
    return lines[0] if lines else "unknown"


def parse_args() -> argparse.Namespace:
    runtime_names = [runtime.name for runtime in configured_runtimes()]
    workload_names = [workload.name for workload in WORKLOADS]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-n", "--runs", type=int, default=7)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--seed", type=int, default=20260905)
    parser.add_argument("--runtimes", nargs="+", choices=runtime_names)
    parser.add_argument("--workloads", nargs="+", choices=workload_names)
    parser.add_argument("--prepare", action="store_true",
                        help="clone pinned bf-cpp and Tritium sources and build them")
    parser.add_argument("--require-all", action="store_true")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    if args.runs < 1 or args.warmups < 0 or args.timeout <= 0:
        parser.error("runs must be positive, warmups nonnegative, and timeout positive")
    return args


def main() -> int:
    args = parse_args()
    try:
        if args.prepare:
            prepare_external_runtimes()
        build_commands = build_bffsree()
        runtimes = select_runtimes(args.runtimes, args.require_all)
        selected = set(args.workloads or (workload.name for workload in WORKLOADS))
        workloads = tuple(w for w in WORKLOADS if w.name in selected)
        samples, skipped = collect_samples(
            runtimes, workloads, args.runs, args.warmups, args.timeout, args.seed
        )
        summary = summarize(samples, runtimes, workloads)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print_summary(summary, runtimes, workloads)
    if args.json:
        report = {
            "methodology": {
                "runs": args.runs,
                "warmups": args.warmups,
                "interleaved": True,
                "statistic": "median process wall time",
                "timeout_seconds": args.timeout,
                "seed": args.seed,
            },
            "source_revisions": {
                "bf-cpp": BF_CPP_REV,
                "tritium": TRITIUM_REV,
                "go2bf": "6001981a6834e6cc908888d0089bc18947ea16cb",
            },
            "system": {
                "platform": platform.platform(),
                "python": platform.python_version(),
                "compiler": compiler_version(),
            },
            "bffsree_build_commands": build_commands,
            "samples_seconds": samples,
            "skipped": skipped,
            **summary,
        }
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n")
        print(f"\nWrote {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
