#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""
Run abckit_gtests (or any gtest binary) with retry-on-failure for flaky tests.

A test that fails the first run is re-run up to MAX_RETRIES times. If any re-run
passes, the test is treated as FLAKY-PASS (reported PASSED with a [flaky] note).
Real failures (all re-runs fail) are reported FAILED. Use this wrapper for suites
that contain tests with unavoidable non-determinism (e.g. concurrency races whose
assertion is the always-true safety property but whose exact branch hit is
best-effort).

Usage:
  run_gtests_with_retry.py <gtest-binary> [extra gtest args...]
  MAX_RETRIES=5 run_gtests_with_retry.py <gtest-binary> --gtest_filter='*ThreadSafety*'

Exit code: 0 if every test passes (immediately or on retry), 1 if any test fails
all retries. The JSON output is parsed to discover failed test names; the wrapper
re-runs only those (via --gtest_filter) to keep retry cost low.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile


def run_gtests(binary, args, json_path=None):
    cmd = [binary] + list(args)
    if json_path is not None:
        cmd.append(f"--gtest_output=json:{json_path}")
    proc = subprocess.run(cmd, capture_output=True, text=True)
    return proc


def parse_failed_tests(json_path):
    """Return a list of 'Suite.Test' names that failed in the JSON report."""
    with open(json_path) as f:
        data = json.load(f)
    failed = []
    for suite in data.get("testsuites", []):
        suite_name = suite.get("name", "")
        for test in suite.get("testsuite", []):
            # A test failed if it has a non-empty failures/errors array OR
            # result == "FAILED" (gtest versions differ on the field).
            has_failures = bool(test.get("failures")) or bool(test.get("errors"))
            result = test.get("result", "")
            status = test.get("status", "")
            if has_failures or result == "FAILED" or status == "FAILED":
                failed.append(f"{suite_name}.{test.get('name', '')}")
    return failed


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("binary", help="path to the gtest binary")
    parser.add_argument("gtest_args", nargs=argparse.REMAINDER,
                        help="extra args forwarded to the gtest binary")
    args = parser.parse_args()
    # Strip a leading '--' that argparse REMAINDER may leave.
    gtest_args = [a for a in args.gtest_args if a != "--"]

    max_retries = int(os.environ.get("MAX_RETRIES", "3"))

    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
        json_path = f.name
    try:
        # First run: all tests.
        proc = run_gtests(args.binary, gtest_args, json_path=json_path)
        first_pass = proc.returncode == 0
        if first_pass:
            failed = []
        else:
            try:
                failed = parse_failed_tests(json_path)
            except (OSError, json.JSONDecodeError) as exc:
                # gtest crashed before writing a complete JSON report (e.g. SIGSEGV) — can't
                # discover individual failed test names to retry. Report + fail.
                print(f"[retry-wrapper] gtest run exited {proc.returncode} with no usable JSON "
                      f"report ({exc}); cannot retry individual tests")
                return 1

        if first_pass:
            print(f"[retry-wrapper] all tests passed on first run")
            return 0

        print(f"[retry-wrapper] first run had {len(failed)} failure(s); retrying each up to "
              f"{max_retries} time(s)...")
        flaky_pass = []
        real_fail = []
        for name in failed:
            # Re-run just this test up to max_retries times.
            passed = False
            for attempt in range(1, max_retries + 1):
                r = run_gtests(args.binary,
                               list(gtest_args) + [f"--gtest_filter={name}"])
                if r.returncode == 0:
                    passed = True
                    print(f"[retry-wrapper] FLAKY-PASS: {name} (passed on retry {attempt})")
                    break
            if passed:
                flaky_pass.append(name)
            else:
                print(f"[retry-wrapper] FAILED: {name} (failed all {max_retries} retries)")
                real_fail.append(name)

        print(f"[retry-wrapper] summary: {len(flaky_pass)} flaky-pass, "
              f"{len(real_fail)} real-fail")
        return 0 if not real_fail else 1
    finally:
        os.unlink(json_path)


if __name__ == "__main__":
    sys.exit(main())
