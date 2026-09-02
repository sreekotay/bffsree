#!/usr/bin/env python3
"""Compare matched workloads across Plush, Python, Ruby, Lua, and bffsree."""

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
PROGRAMS = ROOT / "comparison" / "programs"
BUILD_DIR = ROOT / ".bench-build"
BFFSREE = BUILD_DIR / ("bffsree-fast.exe" if os.name == "nt" else "bffsree-fast")


@dataclass(frozen=True)
class Workload:
    name: str
    expected: bytes | None = None
    expected_file: str | None = None

    def expected_bytes(self) -> bytes:
        if self.expected_file:
            return (PROGRAMS / self.expected_file).read_bytes()
        assert self.expected is not None
        return self.expected


WORKLOADS = (
    Workload("fib", expected=b"121393\n"),
    Workload("binary_tree", expected=b"3264000\n"),
    Workload("mandelbrot", expected_file="mandelbrot.out"),
)


@dataclass(frozen=True)
class Runtime:
    name: str
    suffix: str
    command: tuple[str, ...]

    def invocation(self, workload: Workload) -> list[str]:
        return [*self.command, str(PROGRAMS / f"{workload.name}.{self.suffix}")]


def command_from_env(variable: str, fallback: str) -> tuple[str, ...]:
    return tuple(shlex.split(os.environ.get(variable, fallback)))


def configured_runtimes() -> tuple[Runtime, ...]:
    return (
        Runtime("plush", "psh", command_from_env("PLUSH", "plush")),
        Runtime("python", "py", command_from_env("PYTHON", sys.executable)),
        Runtime("ruby", "rb", command_from_env("RUBY", "ruby")),
        Runtime("lua", "lua", command_from_env("LUA", "lua")),
        Runtime("bffsree", "b", (str(BFFSREE),)),
    )


