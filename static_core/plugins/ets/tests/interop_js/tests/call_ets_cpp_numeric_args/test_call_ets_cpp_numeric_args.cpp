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
#include "ets_interop_js_gtest.h"

namespace ark::ets::interop::js::testing {

class EtsCallEtsCppNumericArgsTest : public EtsInteropTest {};

// Named test-data constants (no magic numbers in test code)
constexpr int32_t INT_1 = 1;
constexpr int32_t INT_2 = 2;
constexpr int32_t INT_3 = 3;
constexpr int32_t INT_4 = 4;
constexpr int32_t INT_5 = 5;
constexpr int32_t INT_6 = 6;
constexpr int32_t INT_7 = 7;
constexpr int32_t INT_8 = 8;
constexpr int32_t INT_9 = 9;
constexpr int32_t INT_10 = 10;
constexpr int32_t INT_11 = 11;
constexpr int32_t INT_12 = 12;
constexpr int32_t INT_13 = 13;
constexpr int32_t INT_14 = 14;
constexpr int32_t INT_15 = 15;
constexpr int32_t INT_16 = 16;
constexpr int32_t INT_17 = 17;
constexpr int32_t INT_20 = 20;
constexpr int32_t INT_21 = 21;
constexpr int32_t INT_25 = 25;
constexpr int32_t INT_30 = 30;
constexpr int32_t INT_36 = 36;
constexpr int32_t INT_40 = 40;
constexpr int32_t INT_42 = 42;
constexpr int32_t INT_44 = 44;
constexpr int32_t INT_50 = 50;
constexpr int32_t INT_56 = 56;
constexpr int32_t INT_60 = 60;
constexpr int32_t INT_70 = 70;
constexpr int32_t INT_100 = 100;
constexpr int32_t INT_128 = 128;
constexpr int32_t INT_136 = 136;
constexpr int32_t INT_153 = 153;
constexpr int32_t INT_160 = 160;
constexpr int32_t INT_200 = 200;
constexpr int32_t INT_300 = 300;
constexpr int32_t INT_400 = 400;
constexpr int32_t INT_500 = 500;
constexpr int32_t INT_600 = 600;
constexpr int32_t INT_700 = 700;
constexpr int32_t INT_800 = 800;
constexpr int32_t INT_999 = 999;
constexpr int32_t INT_1000 = 1000;
constexpr int32_t INT_1010 = 1010;
constexpr int32_t INT_32639 = 32639;
constexpr int32_t INT_32767 = 32767;
constexpr int32_t INT_1000000 = 1000000;
constexpr int32_t INT_2000000 = 2000000;
constexpr int32_t INT_3000000 = 3000000;
constexpr int32_t INT_5000000 = 5000000;
constexpr int32_t INT_2147483647 = 2147483647;
constexpr int64_t I64_9999999999 = 9999999999LL;
constexpr int64_t I64_10000000099 = 10000000099LL;
constexpr int64_t I64_9007199254740992 = 9007199254740992LL;
constexpr int64_t I64_18014398509481984 = 18014398509481984LL;
constexpr double DBL_0_1 = 0.1;
constexpr double DBL_0_2 = 0.2;
constexpr double DBL_0_3 = 0.3;
constexpr double DBL_0_4 = 0.4;
constexpr double DBL_0_5 = 0.5;
constexpr double DBL_0_6 = 0.6;
constexpr double DBL_0_7 = 0.7;
constexpr double DBL_0_8 = 0.8;
constexpr double DBL_1_0 = 1.0;
constexpr double DBL_1_1 = 1.1;
constexpr double DBL_1_25 = 1.25;
constexpr double DBL_1_5 = 1.5;
constexpr double DBL_2_0 = 2.0;
constexpr double DBL_2_2 = 2.2;
constexpr double DBL_2_5 = 2.5;
constexpr double DBL_3_0 = 3.0;
constexpr double DBL_3_14 = 3.14;
constexpr double DBL_3_14159 = 3.14159;
constexpr double DBL_3_25 = 3.25;
constexpr double DBL_3_3 = 3.3;
constexpr double DBL_3_6 = 3.6;
constexpr double DBL_3_75 = 3.75;
constexpr double DBL_4_0 = 4.0;
constexpr double DBL_4_4 = 4.4;
constexpr double DBL_5_0 = 5.0;
constexpr double DBL_5_5 = 5.5;
constexpr double DBL_6_0 = 6.0;
constexpr double DBL_7_0 = 7.0;
constexpr double DBL_7_25 = 7.25;
constexpr double DBL_8_0 = 8.0;
constexpr double DBL_10_0 = 10.0;
constexpr double DBL_10_5 = 10.5;
constexpr double DBL_15_0 = 15.0;
constexpr double DBL_16_5 = 16.5;
constexpr double DBL_30_0 = 30.0;
constexpr double DBL_31_75 = 31.75;
constexpr double DBL_36_0 = 36.0;
constexpr double DBL_45_14 = 45.14;
constexpr double DBL_60_0 = 60.0;
constexpr double DBL_300_0 = 300.0;
constexpr double DBL_600_0 = 600.0;

// =========================================================================
// Two-arg int functions (three-stage path: numNonRest=2)
// Tests ExtractArgs numeric extraction for I32 type
// =========================================================================

TEST_F(EtsCallEtsCppNumericArgsTest, AddTwoInt_Basic)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addTwoInt", INT_10, INT_20);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_30);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddTwoInt_Zero)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addTwoInt", 0, 0);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), 0);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddTwoInt_Negative)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addTwoInt", -INT_100, -INT_200);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), -INT_300);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddTwoInt_Mixed_Signs)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addTwoInt", -INT_50, INT_100);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_50);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddTwoInt_MaxInt)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addTwoInt", INT_2147483647, 0);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_2147483647);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddTwoInt_Overflow)
{
    // int32 overflow wraps around
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addTwoInt", INT_2147483647, INT_1);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT32_MIN);
}

