import unittest

import compare_bf_runtimes as compare


class ComparisonTests(unittest.TestCase):
    def test_normalize_output_handles_line_endings_and_final_newlines(self):
        self.assertEqual(compare.normalize_output(b"one\r\ntwo\r\n"), b"one\ntwo")

    def test_summarize_uses_fast_word_runtime_as_baseline(self):
        runtimes = (
            compare.Runtime("bffsree-fast-word64", ("fast",)),
            compare.Runtime("other", ("other",)),
        )
        workload = compare.Workload("tiny", compare.ROOT / "tiny.b", expected=b"")
        samples = {
            "tiny": {
                "bffsree-fast-word64": [2.0, 1.0, 3.0],
                "other": [4.0, 6.0, 5.0],
            }
        }

        summary = compare.summarize(samples, runtimes, (workload,))

        self.assertEqual(
            summary["medians_seconds"]["tiny"],
            {
                "bffsree-fast-word64": 2.0,
                "other": 5.0,
            },
        )
        self.assertEqual(
            summary["throughput_relative_to_baseline"]["tiny"]["other"], 0.4
        )

    def test_summarize_preserves_missing_timeout_results(self):
        runtimes = (compare.Runtime("bffsree-fast-word64", ("fast",)),)
        workload = compare.Workload("tiny", compare.ROOT / "tiny.b", expected=b"")
        summary = compare.summarize(
            {"tiny": {"bffsree-fast-word64": []}}, runtimes, (workload,)
        )
        self.assertIsNone(summary["medians_seconds"]["tiny"]["bffsree-fast-word64"])


if __name__ == "__main__":
    unittest.main()
