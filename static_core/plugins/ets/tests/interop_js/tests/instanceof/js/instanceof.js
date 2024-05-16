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

"use strict";


let A = globalThis.gtest.etsVm.getClass("LA;");

class ADeclared {};
class BDeclared {};
class CDeclared extends ADeclared {};
class DDeclared extends A {}

class AValue {}
class CValue extends BDeclared {};

function js_fn() {
    return new ADeclared();
}

function as_jsvalue(x) {
    return x;
}

module.exports = {
    AValue,
    CValue,
    ADeclared,
    BDeclared,
    CDeclared,
    DDeclared,
    js_avalue: new AValue(),
    js_cvalue: new CValue(),
    as_jsvalue,
    js_fn,
    create_error: Error
}