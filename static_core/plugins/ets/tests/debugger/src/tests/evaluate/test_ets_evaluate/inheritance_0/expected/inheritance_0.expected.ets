/*
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

import {A, B, C} from "../inheritance_0.base"

class EvalPatchFunction {
    private static EvalPatchFunction() {
        let varA = new A()
        varA.a_foo();
        varA.a_x = 10;

        let varC = new C()
        varC.a_foo();
        varC.a_x = 20;
        varC.b_foo();
        varC.b_x = 30;
        varC.c_foo();
        varC.c_x = 40;

        let varB = new B()
        varB.a_foo();
        varB.a_x = 50;
        varB.b_foo();
        const evalGeneratedResult = (varB.b_x = 60);
        return evalGeneratedResult;
    }
}
