/**
 * Copyright (c) 2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

const helper = requireNapiPreview('libinterop_test_helper.so', false);

function runTest(test) {
    print('Running test ' + test);
    const gtestAbcPath = helper.getEnvironmentVar('ARK_ETS_INTEROP_JS_GTEST_ABC_PATH');
    const stdlibPath = helper.getEnvironmentVar('ARK_ETS_STDLIB_PATH');
    let etsVm = requireNapiPreview('ets_interop_js_napi_arkjsvm.so', false);
    const etsOpts = {
        'panda-files': gtestAbcPath,
        'boot-panda-files': `${stdlibPath}:${gtestAbcPath}`,
        'gc-trigger-type': 'heap-trigger',
        'load-runtimes': 'ets',
        'compiler-enable-jit': 'false',
        'coroutine-enable-external-scheduling': 'true',
    };
    if (!etsVm.createRuntime(etsOpts)) {
        throw Error('Cannot create ETS runtime');
    }

    etsVm.call(test);
    let tId = 0;
    let checkFn = () => {
        if (etsVm.call('.is_unset')) {
            return;
        }
        helper.clearInterval(tId);
        etsVm.call('.check');
    };
    tId = helper.setInterval(checkFn);
}

let args = helper.getArgv();
if (args.length !== 5) {
    throw Error('Expected test name');
}
runTest(args[4]);
