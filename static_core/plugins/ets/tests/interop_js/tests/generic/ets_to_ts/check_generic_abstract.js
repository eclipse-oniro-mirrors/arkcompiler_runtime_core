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

const {
    AbstractClass,
    create_abstract_object_from_ets,
    num,
    str,
} = require("generic.test.js");

function check_abstract_class() {
    const abstarctClass = new AbstractClass(num);
    ASSERT_EQ(typeof num, typeof abstarctClass.get());
}

function check_create_abstract_object_from_ets() {
    const abstarctClass = create_abstract_object_from_ets();

    ASSERT_EQ(typeof str, typeof abstarctClass.get());
}

check_abstract_class();
check_create_abstract_object_from_ets();