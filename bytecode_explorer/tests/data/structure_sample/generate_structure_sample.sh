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

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RUNTIME_CORE_ROOT=$(cd "$SCRIPT_DIR/../../../../" && pwd)
REPO_ROOT=$(cd "$RUNTIME_CORE_ROOT/../.." && pwd)
OUTPUT_DIR=${1:-"$SCRIPT_DIR"}
ES2ABC="$REPO_ROOT/out/x64.debug/arkcompiler/ets_frontend/es2abc"
MERGE_ABC="$REPO_ROOT/out/x64.debug/arkcompiler/ets_frontend/merge_abc"
TEMP_DIR=$(mktemp -d)

cleanup()
{
    if [ -d "$TEMP_DIR" ]; then
        rm -rf "$TEMP_DIR"
    fi
}
trap cleanup EXIT

mkdir -p "$OUTPUT_DIR" "$TEMP_DIR/merge_input"
mkdir -p "$TEMP_DIR/default_api_merge_input"
OUTPUT_DIR=$(cd "$OUTPUT_DIR" && pwd)

printf '%s;structure_alpha;esm;structure_alpha.ts;entry;false;ets\n' "$SCRIPT_DIR/structure_alpha.ts" \
    > "$TEMP_DIR/alpha.list"
printf '%s;structure_beta;esm;structure_beta.ts;entry;false;ets\n' "$SCRIPT_DIR/structure_beta.ts" \
    > "$TEMP_DIR/beta.list"

"$ES2ABC" --module --merge-abc --extension=ts --enable-annotations \
    --outputProto="$TEMP_DIR/alpha.protoBin" "@$TEMP_DIR/alpha.list"
"$ES2ABC" --module --merge-abc --extension=ts --enable-annotations \
    --outputProto="$TEMP_DIR/beta.protoBin" "@$TEMP_DIR/beta.list"
"$ES2ABC" --module --merge-abc --extension=ts --enable-annotations \
    --target-api-version=12 --target-api-sub-version=beta1 \
    --outputProto="$TEMP_DIR/beta_default_api.protoBin" "@$TEMP_DIR/beta.list"

cp "$TEMP_DIR/alpha.protoBin" "$TEMP_DIR/merge_input/alpha.protoBin"
cp "$TEMP_DIR/beta.protoBin" "$TEMP_DIR/merge_input/beta.protoBin"
"$MERGE_ABC" --input "$TEMP_DIR/merge_input" --suffix protoBin \
    --output structure_sample.abc --outputFilePath "$OUTPUT_DIR"
cp "$TEMP_DIR/beta_default_api.protoBin" "$TEMP_DIR/default_api_merge_input/beta.protoBin"
"$MERGE_ABC" --input "$TEMP_DIR/default_api_merge_input" --suffix protoBin \
    --output structure_default_api_merged.abc --outputFilePath "$OUTPUT_DIR"

(
    cd "$SCRIPT_DIR"
    "$ES2ABC" --module --extension=ts --enable-annotations \
        --output="$OUTPUT_DIR/structure_alpha_nonmerge.abc" structure_alpha.ts
    "$ES2ABC" --module --extension=ts --enable-annotations \
        --target-api-version=12 --target-api-sub-version=beta1 \
        --output="$OUTPUT_DIR/structure_default_api.abc" structure_beta.ts
    "$ES2ABC" --extension=ts --target-api-version=12 --target-api-sub-version=beta1 \
        --output="$OUTPUT_DIR/structure_script.abc" structure_script.ts
)

echo "Generated merged, merged no-scope, non-merge, no-scope and script structure fixtures in $OUTPUT_DIR"
