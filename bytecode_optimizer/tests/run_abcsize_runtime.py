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

"""Compare execution with SCCP and exception-region optimization on and off."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    compiler = build / "arkcompiler/ets_frontend/es2abc"
    runtime = build / "arkcompiler/ets_runtime/ark_js_vm"
    source = Path(__file__).resolve().parent / "js/abcsizeRuntimeRegression.js"
    variants = [
        ("baseline", ["--opt-level=0"]),
        ("optimized", ["--opt-level=2", "--branch-elimination=false", "--opt-try-catch-func=false"]),
        ("sccp", ["--opt-level=2", "--branch-elimination=true", "--opt-try-catch-func=false"]),
        ("eh", ["--opt-level=2", "--branch-elimination=false", "--opt-try-catch-func=true"]),
        ("sccp-eh", ["--opt-level=2", "--branch-elimination=true", "--opt-try-catch-func=true"]),
    ]
    with tempfile.TemporaryDirectory(prefix="abcsize-runtime-") as directory:
        for name, flags in variants:
            output = Path(directory) / (name + ".abc")
            subprocess.run([str(compiler), *flags, "--output=" + str(output), str(source)],
                           check=True, timeout=120)
            result = subprocess.run([str(runtime), str(output)], check=True, timeout=60,
                                    capture_output=True, text=True)
            if result.stdout.strip() != "PASS: abcsize runtime":
                raise RuntimeError(name + ": " + result.stdout + result.stderr)
            print(f"{name}: PASS, {output.stat().st_size} bytes")


if __name__ == "__main__":
    main()