def normalize_output(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").rstrip(b"\n")


def build_bffsree() -> list[str]:
    BUILD_DIR.mkdir(exist_ok=True)
    compiler = shlex.split(os.environ.get("CC", "cc"))
    command = compiler + [
        "-Wall",
        "-Wextra",
        "-O3",
        "-DNDEBUG",
        "-DBF_FAST=1",
        "-march=native",
        "-DBF_CELL_BITS=8",
        "-DBF_CELL_SIGNED=0",
        "-DBF_OP_BUF_BITS=16",
        "-o",
        str(BFFSREE),
        str(ROOT / "main.c"),
    ]
    subprocess.run(command, cwd=ROOT, check=True)
    return command


def executable_available(runtime: Runtime) -> bool:
    executable = runtime.command[0]
    return Path(executable).is_file() or shutil.which(executable) is not None


def select_runtimes(names: Sequence[str] | None, require_all: bool) -> tuple[Runtime, ...]:
    runtimes = configured_runtimes()
    requested = set(names or (runtime.name for runtime in runtimes))
    selected = []
    missing = []
    for runtime in runtimes:
        if runtime.name not in requested:
            continue
        if executable_available(runtime):
            selected.append(runtime)
        else:
            missing.append(runtime.name)
    if missing and (require_all or names):
        raise RuntimeError(
            "missing requested runtime(s): "
            + ", ".join(missing)
            + "; set PLUSH, RUBY, LUA, or PYTHON to override commands"
        )
    if missing:
        print("Skipping unavailable runtime(s): " + ", ".join(missing), file=sys.stderr)
    if not selected:
        raise RuntimeError("no benchmark runtimes are available")
    return tuple(selected)


def run_once(
    runtime: Runtime, workload: Workload, timeout: float
) -> tuple[float, bytes]:
    start = time.perf_counter()
    result = subprocess.run(
        runtime.invocation(workload),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
    )
    elapsed = time.perf_counter() - start
    if result.returncode != 0:
        stderr = result.stderr.decode(errors="replace").strip()
        raise RuntimeError(
            f"{runtime.name}/{workload.name} exited {result.returncode}: {stderr}"
        )
    return elapsed, result.stdout


def verify_output(runtime: Runtime, workload: Workload, output: bytes) -> None:
    expected = normalize_output(workload.expected_bytes())
    actual = normalize_output(output)
    if actual != expected:
        raise RuntimeError(
            f"{runtime.name}/{workload.name} output mismatch "
            f"({len(actual)} bytes, expected {len(expected)})"
        )


def collect_samples(
    runtimes: Sequence[Runtime],
    workloads: Sequence[Workload],
    runs: int,
    warmups: int,
    timeout: float,
    seed: int,
) -> dict[str, dict[str, list[float]]]:
    samples = {
        workload.name: {runtime.name: [] for runtime in runtimes}
        for workload in workloads
    }
    print(f"Validating outputs with {warmups} warmup(s)...", flush=True)
    for workload in workloads:
        for runtime in runtimes:
            attempts = max(1, warmups)
            for _ in range(attempts):
                _, output = run_once(runtime, workload, timeout)
                verify_output(runtime, workload, output)

    rng = random.Random(seed)
    print(f"Collecting {runs} interleaved run(s)...", flush=True)
    for round_index in range(runs):
        jobs = [
            (runtime, workload)
            for workload in workloads
            for runtime in runtimes
        ]
        rng.shuffle(jobs)
        for runtime, workload in jobs:
            elapsed, output = run_once(runtime, workload, timeout)
            verify_output(runtime, workload, output)
            samples[workload.name][runtime.name].append(elapsed)
        print(f"  round {round_index + 1}/{runs}", flush=True)
    return samples


def summarize(
    samples: dict[str, dict[str, list[float]]],
    runtimes: Sequence[Runtime],
    workloads: Sequence[Workload],
) -> dict[str, object]:
    medians = {
        workload.name: {
            runtime.name: statistics.median(samples[workload.name][runtime.name])
            for runtime in runtimes
        }
        for workload in workloads
    }
    baseline = "python" if any(runtime.name == "python" for runtime in runtimes) else runtimes[0].name
    relative = {
        workload.name: {
            runtime.name: medians[workload.name][baseline]
            / medians[workload.name][runtime.name]
            for runtime in runtimes
        }
        for workload in workloads
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
    relative = summary["throughput_relative_to_baseline"]
    baseline = summary["baseline"]
    assert isinstance(medians, dict) and isinstance(relative, dict)

    print("\nMedian process wall time (seconds; lower is better)")
    print(f"{'workload':>15}", end="")
    for runtime in runtimes:
        print(f"  {runtime.name:>12}", end="")
    print()
    for workload in workloads:
        print(f"{workload.name:>15}", end="")
        for runtime in runtimes:
            print(f"  {medians[workload.name][runtime.name]:12.6f}", end="")
        print()

    print(f"\nThroughput relative to {baseline} (higher is better)")
    print(f"{'workload':>15}", end="")
    for runtime in runtimes:
        print(f"  {runtime.name:>12}", end="")
    print()
    for workload in workloads:
        print(f"{workload.name:>15}", end="")
        for runtime in runtimes:
            print(f"  {relative[workload.name][runtime.name]:11.3f}x", end="")
        print()


def runtime_versions(runtimes: Sequence[Runtime]) -> dict[str, str]:
    versions = {}
    for runtime in runtimes:
        if runtime.name == "bffsree":
            versions[runtime.name] = "local bffsree-fast build"
            continue
        for flag in ("--version", "-v"):
            result = subprocess.run(
                [*runtime.command, flag],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=10,
                check=False,
            )
            text = result.stdout.strip().splitlines()
            if text:
                versions[runtime.name] = text[0]
                break
        else:
            versions[runtime.name] = "unknown"
    return versions


def compiler_version() -> str:
    result = subprocess.run(
        [*shlex.split(os.environ.get("CC", "cc")), "--version"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=10,
        check=False,
    )
    lines = result.stdout.strip().splitlines()
    return lines[0] if lines else "unknown"


def parse_args() -> argparse.Namespace:
    runtime_names = [runtime.name for runtime in configured_runtimes()]
    workload_names = [workload.name for workload in WORKLOADS]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-n", "--runs", type=int, default=11)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--seed", type=int, default=20260902)
    parser.add_argument("--runtimes", nargs="+", choices=runtime_names)
    parser.add_argument("--workloads", nargs="+", choices=workload_names)
    parser.add_argument(
        "--require-all",
        action="store_true",
        help="fail instead of skipping unavailable language runtimes",
    )
    parser.add_argument("--json", type=Path, help="write raw samples and metadata")
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be at least 1")
    if args.warmups < 0:
        parser.error("--warmups cannot be negative")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def main() -> int:
    args = parse_args()
    try:
        build_command = build_bffsree()
        runtimes = select_runtimes(args.runtimes, args.require_all)
        selected_names = set(args.workloads or (workload.name for workload in WORKLOADS))
        workloads = tuple(
            workload for workload in WORKLOADS if workload.name in selected_names
        )
        samples = collect_samples(
            runtimes,
            workloads,
            args.runs,
            args.warmups,
            args.timeout,
            args.seed,
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
                "seed": args.seed,
                "timeout_seconds": args.timeout,
            },
            "workload_parameters": {
                "fib": {"n": 26, "article_n": 38, "algorithm": "naive recursion"},
                "binary_tree": {
                    "depth": 7,
                    "traversals": 100,
                    "article_depth": 14,
                    "article_traversals": 2000,
                    "representation": "implicit perfect tree",
                },
                "mandelbrot": {
                    "width": 65,
                    "height": 41,
                    "max_iterations": 20,
                    "arithmetic": "signed 4-bit fixed point in wrapping bytes",
                },
            },
            "system": {
                "platform": platform.platform(),
                "python": platform.python_version(),
                "compiler": compiler_version(),
                "runtime_versions": runtime_versions(runtimes),
            },
            "bffsree_build_command": build_command,
            "samples_seconds": samples,
            **summary,
        }
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n")
        print(f"\nWrote {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
