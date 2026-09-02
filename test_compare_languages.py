#!/usr/bin/env python3

import unittest

import compare_languages as compare


class CompareLanguagesTests(unittest.TestCase):
    def test_output_normalization_preserves_significant_spaces(self):
        self.assertEqual(compare.normalize_output(b"one \r\n"), b"one ")
        self.assertEqual(compare.normalize_output(b"one\r\ntwo\n"), b"one\ntwo")

    def test_summary_uses_median_and_python_baseline(self):
        runtimes = (
            compare.Runtime("python", "py", ("python3",)),
            compare.Runtime("bffsree", "b", ("bffsree",)),
        )
        workload = compare.WORKLOADS[0]
        samples = {
            "fib": {
                "python": [2.0, 4.0, 3.0],
                "bffsree": [6.0, 8.0, 7.0],
            }
        }

        summary = compare.summarize(samples, runtimes, (workload,))

        self.assertEqual(summary["baseline"], "python")
        self.assertEqual(summary["medians_seconds"]["fib"]["python"], 3.0)
        self.assertEqual(
            summary["throughput_relative_to_baseline"]["fib"]["python"], 1.0
        )
        self.assertAlmostEqual(
            summary["throughput_relative_to_baseline"]["fib"]["bffsree"],
            3.0 / 7.0,
        )


if __name__ == "__main__":
    unittest.main()
