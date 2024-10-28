/**
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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
// Before AOP:
class A {
    constructor(x, y) {
        print(x + y);
    }
}
class B extends A {
    constructor(arr) {
        super(0, 0);
        print(arr);
    }
}

let arr = [1, 2];

new B(arr);

// After AOP:
// class A {
//     constructor(x, y) {
//         print(x + y)
//     }
// }
// class B extends A {
//     constructor(arr) {
//         super(...arr)
//         print(arr)
//     }
// }

// let arr = [1, 2]

// new B(arr);