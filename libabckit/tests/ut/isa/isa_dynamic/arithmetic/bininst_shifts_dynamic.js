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
    foo(a, b) {
        return 3 * a * b;
    }
}

let a = new A();
let res = a.foo(10, 2);
print(res);

// After AOP 1:

// class A {
//    foo(a: long, b: long) : long {
//       return a >> b;
//    }
// }

// After AOP 2:

// class A {
//    foo(a: long, b: long) : long {
//       return a << b;
//    }
// }

// After AOP 3:

// class A {
//    foo(a: long, b: long) : long {
//       return a >>> b;
//    }
// }
