/**
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

const { etsVm, getTestModule } = require("escompat.test.js")

const ets_mod = getTestModule("escompat_test");
const CreateEtsSample = ets_mod.getFunction("Error_CreateEtsSample");
const TestJSSample = ets_mod.getFunction("Error_TestJSSample");

{   // Test JS Error
    TestJSSample(new Error("foo"));
}

{   // Test ETS Error
    let v = CreateEtsSample();
    ASSERT_TRUE(v instanceof Error);

    ASSERT_EQ(v.message, "bar");
}
