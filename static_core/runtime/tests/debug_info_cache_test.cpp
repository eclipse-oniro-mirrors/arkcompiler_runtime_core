/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

#include "assembly-parser.h"
#include "assembly-emitter.h"
#include "mangling.h"
#include "libarkbase/utils/utf.h"
#include "runtime/entrypoints/entrypoints.h"
#include "runtime/include/runtime.h"
#include "runtime/include/method.h"
#include "runtime/tooling/debugger.h"
#include "runtime/tooling/inspector/debugger/debug_info_cache.h"

namespace ark::test {

class DebugInfoCacheTest : public testing::Test {
public:
    DebugInfoCacheTest()
    {
        RuntimeOptions options;
        options.SetShouldLoadBootPandaFiles(false);
        options.SetShouldInitializeIntrinsics(false);
        options.SetGcType("epsilon");
        Runtime::Create(options);
        thread_ = ark::MTManagedThread::GetCurrent();
        thread_->ManagedCodeBegin();
    }

    ~DebugInfoCacheTest() override
    {
        thread_->ManagedCodeEnd();
        Runtime::Destroy();
    }

    NO_COPY_SEMANTIC(DebugInfoCacheTest);
    NO_MOVE_SEMANTIC(DebugInfoCacheTest);

private:
    ark::MTManagedThread *thread_;
};

static constexpr size_t BYTECODE_OFFSET_WITHIN_RANGE = 1;
static constexpr uint32_t LOCAL_VARIABLE_LENGTH = 10U;

struct MethodInfo {
    std::unique_ptr<const panda_file::File> filePtr;
    panda_file::File::EntityId methodId;
    panda_file::File::EntityId codeId;
    uint32_t nargs;
    uint32_t nregs;
};

static std::optional<MethodInfo> ParseAndGetMethodInfo(const std::string &source, const std::string &srcFilename)
{
    pandasm::Parser p;
    auto res = p.Parse(source, srcFilename);
    if (!res) {
        return std::nullopt;
    }
    auto filePtr = pandasm::AsmEmitter::Emit(res.Value());
    if (filePtr == nullptr) {
        return std::nullopt;
    }
    PandaString descriptor;
    auto classId = filePtr->GetClassId(ClassHelper::GetDescriptor(utf::CStringAsMutf8("_GLOBAL"), &descriptor));
    if (!classId.IsValid()) {
        return std::nullopt;
    }
    panda_file::ClassDataAccessor cda(*filePtr, classId);
    panda_file::File::EntityId methodId;
    panda_file::File::EntityId codeId;
    cda.EnumerateMethods([&](panda_file::MethodDataAccessor &mda) {
        methodId = mda.GetMethodId();
        if (mda.GetCodeId()) {
            codeId = *mda.GetCodeId();
        }
    });
    panda_file::CodeDataAccessor codeDataAccessor(*filePtr, codeId);
    return MethodInfo {std::move(filePtr), methodId, codeId, codeDataAccessor.GetNumArgs(),
                       codeDataAccessor.GetNumVregs()};
}

static std::optional<MethodInfo> GetMethodInfoFromFile(std::unique_ptr<const panda_file::File> filePtr)
{
    if (filePtr == nullptr) {
        return std::nullopt;
    }
    PandaString descriptor;
    auto classId = filePtr->GetClassId(ClassHelper::GetDescriptor(utf::CStringAsMutf8("_GLOBAL"), &descriptor));
    if (!classId.IsValid()) {
        return std::nullopt;
    }
    panda_file::ClassDataAccessor cda(*filePtr, classId);
    panda_file::File::EntityId methodId;
    panda_file::File::EntityId codeId;
    cda.EnumerateMethods([&](panda_file::MethodDataAccessor &mda) {
        methodId = mda.GetMethodId();
        if (mda.GetCodeId()) {
            codeId = *mda.GetCodeId();
        }
    });
    panda_file::CodeDataAccessor codeDataAccessor(*filePtr, codeId);
    return MethodInfo {std::move(filePtr), methodId, codeId, codeDataAccessor.GetNumArgs(),
                       codeDataAccessor.GetNumVregs()};
}

static ark::Frame *AllocFrame(Method &method, uint32_t numVregs)
{
    uint32_t extSz = EMPTY_EXT_FRAME_DATA_SIZE;
    void *mem = aligned_alloc(8, ark::Frame::GetAllocSize(ark::Frame::GetActualSize<false>(numVregs), extSz));
    if (mem == nullptr) {
        return nullptr;
    }
    return new (ark::Frame::FromExt(mem, extSz)) ark::Frame(mem, &method, nullptr, numVregs);
}

TEST_F(DebugInfoCacheTest, GetLocalRegNumber_FindsParameterByName)
{
    auto source = R"(
        .function void foo(i32 a0, i32 a1) {
            movi v0, 0
            movi v1, 1
            return.void
        }
    )";

