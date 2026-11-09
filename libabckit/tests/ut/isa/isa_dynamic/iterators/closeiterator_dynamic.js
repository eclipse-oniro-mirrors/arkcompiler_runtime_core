/**
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

let closed = 0;

function closeOnThrow(mode) {
    const iterator = {};
    iterator[Symbol.iterator] = function () { return this; };
    iterator.next = function () { return {value: 1, done: false}; };
    if (mode !== 3) {
        iterator.return = function () {
            closed++;
            if (mode === 1) {
                throw 88;
            }
            if (mode === 2) {
                return 1;
            }
            return {};
        };
    }
    try {
        for (const value of iterator) {
            throw 99;
        }
    } catch (error) {
        print(error);
    }
}

// Successful close, throwing return(), non-object result, and absent return().
// The original exception must win in all four cases; return() runs only when present.
closeOnThrow(0);
print(closed);
closeOnThrow(1);
print(closed);
closeOnThrow(2);
print(closed);
closeOnThrow(3);
print(closed);
