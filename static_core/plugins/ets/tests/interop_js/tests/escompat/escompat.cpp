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

#include <gtest/gtest.h>
#include "ets_interop_js_gtest.h"

namespace panda::ets::interop::js::testing {

class ESCompatTest : public EtsInteropTest {};

TEST_F(ESCompatTest, compat_array)
{
    ASSERT_EQ(true, RunJsTestSute("compat_array.js"));
}

// TODO(oignatenko) uncomment Array_TestJSLength code after interop is implemented from JS to eTS
TEST_F(ESCompatTest, compat_array_length)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_length.js"));
}

TEST_F(ESCompatTest, compat_array_pop)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_pop.js"));
}

TEST_F(ESCompatTest, compat_array_fill)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_fill.js"));
}

TEST_F(ESCompatTest, compat_array_shift)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_shift.js"));
}

// TODO(oignatenko) uncomment Array_TestJSSlice code after interop is implemented from JS to eTS
TEST_F(ESCompatTest, compat_array_slice)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_slice.js"));
}

// TODO(oignatenko) uncomment Array_TestJSSplice code after interop is implemented from JS to eTS
TEST_F(ESCompatTest, compat_array_splice)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_splice.js"));
}

// TODO(oignatenko) uncomment test_to_spliced.js code after recent regression making it work in place is fixed
TEST_F(ESCompatTest, compat_array_to_spliced)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_to_spliced.js"));
}

// TODO(oignatenko) uncomment Array_TestJSCopyWithin code after interop is implemented from JS to eTS
TEST_F(ESCompatTest, compat_array_copy_within)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_copy_within.js"));
}

// TODO(oignatenko) uncomment Array_TestJSWith code after interop is implemented from JS to eTS
TEST_F(ESCompatTest, compat_array_with)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_with.js"));
}

TEST_F(ESCompatTest, compat_array_last_index_of)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_last_index_of.js"));
}

// TODO(oignatenko) uncomment Array_TestJSToReversed code after interop is implemented from JS to eTS
TEST_F(ESCompatTest, compat_array_to_reversed)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_to_reversed.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_sort)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_sort.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_to_sorted)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_to_sorted.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_join)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_join.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_some)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_some.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_every)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_every.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_filter)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_filter.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_filter_array)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_filter_array.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_map)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_map.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_flat_map)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_flat_map.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_reduce)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_reduce.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_reduce_right)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_reduce_right.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_find_last)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_find_last.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_find_index)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_find_index.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_find)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_find.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_is_array)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_is_array.js"));
}

// TODO(oignatenko) enable this after interop is implemented for this method in either or both dimensions
TEST_F(ESCompatTest, DISABLED_compat_array_from_async)
{
    ASSERT_EQ(true, RunJsTestSute("array_js_suites/test_from_async.js"));
}

TEST_F(ESCompatTest, compat_boolean)
{
    // TODO(vpukhov): fix boxed primitives casts
    // ASSERT_EQ(true, RunJsTestSute("compat_boolean.js"));
}

TEST_F(ESCompatTest, compat_error)
{
    // TODO(vpukhov): compat accessors
    // ASSERT_EQ(true, RunJsTestSute("compat_error.js"));
}

}  // namespace panda::ets::interop::js::testing