    auto mi = ParseAndGetMethodInfo(source, "test_param.pa");
    ASSERT_TRUE(mi.has_value());
    ark::tooling::inspector::DebugInfoCache cache;
    cache.AddPandaFile(*mi->filePtr, true);
    Method method(nullptr, mi->filePtr.get(), mi->methodId, mi->codeId, 0, mi->nargs, nullptr);
    auto *frame = AllocFrame(method, mi->nregs + mi->nargs);
    ASSERT_NE(frame, nullptr);
    frame->SetBytecodeOffset(BYTECODE_OFFSET_WITHIN_RANGE);
    ark::tooling::PtDebugFrame ptFrame(&method, frame);

    auto regInfo = cache.GetLocalRegNumber(ptFrame, "a0");
    ASSERT_TRUE(regInfo.has_value());
    EXPECT_EQ(regInfo->regNumber, 0);
    regInfo = cache.GetLocalRegNumber(ptFrame, "a1");
    ASSERT_TRUE(regInfo.has_value());
    EXPECT_EQ(regInfo->regNumber, 1);
    regInfo = cache.GetLocalRegNumber(ptFrame, "a2");
    ASSERT_FALSE(regInfo.has_value());

    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
    std::free(frame->GetExt());
}

TEST_F(DebugInfoCacheTest, GetLocalRegNumber_FindsLocalVariableByName)
{
    pandasm::Parser p;
    auto source = R"(
        .function void foo(i32 a0) {
            movi v0, 0
            movi v1, 1
            return.void
        }
    )";
    auto res = p.Parse(source, "test_local.pa");
    ASSERT_TRUE(res);
    auto &prog = res.Value();
    std::vector<pandasm::Function::Parameter> params;
    params.emplace_back(pandasm::Type("i32", 0), ark::panda_file::SourceLang::PANDA_ASSEMBLY);
    auto &function = prog.functionStaticTable.at(pandasm::GetFunctionSignatureFromName("foo", params));
    pandasm::debuginfo::LocalVariable lv;
    lv.name = "localX";
    lv.signature = "I";
    lv.reg = 0;
    lv.start = 0;
    lv.length = LOCAL_VARIABLE_LENGTH;
    function.localVariableDebug.push_back(lv);
    lv.name = "localY";
    lv.signature = "F";
    lv.reg = 1;
    function.localVariableDebug.push_back(lv);
    auto mi = GetMethodInfoFromFile(pandasm::AsmEmitter::Emit(res.Value()));
    ASSERT_TRUE(mi.has_value());
    ark::tooling::inspector::DebugInfoCache cache;
    cache.AddPandaFile(*mi->filePtr, true);
    Method method(nullptr, mi->filePtr.get(), mi->methodId, mi->codeId, 0, mi->nargs, nullptr);
    auto *frame = AllocFrame(method, mi->nregs + mi->nargs);
    ASSERT_NE(frame, nullptr);
    frame->SetBytecodeOffset(BYTECODE_OFFSET_WITHIN_RANGE);
    ark::tooling::PtDebugFrame ptFrame(&method, frame);
    auto regInfo = cache.GetLocalRegNumber(ptFrame, "localX");
    ASSERT_TRUE(regInfo.has_value());
    EXPECT_EQ(regInfo->regNumber, 0);
    regInfo = cache.GetLocalRegNumber(ptFrame, "localY");
    ASSERT_TRUE(regInfo.has_value());
    EXPECT_EQ(regInfo->regNumber, 1);
    regInfo = cache.GetLocalRegNumber(ptFrame, "nonexistent");
    ASSERT_FALSE(regInfo.has_value());
    regInfo = cache.GetLocalRegNumber(ptFrame, "a0");
    ASSERT_TRUE(regInfo.has_value());
    EXPECT_EQ(regInfo->regNumber, 0);
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
    std::free(frame->GetExt());
}

