/**
 * Copyright (c) 2025-2026 Huawei Device Co., Ltd.
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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include "ets_interop_js_gtest.h"

namespace ark::ets::interop::js::testing {

class EtsCallEtsCppStringMixedTest : public EtsInteropTest {};

// Named test-data constants (no magic numbers in test code)
constexpr int32_t INT_1 = 1;
constexpr int32_t INT_2 = 2;
constexpr int32_t INT_3 = 3;
constexpr int32_t INT_5 = 5;
constexpr int32_t INT_10 = 10;
constexpr int32_t INT_12 = 12;
constexpr int32_t INT_34 = 34;
constexpr int32_t INT_42 = 42;
constexpr int32_t INT_99 = 99;
constexpr int32_t INT_1000 = 1000;
constexpr size_t SIZE_2000 = 2000U;
constexpr int32_t INT_1000000 = 1000000;
constexpr int32_t INT_2000000 = 2000000;
constexpr double DBL_1_0 = 1.0;
constexpr double DBL_1_5 = 1.5;
constexpr double DBL_2_0 = 2.0;
constexpr double DBL_2_5 = 2.5;
constexpr double DBL_3_0 = 3.0;
constexpr double DBL_3_14 = 3.14;

// =========================================================================
// Two-arg string functions (three-stage: ExtractArgs + MaterializeArgs)
// Tests string extraction (GetStringHybrid) and materialization (EtsString::Create)
// =========================================================================

TEST_F(EtsCallEtsCppStringMixedTest, ConcatTwoStrings_Basic)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatTwoStrings", std::string_view("Hello, "),
                                               std::string_view("World!"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "Hello, World!");
}

TEST_F(EtsCallEtsCppStringMixedTest, ConcatTwoStrings_Both_Empty)
{
    auto result =
        CallEtsFunction<std::string>(GetPackageName(), "concatTwoStrings", std::string_view(""), std::string_view(""));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "");
}

TEST_F(EtsCallEtsCppStringMixedTest, ConcatTwoStrings_First_Empty)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatTwoStrings", std::string_view(""),
                                               std::string_view("abc"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "abc");
}

TEST_F(EtsCallEtsCppStringMixedTest, ConcatTwoStrings_Second_Empty)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatTwoStrings", std::string_view("xyz"),
                                               std::string_view(""));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "xyz");
}

TEST_F(EtsCallEtsCppStringMixedTest, ConcatTwoStrings_Long_Strings)
{
    std::string a(INT_1000, 'a');
    std::string b(INT_1000, 'b');
    auto result =
        CallEtsFunction<std::string>(GetPackageName(), "concatTwoStrings", std::string_view(a), std::string_view(b));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), SIZE_2000);
    ASSERT_EQ(result.value().substr(0, INT_1000), a);
    ASSERT_EQ(result.value().substr(INT_1000), b);
}

TEST_F(EtsCallEtsCppStringMixedTest, ConcatTwoStrings_Special_Chars)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatTwoStrings", std::string_view("!@#$%^&*()"),
                                               std::string_view("_+-=[]{}|;:',.<>?"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "!@#$%^&*()_+-=[]{}|;:',.<>?");
}

TEST_F(EtsCallEtsCppStringMixedTest, ConcatTwoStrings_Utf16_Chinese)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatTwoStrings", std::string_view("你好"),
                                               std::string_view("世界"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "你好世界");
}

TEST_F(EtsCallEtsCppStringMixedTest, ConcatTwoStrings_Utf16_Emoji)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatTwoStrings", std::string_view("😀"),
                                               std::string_view("🚀"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "😀🚀");
}

TEST_F(EtsCallEtsCppStringMixedTest, CompareTwoStrings_Equal)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "compareTwoStrings", std::string_view("abc"),
                                           std::string_view("abc"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), 0);
}

TEST_F(EtsCallEtsCppStringMixedTest, CompareTwoStrings_Less)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "compareTwoStrings", std::string_view("abc"),
                                           std::string_view("abd"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), -INT_1);
}

TEST_F(EtsCallEtsCppStringMixedTest, CompareTwoStrings_Greater)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "compareTwoStrings", std::string_view("xyz"),
                                           std::string_view("abc"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_1);
}

TEST_F(EtsCallEtsCppStringMixedTest, StringLengthSum_Basic)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "stringLengthSum", std::string_view("hello"),
                                           std::string_view("world"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_10);
}

TEST_F(EtsCallEtsCppStringMixedTest, StringLengthSum_Empty)
{
    auto result =
        CallEtsFunction<int32_t>(GetPackageName(), "stringLengthSum", std::string_view(""), std::string_view(""));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), 0);
}

TEST_F(EtsCallEtsCppStringMixedTest, FirstCharConcat_Basic)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "firstCharConcat", std::string_view("Hello"),
                                               std::string_view("World"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "HW");
}

TEST_F(EtsCallEtsCppStringMixedTest, FirstCharConcat_Empty)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "firstCharConcat", std::string_view(""),
                                               std::string_view("abc"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "");
}

// =========================================================================
// Three+ arg string functions (three-stage with multiple string extractions)
// =========================================================================

TEST_F(EtsCallEtsCppStringMixedTest, ConcatThreeStrings_Basic)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatThreeStrings", std::string_view("a"),
                                               std::string_view("b"), std::string_view("c"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "abc");
}

TEST_F(EtsCallEtsCppStringMixedTest, ConcatThreeStrings_With_Spaces)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatThreeStrings", std::string_view("Hello "),
                                               std::string_view("World"), std::string_view("!"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "Hello World!");
}

TEST_F(EtsCallEtsCppStringMixedTest, ConcatThreeStrings_Utf16)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatThreeStrings", std::string_view("你好"),
                                               std::string_view(", "), std::string_view("世界!"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "你好, 世界!");
}

TEST_F(EtsCallEtsCppStringMixedTest, ConcatFourStrings_Basic)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatFourStrings", std::string_view("1"),
                                               std::string_view("2"), std::string_view("3"), std::string_view("4"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "1234");
}

TEST_F(EtsCallEtsCppStringMixedTest, ConcatFiveStrings_Basic)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatFiveStrings", std::string_view("a"),
                                               std::string_view("b"), std::string_view("c"), std::string_view("d"),
                                               std::string_view("e"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "abcde");
}

TEST_F(EtsCallEtsCppStringMixedTest, ConcatThreeWithSeparator_Basic)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatThreeWithSeparator", std::string_view("x"),
                                               std::string_view("y"), std::string_view("z"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "x,y,z");
}

TEST_F(EtsCallEtsCppStringMixedTest, JoinFiveWithPipe_Basic)
{
    auto result =
        CallEtsFunction<std::string>(GetPackageName(), "joinFiveWithPipe", std::string_view("1"), std::string_view("2"),
                                     std::string_view("3"), std::string_view("4"), std::string_view("5"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "1|2|3|4|5");
}

// =========================================================================
// Mixed numeric + string functions (partial extraction)
// Tests interleaving of extracted (numeric/string) and non-extracted args
// =========================================================================

TEST_F(EtsCallEtsCppStringMixedTest, VerifyIntString_Match)
{
    auto result = CallEtsFunction<bool>(GetPackageName(), "verifyIntString", static_cast<int32_t>(INT_42),
                                        std::string_view("hello"));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value());
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifyIntString_Mismatch_Int)
{
    auto result =
        CallEtsFunction<bool>(GetPackageName(), "verifyIntString", static_cast<int32_t>(0), std::string_view("hello"));
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result.value());
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifyIntString_Mismatch_String)
{
    auto result = CallEtsFunction<bool>(GetPackageName(), "verifyIntString", static_cast<int32_t>(INT_42),
                                        std::string_view("world"));
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result.value());
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifyStringInt_Match)
{
    auto result = CallEtsFunction<bool>(GetPackageName(), "verifyStringInt", std::string_view("world"),
                                        static_cast<int32_t>(INT_99));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value());
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifyNumericStringNumeric_Match)
{
    auto result = CallEtsFunction<bool>(GetPackageName(), "verifyNumericStringNumeric", static_cast<int32_t>(INT_1),
                                        std::string_view("mid"), static_cast<int32_t>(INT_3));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value());
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifyNumericStringNumeric_Mismatch)
{
    auto result = CallEtsFunction<bool>(GetPackageName(), "verifyNumericStringNumeric", static_cast<int32_t>(INT_1),
                                        std::string_view("wrong"), static_cast<int32_t>(INT_3));
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result.value());
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifyIntStringDoubleString_Match)
{
    auto result = CallEtsFunction<bool>(GetPackageName(), "verifyIntStringDoubleString", static_cast<int32_t>(INT_10),
                                        std::string_view("str1"), DBL_3_14, std::string_view("str2"));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value());
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifyAllFiveMixed_Match)
{
    auto result =
        CallEtsFunction<bool>(GetPackageName(), "verifyAllFiveMixed", static_cast<int32_t>(INT_1),
                              std::string_view("two"), DBL_3_0, std::string_view("four"), static_cast<int32_t>(INT_5));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value());
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifyAllFiveMixed_Mismatch)
{
    auto result =
        CallEtsFunction<bool>(GetPackageName(), "verifyAllFiveMixed", static_cast<int32_t>(0), std::string_view("two"),
                              DBL_3_0, std::string_view("four"), static_cast<int32_t>(INT_5));
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result.value());
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifyAllSixMixed_Match)
{
    auto result = CallEtsFunction<bool>(GetPackageName(), "verifyAllSixMixed", static_cast<int32_t>(INT_1),
                                        std::string_view("two"), DBL_3_0, std::string_view("four"),
                                        static_cast<int32_t>(INT_5), std::string_view("six"));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value());
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifyLongStringLong_Match)
{
    auto result = CallEtsFunction<bool>(GetPackageName(), "verifyLongStringLong", static_cast<int64_t>(INT_1000000),
                                        std::string_view("middle"), static_cast<int64_t>(INT_2000000));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value());
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifyByteStringShort_Match)
{
    auto result = CallEtsFunction<bool>(GetPackageName(), "verifyByteStringShort", static_cast<int32_t>(INT_10),
                                        std::string_view("mid"), static_cast<int32_t>(INT_1000));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value());
}

// =========================================================================
// Six-arg interleaved (tests protoReader_ sync in MaterializeArgs)
// Pattern: int, string, int, string, int, string
// All numeric and string args are extracted by ExtractArgs
// =========================================================================

TEST_F(EtsCallEtsCppStringMixedTest, VerifySixInterleaved_Match)
{
    auto result = CallEtsFunction<bool>(GetPackageName(), "verifySixInterleaved", static_cast<int32_t>(INT_1),
                                        std::string_view("a"), static_cast<int32_t>(INT_2), std::string_view("b"),
                                        static_cast<int32_t>(INT_3), std::string_view("c"));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value());
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifySixInterleaved_Mismatch)
{
    auto result = CallEtsFunction<bool>(GetPackageName(), "verifySixInterleaved", static_cast<int32_t>(INT_1),
                                        std::string_view("a"), static_cast<int32_t>(INT_99), std::string_view("b"),
                                        static_cast<int32_t>(INT_3), std::string_view("c"));
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result.value());
}

// =========================================================================
// Eight-arg all strings (three-stage with 8 string extractions)
// =========================================================================

TEST_F(EtsCallEtsCppStringMixedTest, ConcatEightStrings_Basic)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatEightStrings", std::string_view("a"),
                                               std::string_view("b"), std::string_view("c"), std::string_view("d"),
                                               std::string_view("e"), std::string_view("f"), std::string_view("g"),
                                               std::string_view("h"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "abcdefgh");
}

// =========================================================================
// String return from numeric processing
// =========================================================================

TEST_F(EtsCallEtsCppStringMixedTest, VerifyIntPairReturnString_Match)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "verifyIntPairReturnString",
                                               static_cast<int32_t>(INT_12), static_cast<int32_t>(INT_34));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "match");
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifyIntPairReturnString_NoMatch)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "verifyIntPairReturnString",
                                               static_cast<int32_t>(INT_99), static_cast<int32_t>(INT_34));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "no_match");
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifyDoublePairReturnString_Match)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "verifyDoublePairReturnString", DBL_1_5, DBL_2_5);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "match");
}

TEST_F(EtsCallEtsCppStringMixedTest, VerifyDoublePairReturnString_NoMatch)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "verifyDoublePairReturnString", DBL_1_0, DBL_2_0);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "no_match");
}

// =========================================================================
// Single-arg string functions (fallback path: numNonRest<=1)
// =========================================================================

TEST_F(EtsCallEtsCppStringMixedTest, IdentityString_Basic)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "identityString", std::string_view("hello world"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "hello world");
}

TEST_F(EtsCallEtsCppStringMixedTest, IdentityString_Empty)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "identityString", std::string_view(""));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "");
}

TEST_F(EtsCallEtsCppStringMixedTest, IdentityString_Utf16)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "identityString", std::string_view("你好世界🌍"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "你好世界🌍");
}

TEST_F(EtsCallEtsCppStringMixedTest, StringLength_Basic)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "stringLength", std::string_view("hello"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_5);
}

TEST_F(EtsCallEtsCppStringMixedTest, StringLength_Empty)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "stringLength", std::string_view(""));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), 0);
}

TEST_F(EtsCallEtsCppStringMixedTest, StringLength_Long)
{
    std::string s(INT_1000, 'x');
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "stringLength", std::string_view(s));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_1000);
}

TEST_F(EtsCallEtsCppStringMixedTest, ReverseString_Basic)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "reverseString", std::string_view("abc"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "cba");
}

TEST_F(EtsCallEtsCppStringMixedTest, ReverseString_Palindrome)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "reverseString", std::string_view("level"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "level");
}

TEST_F(EtsCallEtsCppStringMixedTest, ReverseString_Empty)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "reverseString", std::string_view(""));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "");
}

TEST_F(EtsCallEtsCppStringMixedTest, ReverseString_Single_Char)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "reverseString", std::string_view("x"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "x");
}

// =========================================================================
// String varargs with fixed params (three-stage + rest packing)
// =========================================================================

TEST_F(EtsCallEtsCppStringMixedTest, ConcatTwoFixedAndRestStrings_With_Rest)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatTwoFixedAndRestStrings", std::string_view("a"),
                                               std::string_view("b"), std::string_view("c"), std::string_view("d"),
                                               std::string_view("e"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "abcde");
}

TEST_F(EtsCallEtsCppStringMixedTest, ConcatTwoFixedAndRestStrings_No_Rest)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatTwoFixedAndRestStrings", std::string_view("x"),
                                               std::string_view("y"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "xy");
}

TEST_F(EtsCallEtsCppStringMixedTest, ConcatTwoFixedAndRestStrings_One_Rest)
{
    auto result = CallEtsFunction<std::string>(GetPackageName(), "concatTwoFixedAndRestStrings", std::string_view("1"),
                                               std::string_view("2"), std::string_view("3"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "123");
}

}  // namespace ark::ets::interop::js::testing
