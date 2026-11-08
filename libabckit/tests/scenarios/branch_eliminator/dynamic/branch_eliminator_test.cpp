/**
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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

#include <sstream>
#include <iostream>

#include <gtest/gtest.h>

#include "libabckit/c/abckit.h"
#include "libabckit/c/isa/isa_dynamic.h"
#include "helpers/helpers_runtime.h"

#include "branch_eliminator.h"
#include "helpers/helpers.h"

namespace libabckit::test {

static constexpr auto VERSION = ABCKIT_VERSION_RELEASE_1_0_0;
static const AbckitApi *g_impl = AbckitGetApiImpl(VERSION);
static const AbckitInspectApi *g_implI = AbckitGetInspectApiImpl(VERSION);
static const AbckitGraphApi *g_implG = AbckitGetGraphApiImpl(VERSION);
static const AbckitIsaApiDynamic *g_dynG = AbckitGetIsaApiDynamicImpl(VERSION);

static bool MethodHasBranch(const std::string &moduleName, const std::string &methodName, AbckitFile *file)
{
    EXPECT_NE(file, nullptr);
    EXPECT_NE(g_impl, nullptr);
    EXPECT_NE(g_implI, nullptr);
    EXPECT_NE(g_implG, nullptr);
    EXPECT_NE(g_dynG, nullptr);
    VisitHelper vh = VisitHelper(file, g_impl, g_implI, g_implG, g_dynG);
    bool found = false;
    bool ret = false;
    vh.EnumerateModules([&](AbckitCoreModule *mod) {
        if (vh.GetString(g_implI->moduleGetName(mod)) != moduleName) {
            return;
        }
        vh.EnumerateModuleFunctions(mod, [&](AbckitCoreFunction *func) {
            if (found || vh.GetString(g_implI->functionGetName(func)) != methodName) {
                return;
            }
            found = true;
            auto *graph = g_implI->createGraphFromFunction(func);
            auto *ifInst = vh.GraphInstsFindIf(
                graph, [&](AbckitInst *inst) { return g_dynG->iGetOpcode(inst) == ABCKIT_ISA_API_DYNAMIC_OPCODE_IF; });
            ret = ifInst != nullptr;
            g_impl->destroyGraph(graph);
        });
    });

    EXPECT_TRUE(found);
    return ret;
}

class AbckitScenarioTest : public ::testing::Test {};

static const std::string DIR = std::string(ABCKIT_ABC_DIR "scenarios/branch_eliminator/dynamic/");
static const std::string INPUT = DIR + "branch_eliminator.abc";
static constexpr bool CONFIG_IS_DEBUG_ORIGIN_VALUE = false;

class AbckitBranchEliminatorFailureTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        graphCalls_ = 0;
        nameCalls_ = 0;
        file_ = g_impl->openAbc(INPUT.c_str(), INPUT.size());
        ASSERT_NE(file_, nullptr);
    }

    void TearDown() override
    {
        if (file_ != nullptr) {
            g_impl->closeFile(file_);
            EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
        }
    }

    void InjectFailure(BranchEliminator &runner, bool duringCollection)
    {
        // Keep the real API table unchanged; only this runner sees the injected failure.
        inspect_.createGraphFromFunction = [](AbckitCoreFunction *function) {
            ++graphCalls_;
            return g_implI->createGraphFromFunction(graphCalls_ == 1 ? nullptr : function);
        };
        if (duringCollection) {
            inspect_.functionGetName = [](AbckitCoreFunction *function) {
                ++nameCalls_;
                return g_implI->functionGetName(nameCalls_ > 1 ? nullptr : function);
            };
        }
        runner.implI_ = &inspect_;
    }

    AbckitFile *file_ = nullptr;
    inline static size_t graphCalls_ = 0;

private:
    AbckitInspectApi inspect_ {*g_implI};
    inline static size_t nameCalls_ = 0;
};

// Test: test-kind=scenario, abc-kind=ArkTS1, category=negative, extension=c
TEST_F(AbckitBranchEliminatorFailureTest, RunStopsAfterTaskFailure)
{
    BranchEliminator runner(VERSION, file_, {{"modules/config", "Config", "isDebug", true}});
    ASSERT_GT(runner.CollectTasks().size(), 1U);
    ASSERT_EQ(runner.GetStatus(), ABCKIT_STATUS_NO_ERROR);
    InjectFailure(runner, false);

    runner.Run();

    EXPECT_EQ(runner.GetStatus(), ABCKIT_STATUS_BAD_ARGUMENT);
    EXPECT_EQ(graphCalls_, 1U);
}

// Test: test-kind=scenario, abc-kind=ArkTS1, category=negative, extension=c
TEST_F(AbckitBranchEliminatorFailureTest, RunStopsAfterCollectionFailure)
{
    BranchEliminator runner(VERSION, file_, {{"modules/config", "Config", "isDebug", true}});
    ASSERT_GT(runner.CollectTasks().size(), 1U);
    ASSERT_EQ(runner.GetStatus(), ABCKIT_STATUS_NO_ERROR);
    InjectFailure(runner, true);

    runner.Run();

    EXPECT_EQ(runner.GetStatus(), ABCKIT_STATUS_BAD_ARGUMENT);
    EXPECT_EQ(graphCalls_, 0U);
}

/*
 * @param value: the value of isDebug when we handle, it can be different from configIsDebugOriginValue
 */
