/**
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#include "helpers/helpers.h"
#include "helpers/helpers_runtime.h"

#include <gtest/gtest.h>

namespace libabckit::test {
namespace {
const auto *g_impl = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
const auto *g_modify = AbckitGetModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
const auto *g_graph = AbckitGetGraphApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
const auto *g_dynamic = AbckitGetIsaApiDynamicImpl(ABCKIT_VERSION_RELEASE_1_0_0);
constexpr auto INPUT = ABCKIT_ABC_DIR "ut/isa/isa_dynamic/iterators/closeiterator_dynamic.abc";
constexpr auto ENTRY = "closeiterator_dynamic";
constexpr auto EXPECTED = "99\n1\n99\n2\n99\n3\n99\n3\n";

void CheckClose(AbckitGraph *graph, AbckitBitImmSize width)
{
    auto *close = helpers::FindFirstInst(graph, ABCKIT_ISA_API_DYNAMIC_OPCODE_CLOSEITERATOR);
    ASSERT_NE(close, nullptr);
    EXPECT_EQ(g_graph->iGetInputCount(close), 1U);
    EXPECT_NE(g_graph->iGetInput(close, 0), nullptr);
    EXPECT_EQ(g_graph->iGetImmediateCount(close), 1U);
    EXPECT_EQ(g_graph->iGetImmediateSize(close, 0), width);
    if (width == BITSIZE_16) {
        EXPECT_GT(g_graph->iGetImmediate(close, 0), UINT8_MAX);
    }
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}
}  // namespace

class LibAbcKitCloseIteratorTest : public ::testing::Test {};

// Test: test-kind=api, api=InspectApiImpl::createGraphFromFunction, abc-kind=JS, category=positive
TEST_F(LibAbcKitCloseIteratorTest, AbruptCloseRoundtrip)
{
    constexpr auto OUTPUT = ABCKIT_ABC_DIR "ut/isa/isa_dynamic/iterators/closeiterator_roundtrip.abc";
    EXPECT_EQ(helpers::ExecuteDynamicAbc(INPUT, ENTRY), EXPECTED);
    helpers::TransformMethod(
        INPUT, OUTPUT, "closeOnThrow",
        [](AbckitFile *, AbckitCoreFunction *, AbckitGraph *graph) { CheckClose(graph, BITSIZE_8); },
        [](AbckitGraph *graph) { CheckClose(graph, BITSIZE_8); });
    EXPECT_EQ(helpers::ExecuteDynamicAbc(OUTPUT, ENTRY), EXPECTED);
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive
TEST_F(LibAbcKitCloseIteratorTest, WideIcCloseRoundtrip)
{
    constexpr auto WIDE = ABCKIT_ABC_DIR "ut/isa/isa_dynamic/iterators/closeiterator_wide.abc";
    constexpr auto OUTPUT = ABCKIT_ABC_DIR "ut/isa/isa_dynamic/iterators/closeiterator_wide_roundtrip.abc";
    helpers::TransformMethod(
        INPUT, WIDE, "closeOnThrow",
        [](AbckitFile *file, AbckitCoreFunction *, AbckitGraph *graph) {
            ASSERT_NO_FATAL_FAILURE(CheckClose(graph, BITSIZE_8));
            auto *close = helpers::FindFirstInst(graph, ABCKIT_ISA_API_DYNAMIC_OPCODE_CLOSEITERATOR);
            auto *iterator = g_graph->iGetInput(close, 0);
            constexpr char NEXT[] = "next";
            auto *name = g_modify->createString(file, NEXT, sizeof(NEXT) - 1);
            ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
            // Each property load consumes two IC slots. Force closeiterator past the 8-bit boundary.
            constexpr size_t LOAD_COUNT = 140;
            for (size_t index = 0; index < LOAD_COUNT; ++index) {
                auto *load = g_dynamic->iCreateLdobjbyname(graph, iterator, name);
                ASSERT_NE(load, nullptr);
                g_graph->iInsertBefore(load, close);
                ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
            }
        },
        [](AbckitGraph *graph) { CheckClose(graph, BITSIZE_16); });
    EXPECT_EQ(helpers::ExecuteDynamicAbc(WIDE, ENTRY), EXPECTED);

    // Reopen the emitted 16-bit instruction and exercise the decoder/codegen once more.
    helpers::TransformMethod(
        WIDE, OUTPUT, "closeOnThrow",
        [](AbckitFile *, AbckitCoreFunction *, AbckitGraph *graph) { CheckClose(graph, BITSIZE_16); },
        [](AbckitGraph *graph) { CheckClose(graph, BITSIZE_16); });
    EXPECT_EQ(helpers::ExecuteDynamicAbc(OUTPUT, ENTRY), EXPECTED);
}
}  // namespace libabckit::test
