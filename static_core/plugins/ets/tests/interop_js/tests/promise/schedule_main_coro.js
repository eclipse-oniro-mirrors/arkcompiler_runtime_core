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

function init() {
    const gtestAbcPath = helper.getEnvironmentVar('ARK_ETS_INTEROP_JS_GTEST_ABC_PATH');
    const stdlibPath = helper.getEnvironmentVar('ARK_ETS_STDLIB_PATH');

    let etsVm = requireNapiPreview('ets_interop_js_napi_arkjsvm.so', false);
    const etsOpts = {
        'panda-files': gtestAbcPath,
        'boot-panda-files': `${stdlibPath}:${gtestAbcPath}`,
        'coroutine-enable-external-scheduling': 'true',
        'xgc-trigger-type': 'never',
    };
    if (!etsVm.createRuntime(etsOpts)) {
        throw Error('Cannot create ETS runtime');
    }
    return etsVm;
}

function runTest() {
    let etsVm = init();
    let tId = 0;

    let waitForSchedule = () => {
        const isWasScheduled = etsVm.getFunction('LETSGLOBAL;', 'wasScheduled');
        let wasSchedulded = isWasScheduled();
        if (wasSchedulded) {
            helper.clearInterval(tId);
        }
    };

    const waitUntillJsIsReady = etsVm.getFunction('LETSGLOBAL;', 'waitUntillJsIsReady');
    waitUntillJsIsReady();
    const jsIsReady = etsVm.getFunction('LETSGLOBAL;', 'jsIsReady');
    jsIsReady();
    tId = helper.setInterval(waitForSchedule, 0);
}

runTest();