TEST_F(EtsCallEtsCppNumericArgsTest, SubTwoInt_Basic)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "subTwoInt", INT_100, INT_30);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_70);
}

TEST_F(EtsCallEtsCppNumericArgsTest, SubTwoInt_Negative_Result)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "subTwoInt", INT_10, INT_50);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), -INT_40);
}

TEST_F(EtsCallEtsCppNumericArgsTest, MulTwoInt_Basic)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "mulTwoInt", INT_7, INT_8);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_56);
}

TEST_F(EtsCallEtsCppNumericArgsTest, MulTwoInt_By_Zero)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "mulTwoInt", INT_999, 0);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), 0);
}

TEST_F(EtsCallEtsCppNumericArgsTest, MulTwoInt_Negative)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "mulTwoInt", -INT_7, INT_8);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), -INT_56);
}

TEST_F(EtsCallEtsCppNumericArgsTest, MaxTwoInt_First)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "maxTwoInt", INT_100, INT_50);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_100);
}

TEST_F(EtsCallEtsCppNumericArgsTest, MaxTwoInt_Second)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "maxTwoInt", INT_30, INT_60);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_60);
}

TEST_F(EtsCallEtsCppNumericArgsTest, MaxTwoInt_Equal)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "maxTwoInt", INT_42, INT_42);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_42);
}

TEST_F(EtsCallEtsCppNumericArgsTest, MinTwoInt_First)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "minTwoInt", INT_10, INT_20);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_10);
}

TEST_F(EtsCallEtsCppNumericArgsTest, MinTwoInt_Second)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "minTwoInt", INT_50, INT_30);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_30);
}

// =========================================================================
// Two-arg long functions (three-stage path: numNonRest=2)
// Tests ExtractArgs numeric extraction for I64 type
// =========================================================================

