#!/usr/bin/env python3
# -*- coding: utf-8 -*-

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
#

from __future__ import annotations

import logging
import json
from typing import TextIO, Tuple, Optional, List, Dict, Any
import re
from enum import Enum
import os

from runner.logger import Log

_LOGGER = logging.getLogger('runner.astchecker.util_astchecker')


class UtilASTChecker:

    skip_options = {'SkipErrors' : False, 'SkipWarnings' : False}

    class _TestType(Enum):
        NODE = 'Node'
        ERROR = 'Error'
        WARNING = 'Warning'

    class _TestCase:
        """
        Class for storing test case parsed from test file
        """

        def __init__(self, name: Optional[str], test_type: UtilASTChecker._TestType,
                     checks: dict, line: int, col: int, error_file: str = '') -> None:
            self.name = name
            self.line = line
            self.col = col
            self.test_type = test_type
            self.checks = checks
            self.error_file = error_file

        def __repr__(self) -> str:
            return f'TestCase({self.name}, {self.line}:{self.col}, {self.test_type}, {self.checks}, {self.error_file})'

        def __eq__(self, other: Any) -> bool:
            return bool(self.name == other.name and self.line == other.line and self.col == other.col
                    and self.test_type == other.test_type and self.checks == other.checks
                    and self.error_file == other.error_file)

        def __hash__(self) -> int:
            return hash(self.__repr__())

    class TestCasesList:
        """
        Class for storing test cases parsed from one test file
        """

        def __init__(self, tests_list: set[UtilASTChecker._TestCase]):
            self.tests_list = tests_list
            has_error_tests = False
            has_warning_tests = False
            for test in tests_list:
                if test.test_type == UtilASTChecker._TestType.ERROR:
                    has_error_tests = True
                if test.test_type == UtilASTChecker._TestType.WARNING:
                    has_warning_tests = True
            self.has_error_tests = has_error_tests
            self.has_warning_tests = has_warning_tests
            self.skip_errors = False
            self.skip_warnings = False

    def __init__(self) -> None:
        self.regex = re.compile(r'/\*\s*@@\s*(?P<pattern>.*?)\s*\*/', re.DOTALL)
        self.reset_skips()

    @staticmethod
    def get_match_location(match: re.Match, start: bool = False) -> Tuple[int, int]:
        """
        Returns match location in file: line and column (counting from 1)
        """
        match_idx = match.start() if start else match.end()
        line_start_index = match.string[:match_idx].rfind('\n') + 1
        col = match_idx - line_start_index + 1
        line = match.string[:match_idx].count('\n') + 1
        return line, col

    @staticmethod
    def check_properties(node: dict, properties: dict) -> bool:
        """
        Checks if `node` contains all key:value pairs specified in `properties` dict argument
        """
        for key, value in properties.items():
            if node.get(key) != value:
                return False
        return True

    @staticmethod
    def run_error_test(test_file: str, test: _TestCase, actual_errors: set) -> bool:
        file_name = os.path.basename(test_file) if test.error_file == '' else test.error_file
        expected_error = f'{test.checks["error"]}', f'[{file_name}:{test.line}:{test.col}]'
        if expected_error in actual_errors:
            actual_errors.remove(expected_error)
            return True
        Log.all(_LOGGER, f'No Expected error {expected_error}')
        return False

    def reset_skips(self) -> None:
        self.skip_options['SkipErrors'] = False
        self.skip_options['SkipWarnings'] = False

    def create_test_case(self, name: Optional[str], pattern_type: UtilASTChecker._TestType,
                         pattern: str, line: int, col: int, error_file: str = '') -> UtilASTChecker._TestCase:
        pattern_parsed = {'error': pattern}
        if pattern_type == UtilASTChecker._TestType.NODE:
            try:
                pattern_parsed = json.loads(pattern)
            except json.JSONDecodeError as ex:
                Log.exception_and_raise(_LOGGER, f'TestCase: {name}.\nThrows JSON error: {ex}.\nJSON data: {pattern}')
        return UtilASTChecker._TestCase(name, pattern_type, pattern_parsed, line, col, error_file=error_file)

    def check_skip_error(self) -> bool:
        return self.skip_options["SkipErrors"]

    def check_skip_warning(self) -> bool:
        return self.skip_options["SkipWarnings"]

    def parse_define_statement(self, match: re.Match[str],
                               link_defs_map: Dict[str, Tuple[UtilASTChecker._TestType, str]],
                               link_sources_map: Dict[str, re.Match[str]]) -> Optional[UtilASTChecker._TestCase]:
        """
        Parses `@<id> <pattern-type> <pattern>`
        """
        match_str = re.sub(r'\s+', ' ', match.group('pattern'))[1:].strip()
        sep1 = match_str.find(' ')
        sep2 = match_str.find(' ', sep1 + 1)
        if sep1 == -1 or sep2 == -1:
            Log.exception_and_raise(_LOGGER, 'Wrong definition format: expected '
                                             f'`/* @@@ <id> <pattern-type> <pattern> */`, got /* @@@ {match_str} */')
        name = match_str[:sep1]
        pattern_type = UtilASTChecker._TestType(match_str[sep1 + 1:sep2])
        pattern = match_str[sep2 + 1:]

        if name in link_defs_map:
            line, col = self.get_match_location(match)
            Log.exception_and_raise(_LOGGER, f'Link {name} (at location {line}:{col}) is already defined')

        if name in link_sources_map:
            match = link_sources_map[name]
            del link_sources_map[name]
            line, col = self.get_match_location(match)
            return self.create_test_case(name, pattern_type, pattern, line, col)

        link_defs_map[name] = (pattern_type, pattern)
        return None

    def parse_match_statement(self, match: re.Match[str],
                              link_defs_map: Dict[str, Tuple[UtilASTChecker._TestType, str]],
                              link_sources_map: Dict[str, re.Match[str]]) -> Optional[UtilASTChecker._TestCase]:
        """
        Parses `<pattern-type> <pattern>` and `<id>`
        """
        str_match = match.group('pattern')
        sep = str_match.find(' ')
        if sep == -1:
            # parse `<id>`
            name = str_match
            if any(not char.isalnum() and char != '_' for char in name):
                Log.exception_and_raise(_LOGGER, f'Bad `<id>` value, expected value from `[a-zA-Z0-9_]+`, got {name}')

            if name in link_sources_map:
                line, col = self.get_match_location(match)
                Log.exception_and_raise(_LOGGER, f'Link {name} (at location {line}:{col}) is already defined')

            if name in link_defs_map:
                pattern_type, pattern = link_defs_map[name]
                del link_defs_map[name]
                line, col = self.get_match_location(match)
                return self.create_test_case(name, pattern_type, pattern, line, col)

            link_sources_map[name] = match
            return None

        # parse `<pattern-type> <pattern>`
        pattern_type = UtilASTChecker._TestType(str_match[:sep])
        pattern = str_match[sep + 1:]
        line, col = self.get_match_location(match)
        return self.create_test_case(None, pattern_type, pattern, line, col)

    def parse_skip_statement(self, match: re.Match[str]) -> None:
        '''
        Parses `# <skip-option>*`
        '''
        match_str = re.sub(r'\s+', ' ', match.group('pattern'))[1:].strip()
        pattern = r"(\w+)\s*=\s*(\w+)"
        matches = re.findall(pattern, match_str)
        for opt, val in matches:
            value = val.lower()
            if opt not in self.skip_options or value not in ['true', 'false']:
                Log.exception_and_raise(_LOGGER, 'Wrong match_at_location format: expected '
                                                 f'`/* @@# <skip-option> */`, got /* @@? {match_str} */')
            self.skip_options[opt] = value == 'true'

    def parse_match_at_loc_statement(self, match: re.Match[str]) -> UtilASTChecker._TestCase:
        """
        Parses `? <line>:<col> <pattern-type> <pattern>`
        and  `? <file-name>:<line>:<col> <pattern-type> <pattern>`
        """
        match_str = re.sub(r'\s+', ' ', match.group('pattern'))[1:].strip()
        sep1 = match_str.find(' ')
        sep2 = match_str.find(' ', sep1 + 1)
        if sep1 == -1 or sep2 == -1:
            Log.exception_and_raise(_LOGGER, 'Wrong match_at_location format: expected '
                                             '`/* @@? <line>:<col> <pattern-type> <pattern> */`, '
                                             f'got /* @@? {match_str} */')
        location = match_str[:sep1]
        loc_sep1 = location.find(':')
        loc_sep2 = location.find(':', loc_sep1 + 1)
        if loc_sep1 == -1:
            Log.exception_and_raise(_LOGGER, 'Wrong match_at_location format: expected '
                                             '`/* @@? <line>:<col> <pattern-type> <pattern> */`, '
                                             f'got /* @@? {match_str} */')
        line_str = location[:loc_sep1] if loc_sep2 == -1 else location[loc_sep1 + 1:loc_sep2]
        col_str = location[loc_sep1 + 1:] if loc_sep2 == -1 else location[loc_sep2 + 1:]
        error_file = '' if loc_sep2 == -1 else location[:loc_sep1]
        if loc_sep2 != -1:
            pass
        if not line_str.isdigit():
            Log.exception_and_raise(_LOGGER, f'Expected line number, got {line_str}')
        if not col_str.isdigit():
            Log.exception_and_raise(_LOGGER, f'Expected column number, got {col_str}')

        line, col = int(line_str), int(col_str)
        pattern_type = UtilASTChecker._TestType(match_str[sep1 + 1:sep2])
        pattern = match_str[sep2 + 1:]
        return self.create_test_case(None, pattern_type, pattern, line, col, error_file)

    def parse_tests(self, file: TextIO) -> UtilASTChecker.TestCasesList:
        """
        Takes .ets file with tests and parses them into a list of TestCases.
        """
        self.reset_skips()
        test_text = file.read()
        link_defs_map: Dict[str, Tuple[UtilASTChecker._TestType, str]] = {}
        link_sources_map: Dict[str, re.Match[str]] = {}
        test_cases = set()
        matches = list(re.finditer(self.regex, test_text))
        for match in matches:
            pattern = match.group('pattern')
            if pattern.startswith('@'):
                test = self.parse_define_statement(match, link_defs_map, link_sources_map)
            elif pattern.startswith('?'):
                test = self.parse_match_at_loc_statement(match)
            elif pattern.startswith('#'):
                test = None
                self.parse_skip_statement(match)
            else:
                test = self.parse_match_statement(match, link_defs_map, link_sources_map)
            if test is not None:
                test_cases.add(test)

        if len(link_defs_map) or len(link_sources_map):
            Log.exception_and_raise(_LOGGER, 'link defined twice')

        test_case_list = UtilASTChecker.TestCasesList(test_cases)
        test_case_list.skip_errors = self.check_skip_error()
        test_case_list.skip_warnings = self.check_skip_warning()
        return test_case_list

    def find_nodes_by_start_location(self, root: dict, line: int, col: int) -> List[dict]:
        """
        Finds all descendants of `root` with location starting at `loc`
        """
        nodes = []
        if 'loc' in root and root['loc']['start']['line'] == line and \
                root['loc']['start']['column'] == col:
            nodes.append(root)

        for child in root.values():
            if isinstance(child, dict):
                nodes.extend(self.find_nodes_by_start_location(child, line, col))
            if isinstance(child, list):
                for item in child:
                    nodes.extend(self.find_nodes_by_start_location(item, line, col))
        return nodes

    def run_node_test(self, test: _TestCase, ast: dict) -> bool:
        nodes_by_loc = self.find_nodes_by_start_location(ast, test.line, test.col)
        test_passed = False
        for node in nodes_by_loc:
            if self.check_properties(node, test.checks):
                test_passed = True
                break
        return test_passed

    def get_actual_errors(self, error: str) -> set:
        actual_errors = set()
        for error_str in error.splitlines():
            if error_str.strip():
                error_text, error_loc = error_str.rsplit(' ', 1)
                actual_errors.add((error_text.strip(), error_loc))
        return actual_errors

    def run_tests(self, test_file: str, test_cases: TestCasesList, ast: dict, error: str = '') -> bool:
        """
        Takes AST and runs tests on it, returns True if all tests passed
        """
        Log.all(_LOGGER, f'Running {len(test_cases.tests_list)} tests...')
        failed_tests = 0
        actual_errors = self.get_actual_errors(error)
        node_test_passed = True
        error_test_passed = True
        warning_test_passed = True
        tests_set = set(test_cases.tests_list)


        for i, test in enumerate(tests_set):
            if test.test_type == UtilASTChecker._TestType.NODE:
                node_test_passed = self.run_node_test(test, ast)
            elif test.test_type == UtilASTChecker._TestType.ERROR:
                error_test_passed = self.run_error_test(test_file, test, actual_errors)
                if test_cases.skip_errors:
                    error_test_passed = True
            elif test.test_type == UtilASTChecker._TestType.WARNING:
                warning_test_passed = self.run_error_test(test_file, test, actual_errors)
                if test_cases.skip_warnings:
                    warning_test_passed = True
            test_name = f'Test {i + 1}' + ('' if test.name is None else f': {test.name}')
            if bool(node_test_passed and error_test_passed and warning_test_passed):
                Log.all(_LOGGER, f'PASS: {test_name}')
            else:
                Log.all(_LOGGER, f'FAIL: {test_name} in {test_file}')
                failed_tests += 1

        for actual_error in actual_errors:
            if actual_error[0].split()[0] == "Warning:" and not self.check_skip_warning():
                Log.all(_LOGGER, f'Unexpected warning {actual_error}')
                failed_tests += 1
            if ((actual_error[0].split()[0] == "TypeError:" or actual_error[0].split()[0] == "SyntaxError:")
                    and not self.check_skip_error()):
                Log.all(_LOGGER, f'Unexpected error {actual_error}')
                failed_tests += 1

        if failed_tests == 0:
            Log.all(_LOGGER, 'All tests passed')
            return True

        Log.all(_LOGGER, f'Failed {failed_tests} tests')
        return False
