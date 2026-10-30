/**
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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

// 20260807927972 Task 02-04: input for the add_log serial-vs-4-worker-parallel scenario.
// Four independent target methods (handle1..handle4) so each worker transforms a DIFFERENT
// function on the SAME shared file (exercising the per-function Graph cache + the single-builder
// snapshot protocol), with no same-function contention.

class MyClass {
    handle1() {
        if (1 < 0) {
            print('abckit');
        } else {
            print('abckit');
        }
    }
    handle2() {
        if (1 < 0) {
            print('abckit');
        } else {
            print('abckit');
        }
    }
    handle3() {
        if (1 < 0) {
            print('abckit');
        } else {
            print('abckit');
        }
    }
    handle4() {
        if (1 < 0) {
            print('abckit');
        } else {
            print('abckit');
        }
    }
}

function main() {
    let c = new MyClass();
    c.handle1();
    c.handle2();
    c.handle3();
    c.handle4();
}

main();
