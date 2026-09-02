#!/usr/bin/env python3
"""Compare bffsree execution tiers with interleaved, verified workloads."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import random
import shlex
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parent
BENCH_DIR = ROOT / "BFBench-1.4"
BUILD_DIR = ROOT / ".bench-build"


@dataclass(frozen=True)
class Workload:
    name: str
    program: str
    expected_file: str | None = None
    expected: bytes | None = None
    input_file: str | None = None

    def input_bytes(self) -> bytes | None:
        return (BENCH_DIR / self.input_file).read_bytes() if self.input_file else None

    def expected_bytes(self) -> bytes:
        if self.expected_file:
            return (BENCH_DIR / self.expected_file).read_bytes()
        assert self.expected is not None
        return self.expected


WORKLOADS = (
    Workload("mandelbrot", "mandelbrot.b", expected_file="mandelbrot.result"),
    Workload(
        "factor",
        "factor.b",
        input_file="factor.in",
        expected=b"123456789123456789: 3 3 7 11 13 19 3607 3803 52579\n",
    ),
    Workload("long", "long.b", expected_file="long.out"),
    Workload(
        "golden",
        "golden.b",
        expected=b"1.618033988749894848204586834365638117\n",
    ),
    Workload("hanoi", "hanoi.b", expected_file="hanoi.out"),
)


@dataclass(frozen=True)
class Variant:
    name: str
    defines: tuple[str, ...] = ()
    native: bool = False


VARIANTS = (
    Variant("reference", ("_refInterp=1",)),
    Variant("checked"),
    Variant("fast", ("BF_FAST=1",), native=True),
)


def normalized_output(data: bytes) -> bytes:
    """Normalize platform newlines and one optional final line ending."""
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").rstrip(b"\n")


def compiler_command() -> list[str]:
    return shlex.split(os.environ.get("CC", "cc"))


def executable_path(variant: Variant) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    return BUILD_DIR / f"bffsree-{variant.name}{suffix}"


def build_variants(variants: Sequence[Variant]) -> dict[str, list[str]]:
    BUILD_DIR.mkdir(exist_ok=True)
    commands: dict[str, list[str]] = {}
    common = [
        "-Wall",
        "-Wextra",
        "-O3",
        "-DNDEBUG",
        "-DBF_CELL_BITS=8",
        "-DBF_CELL_SIGNED=0",
        "-DBF_OP_BUF_BITS=16",
    ]

    for variant in variants:
        output = executable_path(variant)
        command = compiler_command() + common
        command += [f"-D{define}" for define in variant.defines]
        if variant.native:
            command.append("-march=native")
        command += ["-o", str(output), str(ROOT / "main.c")]
        subprocess.run(command, cwd=ROOT, check=True)
        commands[variant.name] = command
    return commands


def run_once(
    variant: Variant, workload: Workload, timeout: float
) -> tuple[float, bytes]:
    start = time.perf_counter()
    result = subprocess.run(
        [executable_path(variant), BENCH_DIR / workload.program],
        input=workload.input_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
    )
    elapsed = time.perf_counter() - start
    if result.returncode != 0:
        stderr = result.stderr.decode(errors="replace").strip()
        raise RuntimeError(
            f"{variant.name}/{workload.name} exited {result.returncode}: {stderr}"
        )
    return elapsed, result.stdout


def verify_output(variant: Variant, workload: Workload, output: bytes) -> None:
    actual = normalized_output(output)
    expected = normalized_output(workload.expected_bytes())
    if actual != expected:
        raise RuntimeError(
            f"{variant.name}/{workload.name} produced incorrect output "
            f"(expected sha256={hashlib.sha256(expected).hexdigest()}, "
            f"actual sha256={hashlib.sha256(actual).hexdigest()})"
        )


def benchmark(
    variants: Sequence[Variant],
    workloads: Sequence[Workload],
    runs: int,
    warmups: int,
    timeout: float,
    seed: int,
) -> dict[str, dict[str, list[float]]]:
    samples = {
        workload.name: {variant.name: [] for variant in variants}
        for workload in workloads
    }

    print(f"Verifying outputs and running {warmups} warmup(s)...", flush=True)
    for workload in workloads:
        expected_output: bytes | None = None
        for variant in variants:
            for _ in range(warmups):
                _, output = run_once(variant, workload, timeout)
                verify_output(variant, workload, output)
            if warmups == 0:
                _, output = run_once(variant, workload, timeout)
                verify_output(variant, workload, output)
            normalized = normalized_output(output)
            if expected_output is not None and normalized != expected_output:
                raise RuntimeError(
                    f"variants disagree on output for {workload.name}"
                )
            expected_output = normalized

    rng = random.Random(seed)
    print(f"Collecting {runs} interleaved run(s)...", flush=True)
    for run_number in range(runs):
        workload_order = list(workloads)
        rng.shuffle(workload_order)
        for workload in workload_order:
            variant_order = list(variants)
            rng.shuffle(variant_order)
            for variant in variant_order:
                elapsed, output = run_once(variant, workload, timeout)
                verify_output(variant, workload, output)
                samples[workload.name][variant.name].append(elapsed)
        print(f"  round {run_number + 1}/{runs}", flush=True)
    return samples


def summarize(
    samples: dict[str, dict[str, list[float]]],
    variants: Sequence[Variant],
    workloads: Sequence[Workload],
) -> dict[str, object]:
    medians = {
        workload.name: {
            variant.name: statistics.median(samples[workload.name][variant.name])
            for variant in variants
        }
        for workload in workloads
    }
    reference = variants[0].name
    speedups = {
        workload.name: {
            variant.name: medians[workload.name][reference]
            / medians[workload.name][variant.name]
            for variant in variants[1:]
        }
        for workload in workloads
    }
    geometric_means = {
        variant.name: math.prod(
            speedups[workload.name][variant.name] for workload in workloads
        )
        ** (1.0 / len(workloads))
        for variant in variants[1:]
    }
    return {
        "medians_seconds": medians,
        "speedup_vs_reference": speedups,
        "geometric_mean_speedup_vs_reference": geometric_means,
    }


def print_summary(
    summary: dict[str, object],
    variants: Sequence[Variant],
    workloads: Sequence[Workload],
) -> None:
    medians = summary["medians_seconds"]
    speedups = summary["speedup_vs_reference"]
    assert isinstance(medians, dict) and isinstance(speedups, dict)

    headers = ["benchmark"] + [variant.name for variant in variants]
    headers += [f"{variant.name} speedup" for variant in variants[1:]]
    print("\nMedian wall time (seconds), lower is better")
    print("  ".join(f"{header:>17}" for header in headers))
    for workload in workloads:
        row = [f"{workload.name:>17}"]
        row += [
            f"{medians[workload.name][variant.name]:17.6f}" for variant in variants
        ]
        row += [
            f"{speedups[workload.name][variant.name]:16.2f}x"
            for variant in variants[1:]
        ]
        print("  ".join(row))

    geomeans = summary["geometric_mean_speedup_vs_reference"]
    assert isinstance(geomeans, dict)
    print("\nGeometric mean speedup vs reference:")
    for variant in variants[1:]:
        print(f"  {variant.name}: {geomeans[variant.name]:.2f}x")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Build and compare bffsree tiers using verified, interleaved runs "
            "and median timings."
        )
    )
    parser.add_argument("-n", "--runs", type=int, default=11)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument("--seed", type=int, default=20260902)
    parser.add_argument(
        "--benchmarks",
        nargs="+",
        choices=[workload.name for workload in WORKLOADS],
        default=[workload.name for workload in WORKLOADS],
    )
    parser.add_argument("--json", type=Path, help="write samples and metadata")
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
    workloads = tuple(
        workload for workload in WORKLOADS if workload.name in args.benchmarks
    )
    try:
        commands = build_variants(VARIANTS)
        samples = benchmark(
            VARIANTS,
            workloads,
            args.runs,
            args.warmups,
            args.timeout,
            args.seed,
        )
        summary = summarize(samples, VARIANTS, workloads)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print_summary(summary, VARIANTS, workloads)
    if args.json:
        compiler = compiler_command()
        compiler_version = subprocess.run(
            compiler + ["--version"],
            capture_output=True,
            text=True,
            check=False,
        ).stdout.splitlines()[0]
        report = {
            "methodology": {
                "runs": args.runs,
                "warmups": args.warmups,
                "interleaved": True,
                "statistic": "median",
                "seed": args.seed,
                "timeout_seconds": args.timeout,
            },
            "system": {
                "platform": platform.platform(),
                "python": platform.python_version(),
                "compiler": compiler_version,
            },
            "build_commands": commands,
            "samples_seconds": samples,
            **summary,
        }
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n")
        print(f"\nWrote {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
