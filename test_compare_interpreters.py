#!/usr/bin/env python3
"""Unit tests for the comparison harness (no third-party test runner needed)."""

import unittest

import compare_interpreters as compare


class CompareInterpretersTests(unittest.TestCase):
    def test_output_normalization_only_changes_line_endings(self):
        self.assertEqual(compare.normalized_output(b"one\r\ntwo\r\n"), b"one\ntwo")
        self.assertEqual(compare.normalized_output(b"one \n"), b"one ")

    def test_summary_uses_medians_and_geometric_mean(self):
        variants = compare.VARIANTS
        workloads = compare.WORKLOADS[:2]
        samples = {
            workloads[0].name: {
                "reference": [8.0, 10.0, 9.0],
                "checked": [4.0, 5.0, 4.5],
                "fast": [2.0, 3.0, 2.25],
            },
            workloads[1].name: {
                "reference": [18.0, 20.0, 19.0],
                "checked": [9.0, 10.0, 9.5],
                "fast": [4.0, 5.0, 4.75],
            },
        }

        summary = compare.summarize(samples, variants, workloads)

        self.assertEqual(summary["medians_seconds"]["mandelbrot"]["reference"], 9.0)
        self.assertEqual(summary["speedup_vs_reference"]["mandelbrot"]["fast"], 4.0)
        self.assertAlmostEqual(
            summary["geometric_mean_speedup_vs_reference"]["checked"], 2.0
        )
        self.assertAlmostEqual(
            summary["geometric_mean_speedup_vs_reference"]["fast"], 4.0
        )


if __name__ == "__main__":
    unittest.main()
