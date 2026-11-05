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

// 20260807927972 Task 02-05: input for the add_try_catch serial-vs-4-worker-parallel scenario.
// Four independent target methods (run1..run4) so each worker wraps a DIFFERENT function's body in
// a try-catch on the SAME shared file (exercising the per-function Graph cache + the single-builder
// snapshot protocol), with no same-function contention. Each runN has the identical body shape the
// serial add_try_catch TransformIr expects (init + call to a throwing api + return).

function throwableApi() {
    print('THROW');
    throw new Error('DUMMY_ERROR');
}

class Handler {
    run1() {
        let tmp;
        tmp = throwableApi();
        return tmp;
    }
    run2() {
        let tmp;
        tmp = throwableApi();
        return tmp;
    }
    run3() {
        let tmp;
        tmp = throwableApi();
        return tmp;
    }
    run4() {
        let tmp;
        tmp = throwableApi();
        return tmp;
    }
}

function main() {
    let c = new Handler();
    print(c.run1());
    print(c.run2());
    print(c.run3());
    print(c.run4());
}

main();
