#!/usr/bin/env bash
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

# Regenerates the debug/release pair used by debug_build_detector_test.
# The source intentionally contains a closure (debug-only newlexenvwithname
# instruction with its variable-name literal array) and named locals (debug
# line-program variable table), so the debug fixture exercises every detector
# signal while the release fixture stays free of all of them.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RUNTIME_CORE_ROOT=$(cd "$SCRIPT_DIR/../../../../" && pwd)
REPO_ROOT=$(cd "$RUNTIME_CORE_ROOT/../.." && pwd)
OUTPUT_DIR=${1:-"$SCRIPT_DIR"}
ES2ABC="$REPO_ROOT/out/x64.release/arkcompiler/ets_frontend/es2abc"
TEMP_DIR=$(mktemp -d)

cleanup()
{
    if [ -d "$TEMP_DIR" ]; then
        rm -rf "$TEMP_DIR"
    fi
}
trap cleanup EXIT

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR=$(cd "$OUTPUT_DIR" && pwd)

printf '%s;&compile_mode_sample&;esm;compile_mode_sample.ets;entry;false;ets\n' \
    "$SCRIPT_DIR/compile_mode_sample.ets" > "$TEMP_DIR/compile_mode_sample.list"

"$ES2ABC" --module --merge-abc \
    --output="$OUTPUT_DIR/compile_mode_release.abc" "@$TEMP_DIR/compile_mode_sample.list"
"$ES2ABC" --module --merge-abc --debug-info \
    --output="$OUTPUT_DIR/compile_mode_debug.abc" "@$TEMP_DIR/compile_mode_sample.list"

echo "Generated release and debug compile-mode fixtures in $OUTPUT_DIR"