TEST_F(DebugInfoCacheTest, GetLocalRegNumber_VariableOutOfRangeReturnsNullOpt)
{
    pandasm::Parser p;
    auto source = R"(
        .function void foo() {
            movi v0, 0
            movi v1, 1
            movi v2, 2
            return.void
        }
    )";
    auto res = p.Parse(source, "test_range.pa");
    ASSERT_TRUE(res);
    auto &function = res.Value().functionStaticTable.at(pandasm::GetFunctionSignatureFromName("foo", {}));
    pandasm::debuginfo::LocalVariable lv;
    lv.name = "outOfRange";
    lv.signature = "I";
    lv.reg = 0;
    lv.start = 1U;
    lv.length = 2U;
    function.localVariableDebug.push_back(lv);
    auto mi = GetMethodInfoFromFile(pandasm::AsmEmitter::Emit(res.Value()));
    ASSERT_TRUE(mi.has_value());
    ark::tooling::inspector::DebugInfoCache cache;
    cache.AddPandaFile(*mi->filePtr, true);
    Method method(nullptr, mi->filePtr.get(), mi->methodId, mi->codeId, 0, mi->nargs, nullptr);
    auto *frame = AllocFrame(method, mi->nargs);
    ASSERT_NE(frame, nullptr);
    frame->SetBytecodeOffset(0);
    ark::tooling::PtDebugFrame ptFrame(&method, frame);
    auto regInfo = cache.GetLocalRegNumber(ptFrame, "outOfRange");
    ASSERT_FALSE(regInfo.has_value());
    // Offset 4 is within variable range (startOffset=2, endOffset=6)
    frame->SetBytecodeOffset(4U);
    ark::tooling::PtDebugFrame ptFrame2(&method, frame);
    regInfo = cache.GetLocalRegNumber(ptFrame2, "outOfRange");
    ASSERT_TRUE(regInfo.has_value());
    EXPECT_EQ(regInfo->regNumber, 0);
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
    std::free(frame->GetExt());
}

TEST_F(DebugInfoCacheTest, GetLocalRegNumber_NoDebugInfoReturnsNullOpt)
{
    auto source = R"(
        .function void foo() {
            return.void
        }
    )";
    auto mi = ParseAndGetMethodInfo(source, "test_noinfo.pa");
    ASSERT_TRUE(mi.has_value());
    // Do NOT call AddPandaFile - cache has no entry for this file
    ark::tooling::inspector::DebugInfoCache cache;
    Method method(nullptr, mi->filePtr.get(), mi->methodId, mi->codeId, 0, mi->nargs, nullptr);
    auto *frame = AllocFrame(method, mi->nargs);
    ASSERT_NE(frame, nullptr);
    frame->SetBytecodeOffset(0);
    ark::tooling::PtDebugFrame ptFrame(&method, frame);
    auto regInfo = cache.GetLocalRegNumber(ptFrame, "anything");
    ASSERT_FALSE(regInfo.has_value());
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
    std::free(frame->GetExt());
}

}  // namespace ark::test