TEST_F(EtsCallEtsCppNumericArgsTest, AddTwoLong_Basic)
{
    auto result = CallEtsFunction<int64_t>(GetPackageName(), "addTwoLong", static_cast<int64_t>(INT_1000000),
                                           static_cast<int64_t>(INT_2000000));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_3000000);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddTwoLong_Large_Values)
{
    auto result = CallEtsFunction<int64_t>(GetPackageName(), "addTwoLong", static_cast<int64_t>(I64_9007199254740992),
                                           static_cast<int64_t>(I64_9007199254740992));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), I64_18014398509481984);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddTwoLong_Negative)
{
    auto result = CallEtsFunction<int64_t>(GetPackageName(), "addTwoLong", static_cast<int64_t>(-INT_5000000),
                                           static_cast<int64_t>(INT_3000000));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), -INT_2000000);
}

// =========================================================================
// Two-arg double functions (three-stage path: numNonRest=2)
// Tests ExtractArgs numeric extraction for F64 type
// =========================================================================

TEST_F(EtsCallEtsCppNumericArgsTest, AddTwoDouble_Basic)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "addTwoDouble", DBL_1_5, DBL_2_5);
    ASSERT_TRUE(result.has_value());
    ASSERT_DOUBLE_EQ(result.value(), DBL_4_0);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddTwoDouble_Precision)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "addTwoDouble", DBL_0_1, DBL_0_2);
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_0_3, 1e-9);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddTwoDouble_Negative)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "addTwoDouble", -DBL_1_5, DBL_2_5);
    ASSERT_TRUE(result.has_value());
    ASSERT_DOUBLE_EQ(result.value(), DBL_1_0);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddTwoDouble_Zero)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "addTwoDouble", 0.0, 0.0);
    ASSERT_TRUE(result.has_value());
    ASSERT_DOUBLE_EQ(result.value(), 0.0);
}

TEST_F(EtsCallEtsCppNumericArgsTest, SubTwoDouble_Basic)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "subTwoDouble", DBL_10_5, DBL_3_25);
    ASSERT_TRUE(result.has_value());
    ASSERT_DOUBLE_EQ(result.value(), DBL_7_25);
}

TEST_F(EtsCallEtsCppNumericArgsTest, MulTwoDouble_Basic)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "mulTwoDouble", DBL_2_5, DBL_4_0);
    ASSERT_TRUE(result.has_value());
    ASSERT_DOUBLE_EQ(result.value(), DBL_10_0);
}

TEST_F(EtsCallEtsCppNumericArgsTest, DivTwoDouble_Basic)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "divTwoDouble", DBL_10_0, DBL_4_0);
    ASSERT_TRUE(result.has_value());
    ASSERT_DOUBLE_EQ(result.value(), DBL_2_5);
}

TEST_F(EtsCallEtsCppNumericArgsTest, DivTwoDouble_By_Zero)
{
    // Division by zero in double produces Infinity
    auto result = CallEtsFunction<double>(GetPackageName(), "divTwoDouble", DBL_1_0, 0.0);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::isinf(result.value()));
}

// =========================================================================
// Mixed numeric type two-arg functions (three-stage path)
// Tests ExtractArgs with different TypeIds in one call
// =========================================================================

TEST_F(EtsCallEtsCppNumericArgsTest, AddByteShort_Basic)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addByteShort", static_cast<int32_t>(INT_10),
                                           static_cast<int32_t>(INT_1000));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_1010);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddByteShort_Boundary)
{
    // byte range: -128..127, short range: -32768..32767
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addByteShort", static_cast<int32_t>(-INT_128),
                                           static_cast<int32_t>(INT_32767));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_32639);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddByteShort_ByteOverflow)
{
    // 200 exceeds byte range, should be truncated to -56 (200 - 256)
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addByteShort", static_cast<int32_t>(INT_200),
                                           static_cast<int32_t>(INT_100));
    ASSERT_TRUE(result.has_value());
    // 200 as int8_t = -56, -56 + 100 = 44
    ASSERT_EQ(result.value(), INT_44);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddIntLong_Basic)
{
    auto result = CallEtsFunction<int64_t>(GetPackageName(), "addIntLong", static_cast<int32_t>(INT_100),
                                           static_cast<int64_t>(I64_9999999999));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), I64_10000000099);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddIntFloat_Basic)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "addIntFloat", static_cast<int32_t>(INT_10), DBL_0_5);
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_10_5, 1e-6);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddFloatDouble_Basic)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "addFloatDouble", DBL_1_25, DBL_2_5);
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_3_75, 1e-6);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddIntDouble_Basic)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "addIntDouble", static_cast<int32_t>(INT_42), DBL_3_14);
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_45_14, 1e-6);
}

