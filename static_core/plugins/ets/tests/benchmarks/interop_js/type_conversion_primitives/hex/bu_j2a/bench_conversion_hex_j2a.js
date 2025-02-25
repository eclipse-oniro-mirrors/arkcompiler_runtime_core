/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
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
function conversionHexJ2a() {
    this.bench = null;
    this.hexString = null;

    function generateNumber(seed) {
        const modulus = Math.pow(2, 32);
        const a = 1664525;
        const c = 1013904223;

        seed = (a * seed + c) % modulus;

        return Math.floor((seed / modulus) * 100);
    };

    /**
     * @Setup
     */
    this.setup = function () {
        console.log('Starting...');
        this.hexString = '';
        const seed = 123;
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

        const data = generateNumber(seed);
        this.hexString = data.toString(16);

        const State = stsVm.getClass('LConversionHexJ2a;');

        this.bench = new State();
        this.bench.setup();

        return 0;
    };

    /**
     * @Benchmark
     */
    this.test = function() {
        this.bench.test(this.hexString);
        return;
    };

    return;
}
