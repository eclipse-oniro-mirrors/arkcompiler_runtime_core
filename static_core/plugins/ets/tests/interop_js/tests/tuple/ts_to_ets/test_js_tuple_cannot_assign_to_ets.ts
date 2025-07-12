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

let etsVm = globalThis.gtest.etsVm;
let setTupleObject = etsVm.getFunction('Lets_tuple/ETSGLOBAL;', "setTupleObject");

function testJsTupleCannotAssignToEts() {
    let tupleObj = [0x55aa, 0x7c00, "hello"];

    let errorCatched = false;
    try {
        setTupleObject(tupleObj);
    }
    catch (e) {
        // expected error
        ASSERT_TRUE(e.message === "Assigning a dynamic object to a static tuple object is not supported.");
        errorCatched = true;
    }

    ASSERT_TRUE(errorCatched);
}

function main(): void {
    testJsTupleCannotAssignToEts();
}

main();
