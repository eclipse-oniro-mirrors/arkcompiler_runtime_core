#!/usr/bin/env python3
# -- coding: utf-8 --
#
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

from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import TestCase
from unittest.mock import Mock

from runner.extensions.generators.sts_stdlib.func_templates_generator import FuncTestsCodeGenerator
from runner.options.config import Config
from runner.utils import make_gtest_name


class SourceExtensionsTest(TestCase):
    def test_mixed_templates_preserve_source_suffix(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            for name, extension in (("legacy", "ets"), ("renamed", "ats")):
                (source / f"{name}.{extension}.j2").write_text("function main(): void {}", encoding="utf-8")
                (source / f"config-{name}.yaml").write_text("{}", encoding="utf-8")
            generator = FuncTestsCodeGenerator(source, root / "generated", Mock(spec=Config))
            generated = [Path(name) for name in generator.generate()]
            self.assertEqual({path.name for path in generated}, {"test-legacy-0001.ets", "test-renamed-0001.ats"})
            for path in generated:
                self.assertIn("function main(): void {}", path.read_text(encoding="utf-8"))

    def test_source_and_abc_names_survive_rename(self) -> None:
        for extension in ("ets", "ats"):
            for suffix in (f".{extension}", f".{extension}.abc"):
                with self.subTest(suffix=suffix):
                    self.assertEqual(make_gtest_name(Path(f"sample_test{suffix}"), "runtime"), "runtime_sample")