// =========================================================================
// Three+ arg int functions (three-stage path: numNonRest>=3)
// =========================================================================

TEST_F(EtsCallEtsCppNumericArgsTest, AddThreeInt_Basic)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addThreeInt", INT_1, INT_2, INT_3);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_6);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddFourInt_Basic)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addFourInt", INT_10, INT_20, INT_30, INT_40);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_100);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddFiveInt_Basic)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addFiveInt", INT_1, INT_2, INT_3, INT_4, INT_5);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_15);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddFiveInt_All_Negative)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addFiveInt", -INT_1, -INT_2, -INT_3, -INT_4, -INT_5);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), -INT_15);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddFiveInt_Mixed)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addFiveInt", INT_100, -INT_50, INT_25, -INT_10, INT_5);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_70);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddSixInt_Basic)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "addSixInt", INT_1, INT_2, INT_3, INT_4, INT_5, INT_6);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_21);
}

// =========================================================================
// Three+ arg double functions (three-stage path)
// =========================================================================

TEST_F(EtsCallEtsCppNumericArgsTest, AddThreeDouble_Basic)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "addThreeDouble", DBL_1_5, DBL_2_5, DBL_3_0);
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_7_0, 1e-9);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddFourDouble_Basic)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "addFourDouble", DBL_0_1, DBL_0_2, DBL_0_3, DBL_0_4);
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_1_0, 1e-9);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddFiveDouble_Basic)
{
    auto result =
        CallEtsFunction<double>(GetPackageName(), "addFiveDouble", DBL_1_1, DBL_2_2, DBL_3_3, DBL_4_4, DBL_5_5);
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_16_5, 1e-9);
}

// =========================================================================
// Mixed numeric type multi-arg functions (three-stage path)
// =========================================================================

TEST_F(EtsCallEtsCppNumericArgsTest, AddMixedNumericFour_Basic)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "addMixedNumericFour", static_cast<int32_t>(INT_10),
                                          static_cast<int64_t>(INT_20),
                                          DBL_0_5,  // float via double (MakeJsArg uses double)
                                          DBL_1_25);
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_31_75, 1e-6);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AddMixedNumericFive_Basic)
{
    // byte, short, int, long, double
    auto result = CallEtsFunction<double>(GetPackageName(), "addMixedNumericFive",
                                          static_cast<int32_t>(INT_1),  // byte
                                          static_cast<int32_t>(INT_2),  // short
                                          static_cast<int32_t>(INT_3),  // int
                                          static_cast<int64_t>(INT_4),  // long
                                          DBL_5_0);                     // double
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_15_0, 1e-6);
}

// =========================================================================
// Eight-arg functions (three-stage path: numNonRest=8)
// =========================================================================

TEST_F(EtsCallEtsCppNumericArgsTest, SumEightInt_Sequence)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "sumEightInt", INT_1, INT_2, INT_3, INT_4, INT_5, INT_6,
                                           INT_7, INT_8);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_36);
}

TEST_F(EtsCallEtsCppNumericArgsTest, SumEightInt_All_Ones)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "sumEightInt", INT_1, INT_1, INT_1, INT_1, INT_1, INT_1,
                                           INT_1, INT_1);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_8);
}

TEST_F(EtsCallEtsCppNumericArgsTest, SumEightInt_Mixed_Signs)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "sumEightInt", INT_100, -INT_200, INT_300, -INT_400,
                                           INT_500, -INT_600, INT_700, -INT_800);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), -INT_400);
}

