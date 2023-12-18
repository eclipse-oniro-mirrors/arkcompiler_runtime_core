#!/bin/bash
# Copyright (c) 2021-2023 Huawei Device Co., Ltd.
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

set -e

if [[ -z $1 ]]; then
    echo "Usage: generate.sh <panda source> <generator options>"
    echo "    <panda source> path where the panda source are located"
    echo "    <generator options> options to main.py. To see full list use --help"

    exit 1
fi

ROOT_DIR=$(realpath $1)
shift 1
export PYTHONPATH=$PYTHONPATH:${ROOT_DIR}/tests/tests-u-runner
GENERATOR=${ROOT_DIR}/tests/tests-u-runner/runner/plugins/ets/preparation_step.py
GENERATOR_OPTIONS=$*

source ${ROOT_DIR}/scripts/python/venv-utils.sh
activate_venv
set +e

python3 -B ${GENERATOR} ${GENERATOR_OPTIONS}
EXIT_CODE=$?

set -e
deactivate_venv

exit ${EXIT_CODE}