static std::string GetExpectOutput(bool value)
{
    std::stringstream expectOutput;
    expectOutput << std::boolalpha;
    expectOutput << "Config.isDebug = " << value << std::endl;
    expectOutput << "myfoo: Config.isDebug is " << value << std::endl;
    expectOutput << "Mybar.test1: Config.isDebug is " << value << std::endl;
    expectOutput << "Mybar.test2: Config.isDebug is " << value << std::endl;
    return expectOutput.str();
}

static void GeneralBranchEliminatorTest(bool configIsDebugFinal)
{
    auto *impl = AbckitGetApiImpl(VERSION);
    const auto originOutput = helpers::ExecuteDynamicAbc(INPUT, "branch_eliminator");

    auto expectOutput = GetExpectOutput(CONFIG_IS_DEBUG_ORIGIN_VALUE);
    ASSERT_EQ(originOutput, expectOutput);

    AbckitFile *file = impl->openAbc(INPUT.c_str(), INPUT.size());
    ASSERT_NE(file, nullptr);
    ASSERT_TRUE(MethodHasBranch("modules/myfoo", "myfoo", file));
    ASSERT_TRUE(MethodHasBranch("modules/mybar", "test1", file));
    ASSERT_TRUE(MethodHasBranch("modules/mybar", "test2", file));
    // Delete true branch
    const std::vector<ConstantInfo> infos = {{"modules/config", "Config", "isDebug", configIsDebugFinal}};
    BranchEliminator b(VERSION, file, infos);
    b.Run();
    ASSERT_FALSE(MethodHasBranch("modules/myfoo", "myfoo", file));
    ASSERT_FALSE(MethodHasBranch("modules/mybar", "test1", file));
    ASSERT_FALSE(MethodHasBranch("modules/mybar", "test2", file));

    const auto modifiedPath = DIR + "branch_eliminator_modified.abc";
    impl->writeAbc(file, modifiedPath.c_str(), modifiedPath.size());
    g_impl->closeFile(file);
    auto output = helpers::ExecuteDynamicAbc(modifiedPath, "branch_eliminator");
    auto expectOutput2 = GetExpectOutput(configIsDebugFinal);
    ASSERT_EQ(output, expectOutput2);
}

// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(AbckitScenarioTest, LibAbcKitTestBranchEliminatorDynamic1)
{
    GeneralBranchEliminatorTest(false);
}

// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(AbckitScenarioTest, LibAbcKitTestBranchEliminatorDynamic2)
{
    GeneralBranchEliminatorTest(true);
}

}  // namespace libabckit::test
