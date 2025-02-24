/*
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

/**
 * @State
 * @Tags interop, bu_j2a
 */
function stringifyObjectJ2a() {
    this.obj = null;

    /**
     * @Setup
     */
    this.setup = function () {
        console.log('Starting...');
        let penv = process.env;
        let stsVm = require(penv.MODULE_PATH + '/ets_interop_js_napi.node');

        const stsRT = stsVm.createRuntime({
            'boot-panda-files': penv.ARK_ETS_STDLIB_PATH + ':' + penv.ARK_ETS_INTEROP_JS_GTEST_ABC_PATH,
            'panda-files': penv.ARK_ETS_INTEROP_JS_GTEST_ABC_PATH,
        });

        if (!stsRT) {
            console.error('Failed to create ETS runtime');
            return 1;
        }

        const Person = stsVm.getClass('LStringifyObjectJ2a;');

        this.obj = new Person();

        return 0;
    };

    /**
     * @Benchmark
     */
    this.test = function() {
        JSON.stringify(this.obj);
        return;
    };

    return;
}