TEST_F(EtsCallEtsCppNumericArgsTest, SumEightDouble_Sequence)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "sumEightDouble", DBL_1_0, DBL_2_0, DBL_3_0, DBL_4_0,
                                          DBL_5_0, DBL_6_0, DBL_7_0, DBL_8_0);
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_36_0, 1e-9);
}

TEST_F(EtsCallEtsCppNumericArgsTest, SumEightDouble_Fractional)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "sumEightDouble", DBL_0_1, DBL_0_2, DBL_0_3, DBL_0_4,
                                          DBL_0_5, DBL_0_6, DBL_0_7, DBL_0_8);
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_3_6, 1e-9);
}

// =========================================================================
// Sixteen-arg function (boundary: maxArgSlots=16 → three-stage)
// =========================================================================

TEST_F(EtsCallEtsCppNumericArgsTest, SumSixteenInt_Sequence)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "sumSixteenInt", INT_1, INT_2, INT_3, INT_4, INT_5, INT_6,
                                           INT_7, INT_8, INT_9, INT_10, INT_11, INT_12, INT_13, INT_14, INT_15, INT_16);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_136);
}

TEST_F(EtsCallEtsCppNumericArgsTest, SumSixteenInt_All_Ones)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "sumSixteenInt", INT_1, INT_1, INT_1, INT_1, INT_1, INT_1,
                                           INT_1, INT_1, INT_1, INT_1, INT_1, INT_1, INT_1, INT_1, INT_1, INT_1);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_16);
}

TEST_F(EtsCallEtsCppNumericArgsTest, SumSixteenInt_All_Tens)
{
    auto result =
        CallEtsFunction<int32_t>(GetPackageName(), "sumSixteenInt", INT_10, INT_10, INT_10, INT_10, INT_10, INT_10,
                                 INT_10, INT_10, INT_10, INT_10, INT_10, INT_10, INT_10, INT_10, INT_10, INT_10);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_160);
}

// =========================================================================
// Seventeen-arg function (exceeds maxArgSlots → ConvertArgsFallback)
// =========================================================================

TEST_F(EtsCallEtsCppNumericArgsTest, SumSeventeenInt_Sequence)
{
    auto result =
        CallEtsFunction<int32_t>(GetPackageName(), "sumSeventeenInt", INT_1, INT_2, INT_3, INT_4, INT_5, INT_6, INT_7,
                                 INT_8, INT_9, INT_10, INT_11, INT_12, INT_13, INT_14, INT_15, INT_16, INT_17);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_153);
}

TEST_F(EtsCallEtsCppNumericArgsTest, SumSeventeenInt_All_Ones)
{
    auto result =
        CallEtsFunction<int32_t>(GetPackageName(), "sumSeventeenInt", INT_1, INT_1, INT_1, INT_1, INT_1, INT_1, INT_1,
                                 INT_1, INT_1, INT_1, INT_1, INT_1, INT_1, INT_1, INT_1, INT_1, INT_1);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_17);
}

// =========================================================================
// Single-arg functions (fallback path: numNonRest<=1)
// =========================================================================

TEST_F(EtsCallEtsCppNumericArgsTest, IdentityInt_Basic)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "identityInt", INT_42);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_42);
}

TEST_F(EtsCallEtsCppNumericArgsTest, IdentityInt_Negative)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "identityInt", -INT_1);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), -INT_1);
}

TEST_F(EtsCallEtsCppNumericArgsTest, IdentityInt_Max)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "identityInt", INT_2147483647);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_2147483647);
}

TEST_F(EtsCallEtsCppNumericArgsTest, IdentityInt_Min)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "identityInt", INT32_MIN);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT32_MIN);
}

TEST_F(EtsCallEtsCppNumericArgsTest, IdentityLong_Basic)
{
    auto result = CallEtsFunction<int64_t>(GetPackageName(), "identityLong", static_cast<int64_t>(I64_9999999999));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), I64_9999999999);
}

