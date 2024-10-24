#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright (c) 2024 Huawei Device Co., Ltd.
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
import multiprocessing
import os
import stat
import sys
import re
from functools import lru_cache
import random
from typing import List, Dict, Any, Tuple, TextIO, Final
import logging

import stress_common
from stress_test import Test, Result
from stress import StressJSTest, EXCLUDED_TESTS
from stress_common import SCRIPT_DIR, collect_from

FAIL_LIST_PATH = os.path.join(SCRIPT_DIR, 'fail_list_full.json')
OPTIONS_LIST_PATH = os.path.join(SCRIPT_DIR, 'options_list.json')

logging.basicConfig(format='%(message)s', level=logging.DEBUG)


class Descriptor:
    def __init__(self, input_file: str) -> None:
        self.input_file = input_file
        self.header = re.compile(
            r"/\*---(?P<header>.+)---\*/", re.DOTALL)
        self.includes = re.compile(r"includes:\s+\[(?P<includes>.+)]")
        self.content = self.get_content()

    def get_content(self) -> str:
        with open(self.input_file, "r", encoding="utf-8") as file_pointer:
            input_str = file_pointer.read()
        return input_str

    def get_header(self) -> str:
        header_comment = self.header.search(self.content)
        return header_comment.group(0) if header_comment else ""

    def parse_descriptor(self) -> Dict[str, Any]:
        header = self.get_header()
        result: Dict[str, Any] = {}

        if len(header) == 0:
            return result

        includes = []
        match = self.includes.search(header)
        includes += [incl.strip() for incl in match.group("includes").split(",")] if match else []

        result["includes"] = includes
        return result


@lru_cache(maxsize=100000)
def get_harness_code(path: str = None) -> str:
    return read_harness_code(path)


def read_harness_code(path: str = None) -> str:
    ajs = os.path.abspath(os.path.join(path, 'harness', 'assert.js'))
    sjs = os.path.abspath(os.path.join(path, 'harness', 'sta.js'))

    with open(ajs, 'r') as f:
        assert_js = f.read()
    with open(sjs, 'r') as f:
        sta_js = f.read()
    return assert_js + '\n' + sta_js + '\n'


@lru_cache(maxsize=10000)
def read_import(src: str) -> str:
    with open(src, 'r') as f:
        code = f.read()
    return code


class StressJSFull(StressJSTest):

    def __init__(self, repeats: int, options_list, args) -> None:
        super().__init__(options_list, args)
        self.repeats = 3 if repeats is None else repeats
        self.build_dir = args.build_dir
        logging.debug('Repeats: %s with timeout: %s', self.repeats, self.timeout)

    def compile_single(self, src: str) -> Tuple[str, str, int]:
        self.prepare_single(src)
        src = src.replace("[", "_").replace("]", "_")
        cr = super().compile_single(src + '.mjs')
        return src, cr[1], cr[2]

    def run_single(self, test: Test) -> Result:
        test.abc = test.abc.replace("[", "_").replace("]", "_")
        stress_abc = test.abc + '.stress.abc'
        r1p = Test(test.source + ".mjs", test.abc)
        r2p = Test(test.source + ".mjs", stress_abc)

        test_result_one = self.run_js_test_single(r1p)  # Run test once

        cmd = [stress_common.STRESS, '--input-file', test.abc, '--output-file', stress_abc]
        if test.source in self.options_list:
            cmd += [self.options_list[test.source][0], self.options_list[test.source][1]]
        result: stress_common.ExecRes = stress_common.stress_exec(cmd, allow_error=True, print_command=False, env={"OUT_DIR": self.build_dir})
        if result.returncode != 0:
            error = stress_common.parse_stdout(result.returncode, result.stdout)
            return Result(test.source, error)
        # Stress test passed

        if test_result_one.result == -1:  # First attempt JS Test failed with timeout. This bypass next test
            return Result(test.source, "0")

        test_result_two = self.run_js_test_single(r2p, self.repeats)  # Run test with defined repeats

        if test_result_two.result == 0:
            return Result(test.source, "0")

        if test_result_one.result != test_result_two.result:
            return Result(test.source, f'JS Test result changed. Was {test_result_one.result}, now {test_result_two.result}')

        return Result(test.source, "0")

    def prepare_single(self, src: str) -> None:
        flags = os.O_WRONLY | os.O_CREAT
        mode = stat.S_IWUSR | stat.S_IRUSR
        with os.fdopen(os.open(src.replace("[", "_").replace("]", "_") + ".mjs", flags, mode), 'w') as out:
            with os.fdopen(os.open(src, os.O_RDONLY), 'r') as sf:
                out.write(get_harness_code(self.js_dir))
                for include in Descriptor(src).parse_descriptor().get('includes', []):
                    imp = os.path.abspath(os.path.join(self.js_dir, 'harness', include))
                    out.write(read_import(imp) + "\n")
                out.write(sf.read())

    def collect(self) -> List[str]:
        tests: List[str] = []
        sp = os.path.join(self.js_dir, 'test')
        tests.extend(collect_from(sp, lambda name: name.endswith('.js') and not name.startswith('.')))
        sp = os.path.join(self.js_dir, 'implementation-contributed')
        tests.extend(collect_from(sp, lambda name: name.endswith('.js') and not name.startswith('.')))
        random.shuffle(tests)

        logging.debug('Total tests: %s', len(tests))
        for excluded in EXCLUDED_TESTS:
            tests = list(filter(lambda name: excluded not in name, tests))
        logging.debug('Tests after exclude: %s', len(tests))

        return tests

    def build(self) -> List[Test]:
        tests: List[str] = self.collect()

        logging.debug('Running compiler...')
        compiled_tests: List[Test] = []
        counter = 0
        with multiprocessing.pool.ThreadPool(stress_common.NPROC) as pool:
            for js_path, abc_path, retcode in pool.imap(self.compile_single, tests, chunksize=20):
                if retcode == 0:
                    compiled_tests.append(Test(js_path, abc_path))
                counter += 1
                sys.stdout.write(f'{counter}/{len(tests)}    \r')  # CC-OFF(G.LOG.02) progress print

        logging.debug('Tests successfully compiled: %s', len(compiled_tests))
        return compiled_tests


def main():
    logging.debug('ABCKit stress test')
    args = stress_common.get_args()
    test = StressJSFull(args.repeats, stress_common.get_options_list(OPTIONS_LIST_PATH), args)
    test.prepare()

    tests: List[Test] = test.build()
    results = test.run(tests)

    fail_list = stress_common.get_fail_list(results)

    if args.update_fail_list:
        stress_common.update_fail_list(FAIL_LIST_PATH, fail_list)
        logging.debug('Failures/Total: {}/%s', len(fail_list), len(results))
        return 0

    if not stress_common.check_regression_errors(FAIL_LIST_PATH, fail_list):
        return 1

    if not stress_common.check_fail_list(FAIL_LIST_PATH, fail_list):
        logging.debug('Failures/Total: %s/%s', len(fail_list), len(results))

    logging.debug('ABCKit: no regressions')
    logging.debug('Failures/Total: %s/%s', len(fail_list), len(results))
    return 0


if __name__ == '__main__':
    sys.exit(main())
