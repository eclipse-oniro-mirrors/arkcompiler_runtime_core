/**
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License"
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

#ifndef PANDA_PLUGINS_ETS_TESTS_MOCK_ACCESSING_OBJECTS_FIELDS_TEST_HELPER_H
#define PANDA_PLUGINS_ETS_TESTS_MOCK_ACCESSING_OBJECTS_FIELDS_TEST_HELPER_H

#include <gtest/gtest.h>

#include "plugins/ets/tests/mock/mock_test_helper.h"

namespace panda::ets::test {

class AccessingObjectsFieldsTestBase : public MockEtsNapiTestBaseClass {
protected:
    AccessingObjectsFieldsTestBase() = default;
    explicit AccessingObjectsFieldsTestBase(const char *test_bin_file_name)
        : MockEtsNapiTestBaseClass(test_bin_file_name) {};
};

}  // namespace panda::ets::test

#endif  // PANDA_PLUGINS_ETS_TESTS_MOCK_ACCESSING_OBJECTS_FIELDS_TEST_HELPER_H