TEST_F(EtsCallEtsCppNumericArgsTest, IdentityDouble_Basic)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "identityDouble", DBL_3_14159);
    ASSERT_TRUE(result.has_value());
    ASSERT_DOUBLE_EQ(result.value(), DBL_3_14159);
}

TEST_F(EtsCallEtsCppNumericArgsTest, IdentityDouble_Negative)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "identityDouble", -DBL_2_5);
    ASSERT_TRUE(result.has_value());
    ASSERT_DOUBLE_EQ(result.value(), -DBL_2_5);
}

TEST_F(EtsCallEtsCppNumericArgsTest, IdentityDouble_Zero)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "identityDouble", 0.0);
    ASSERT_TRUE(result.has_value());
    ASSERT_DOUBLE_EQ(result.value(), 0.0);
}

TEST_F(EtsCallEtsCppNumericArgsTest, NegateInt_Positive)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "negateInt", INT_42);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), -INT_42);
}

TEST_F(EtsCallEtsCppNumericArgsTest, NegateInt_Negative)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "negateInt", -INT_100);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), INT_100);
}

TEST_F(EtsCallEtsCppNumericArgsTest, NegateInt_Zero)
{
    auto result = CallEtsFunction<int32_t>(GetPackageName(), "negateInt", 0);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), 0);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AbsDouble_Positive)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "absDouble", DBL_3_14);
    ASSERT_TRUE(result.has_value());
    ASSERT_DOUBLE_EQ(result.value(), DBL_3_14);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AbsDouble_Negative)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "absDouble", -DBL_3_14);
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_3_14, 1e-9);
}

TEST_F(EtsCallEtsCppNumericArgsTest, AbsDouble_Zero)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "absDouble", 0.0);
    ASSERT_TRUE(result.has_value());
    ASSERT_DOUBLE_EQ(result.value(), 0.0);
}

// =========================================================================
// Varargs with fixed params (three-stage + ConvertRestParams)
// =========================================================================

TEST_F(EtsCallEtsCppNumericArgsTest, SumTwoFixedAndRest_ThreeRest)
{
    // 2 fixed int + 3 rest number → numNonRest=2 (three-stage) + rest packing
    auto result = CallEtsFunction<double>(GetPackageName(), "sumTwoFixedAndRest", static_cast<int32_t>(INT_1),
                                          static_cast<int32_t>(INT_2), DBL_3_0, DBL_4_0, DBL_5_0);
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_15_0, 1e-9);
}

TEST_F(EtsCallEtsCppNumericArgsTest, SumTwoFixedAndRest_ZeroRest)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "sumTwoFixedAndRest", static_cast<int32_t>(INT_10),
                                          static_cast<int32_t>(INT_20));
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_30_0, 1e-9);
}

TEST_F(EtsCallEtsCppNumericArgsTest, SumTwoFixedAndRest_OneRest)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "sumTwoFixedAndRest", static_cast<int32_t>(INT_100),
                                          static_cast<int32_t>(INT_200), DBL_300_0);
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_600_0, 1e-9);
}

TEST_F(EtsCallEtsCppNumericArgsTest, SumThreeFixedAndRest_TwoRest)
{
    // 3 fixed int + 2 rest number → numNonRest=3 (three-stage) + rest packing
    auto result = CallEtsFunction<double>(GetPackageName(), "sumThreeFixedAndRest", static_cast<int32_t>(INT_1),
                                          static_cast<int32_t>(INT_2), static_cast<int32_t>(INT_3), DBL_4_0, DBL_5_0);
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_15_0, 1e-9);
}

TEST_F(EtsCallEtsCppNumericArgsTest, SumThreeFixedAndRest_ZeroRest)
{
    auto result = CallEtsFunction<double>(GetPackageName(), "sumThreeFixedAndRest", static_cast<int32_t>(INT_10),
                                          static_cast<int32_t>(INT_20), static_cast<int32_t>(INT_30));
    ASSERT_TRUE(result.has_value());
    ASSERT_NEAR(result.value(), DBL_60_0, 1e-9);
}

}  // namespace ark::ets::interop::js::testing
