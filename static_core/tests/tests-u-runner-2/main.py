#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright (c) 2024-2025 Huawei Device Co., Ltd.
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

import os
import sys
import traceback
from datetime import datetime, timedelta
from pathlib import Path
from typing import Dict, Any

import pytz
from dotenv import load_dotenv

from runner.common_exceptions import InvalidConfiguration
from runner.enum_types.verbose_format import VerboseKind
from runner.logger import Log
from runner.options.cli_options import get_args
from runner.options.config import Config
from runner.runner_base import Runner
from runner.suites.runner_standard_flow import RunnerStandardFlow
from runner.utils import pretty_divider, check_obligatory_env


def main() -> None:
    load_environment()

    args = get_args()
    logger = load_config(args)
    config = Config(args)

    config.workflow.check_binary_artifacts()
    config.workflow.check_types()
    logger.summary(f"Loaded configuration: {config}")

    if config.general.processes == 1:
        Log.default(logger, "Attention: tests are going to take only 1 process. The execution can be slow. "
                            "You can set the option `--processes` to wished processes quantity "
                            "or use special value `all` to use all available cores.")
    failed_tests = 0
    try:
        failed_tests = main_cycle(config, logger)
    except Exception:
        logger.logger.critical(traceback.format_exc())
    finally:
        sys.exit(0 if failed_tests == 0 else 1)


def main_cycle(config: Config, logger: Log) -> int:
    start = datetime.now(pytz.UTC)
    runner = RunnerStandardFlow(config)

    failed_tests = 0

    if config.test_suite.repeats_by_time is None:
        for repeat in range(1, config.test_suite.repeats + 1):
            repeat_str = f"Run #{repeat} of {config.test_suite.repeats}"
            failed_tests += launch_runners(runner, logger, config, repeat, repeat_str)
    else:
        before = datetime.now(pytz.UTC)
        current = before
        end = current + timedelta(seconds=float(config.test_suite.repeats_by_time))
        repeat = 1
        delta: float = 0.0
        while current < end:
            remains = round(config.test_suite.repeats_by_time - delta, 1)
            repeat_str = (f"Run #{repeat} for {config.test_suite.repeats_by_time} sec. "
                          f"Remains {remains} sec")
            failed_tests += launch_runners(runner, logger, config, repeat, repeat_str)
            repeat += 1
            current = datetime.now(pytz.UTC)
            delta = round((current - before).total_seconds(), 1)

    finish = datetime.now(pytz.UTC)
    Log.default(logger, f"Runner has been working for {round((finish - start).total_seconds())} sec")

    return failed_tests


def launch_runners(runner: Runner, logger: Log, config: Config, repeat: int, repeat_str: str) -> int:
    failed_tests = 0
    Log.all(logger, f"{repeat_str}: Runner {runner.name} started")
    runner.before_suite()
    runner.run_threads(repeat)
    runner.after_suite()
    Log.all(logger, f"{repeat_str}: Runner {runner.name} finished")
    Log.all(logger, pretty_divider())
    failed_tests += runner.summarize()
    Log.default(logger, f"{repeat_str}: Runner {runner.name}: {failed_tests} failed tests")
    if config.general.coverage.use_llvm_cov:
        runner.create_coverage_html()
    return failed_tests


def load_environment() -> None:
    home_path = Path.home().joinpath('.urunner.env')
    if home_path.exists():
        load_dotenv(home_path)

    dotenv_path = Path(__file__).with_name('.env')
    if dotenv_path.exists():
        load_dotenv(dotenv_path)

    check_obligatory_env('PANDA_SOURCE_PATH')
    check_obligatory_env('PANDA_BUILD')
    check_obligatory_env('WORK_DIR')
    os.environ['URUNNER_PATH'] = str(Path(__file__).parent)


def load_config(args: Dict[str, Any]) -> Log:
    runner_verbose = "runner.verbose"
    test_suite_const = "test-suite"

    verbose = args[runner_verbose] if runner_verbose in args else VerboseKind.SILENT
    if test_suite_const not in args:
        raise InvalidConfiguration(f"Incorrect configuration: cannot file element '{test_suite_const}'")
    test_suite = args[test_suite_const]
    work_dir = os.path.join(str(os.getenv("WORK_DIR")), test_suite)

    return Log.setup(verbose, work_dir)


if __name__ == "__main__":
    main()
