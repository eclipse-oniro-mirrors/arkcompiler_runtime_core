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

#include <array>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "helpers/helpers.h"
#include "helpers/helpers_runtime.h"
#include "libabckit/c/abckit.h"
#include "libabckit/c/extensions/js/metadata_js.h"
#include "libabckit/c/ir_core.h"
#include "libabckit/c/isa/isa_dynamic.h"
#include "libabckit/c/metadata_core.h"
#include "libabckit/src/adapter_dynamic/metadata_modify_dynamic.h"

namespace libabckit::test {
namespace {
auto *g_api = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto *g_inspect = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto *g_modify = AbckitGetModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto *g_dyn = AbckitGetIsaApiDynamicImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto *g_graph = AbckitGetGraphApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto *g_jsInspect = AbckitGetJsInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto *g_jsModify = AbckitGetJsModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
constexpr auto INPUT = ABCKIT_ABC_DIR "ut/thread_safety/graph_versions/graph_versions.abc";
constexpr size_t WORKER_COUNT = 2;

std::string GetString(AbckitString *str)
{
    return str == nullptr ? std::string {} : g_inspect->abckitStringToString(str);
}
}  // namespace

class LibAbcKitDynamicGraphVersionTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        file_ = g_api->openAbc(INPUT, strlen(INPUT));
        ASSERT_NE(file_, nullptr);
        ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }

    void TearDown() override
    {
        DestroyGraphs();
        if (file_ != nullptr) {
            g_api->closeFile(file_);
            EXPECT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
        }
    }

    AbckitCoreFunction *Function(const char *name)
    {
        struct Search {
            const char *name;
            AbckitCoreFunction *function = nullptr;
        } search {name};
        g_inspect->moduleEnumerateTopLevelFunctions(Module(), &search, [](AbckitCoreFunction *function, void *data) {
            auto *query = static_cast<Search *>(data);
            if (GetString(g_inspect->functionGetName(function)) == query->name) {
                query->function = function;
            }
            return true;
        });
        EXPECT_NE(search.function, nullptr);
        return search.function;
    }

    AbckitGraph *Graph(AbckitCoreFunction *function)
    {
        auto *graph = g_inspect->createGraphFromFunction(function);
        EXPECT_NE(graph, nullptr);
        EXPECT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
        if (graph != nullptr) {
            graphs_.push_back(graph);
        }
        return graph;
    }

    AbckitCoreModule *Module()
    {
        helpers::ModuleByNameContext ctx {nullptr, "graph_versions"};
        g_inspect->fileEnumerateModules(file_, &ctx, helpers::ModuleByNameFinder);
        EXPECT_NE(ctx.module, nullptr);
        return ctx.module;
    }

    void DestroyGraphs()
    {
        for (auto *graph : graphs_) {
            g_api->destroyGraph(graph);
            EXPECT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
        }
        graphs_.clear();
    }

    void PersistAndReopen()
    {
        DestroyGraphs();
        std::string path = std::string(INPUT) + "." + ::testing::UnitTest::GetInstance()->current_test_info()->name();
        g_api->writeAbc(file_, path.c_str(), path.size());
        ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
        g_api->closeFile(file_);
        ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
        file_ = g_api->openAbc(path.c_str(), path.size());
        ASSERT_NE(file_, nullptr);
        ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }

    void ExpectIndex(AbckitGraph *graph, AbckitIsaApiDynamicOpcode opcode, uint64_t expected)
    {
        ASSERT_NE(graph, nullptr);
        auto *load = helpers::FindFirstInst(graph, opcode);
        ASSERT_NE(load, nullptr);
        EXPECT_EQ(g_graph->iGetImmediate(load, 0), expected);
        EXPECT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }

    void SetText(AbckitGraph *graph, const char *text)
    {
        ASSERT_NE(graph, nullptr);
        auto *load = helpers::FindFirstInst(graph, ABCKIT_ISA_API_DYNAMIC_OPCODE_LOADSTRING);
        ASSERT_NE(load, nullptr);
        auto *str = g_modify->createString(file_, text, strlen(text));
        ASSERT_NE(str, nullptr);
        g_graph->iSetString(load, str);
        ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }

    void ExpectText(const char *name, const char *expected)
    {
        auto *graph = Graph(Function(name));
        ASSERT_NE(graph, nullptr);
        auto *load = helpers::FindFirstInst(graph, ABCKIT_ISA_API_DYNAMIC_OPCODE_LOADSTRING);
        ASSERT_NE(load, nullptr);
        EXPECT_EQ(GetString(g_graph->iGetString(load)), expected);
    }

    AbckitCoreImportDescriptor *Import(const char *alias)
    {
        struct Search {
            const char *alias;
            AbckitCoreImportDescriptor *descriptor = nullptr;
        } search {alias};
        g_inspect->moduleEnumerateImports(Module(), &search, [](AbckitCoreImportDescriptor *id, void *data) {
            auto *query = static_cast<Search *>(data);
            if (GetString(g_inspect->importDescriptorGetAlias(id)) == query->alias) {
                query->descriptor = id;
            }
            return true;
        });
        EXPECT_NE(search.descriptor, nullptr);
        return search.descriptor;
    }

    void AddImportLoad(AbckitGraph *graph, bool wide = false)
    {
        auto *ret = helpers::FindFirstInst(graph, ABCKIT_ISA_API_DYNAMIC_OPCODE_RETURN);
        ASSERT_NE(ret, nullptr);
        auto *import = Import("usedImport");
        auto *load = wide ? g_dyn->iCreateWideLdexternalmodulevar(graph, import)
                          : g_dyn->iCreateLdexternalmodulevar(graph, import);
        ASSERT_NE(load, nullptr);
        g_graph->iInsertBefore(load, ret);
        g_graph->iSetInput(ret, load, 0);
        ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
        ASSERT_NO_FATAL_FAILURE(ExpectIndex(graph,
                                            wide ? ABCKIT_ISA_API_DYNAMIC_OPCODE_WIDE_LDEXTERNALMODULEVAR
                                                 : ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR,
                                            1));
    }

    void AddSendableImportLoad(AbckitGraph *graph, bool wide = false)
    {
        auto *ret = helpers::FindFirstInst(graph, ABCKIT_ISA_API_DYNAMIC_OPCODE_RETURN);
        ASSERT_NE(ret, nullptr);
        // The sendable module var creation API consumes a raw section index instead of an import
        // descriptor: `usedImport` sits at index 1 before the compaction.
        auto *load = wide ? g_dyn->iCreateCallruntimeWideldsendableexternalmodulevar(graph, 1)
                          : g_dyn->iCreateCallruntimeLdsendableexternalmodulevar(graph, 1);
        ASSERT_NE(load, nullptr);
        g_graph->iInsertBefore(load, ret);
        g_graph->iSetInput(ret, load, 0);
        ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
        ASSERT_NO_FATAL_FAILURE(
            ExpectIndex(graph,
                        wide ? ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLRUNTIME_WIDELDSENDABLEEXTERNALMODULEVAR
                             : ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLRUNTIME_LDSENDABLEEXTERNALMODULEVAR,
                        1));
    }

    void RemoveUnusedImport()
    {
        auto *removed = Import("unusedImport");
        ASSERT_NE(removed, nullptr);
        g_jsModify->moduleRemoveImport(g_jsInspect->coreModuleToJsModule(Module()),
                                       g_jsInspect->coreImportDescriptorToJsImportDescriptor(removed));
        ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }

    helpers::DebugTables ReadPersistedDebugTables()
    {
        std::string path = std::string(INPUT) + "." + ::testing::UnitTest::GetInstance()->current_test_info()->name();
        return helpers::ReadDynamicDebugTables(path, "debugLines");
    }

    void CheckCompactedGraph(AbckitCoreFunction *function, AbckitGraph *oldGraph, AbckitGraph *unaffected,
                             AbckitIsaApiDynamicOpcode opcode)
    {
        // Building another function emits the pending module changes and renumbers its indices.
        ASSERT_NE(Graph(Function("otherText")), nullptr);
        ASSERT_NO_FATAL_FAILURE(ExpectIndex(Graph(function), opcode, 0));
        g_modify->functionSetGraph(function, oldGraph);
        EXPECT_EQ(g_api->getLastError(), ABCKIT_STATUS_WRONG_STATE);
        ASSERT_NO_FATAL_FAILURE(ExpectIndex(Graph(function), opcode, 0));
        // A function without rewritten module indices remains usable on the owning thread.
        g_modify->functionSetGraph(Function("untouched"), unaffected);
        EXPECT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }

    AbckitFile *file_ = nullptr;
    std::vector<AbckitGraph *> graphs_;
};

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicGraphVersionTest, RejectNewImportReferenceAfterCompaction)
{
    auto *function = Function("untouched");
    auto *graph = Graph(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_NO_FATAL_FAILURE(AddImportLoad(graph));
    ASSERT_NO_FATAL_FAILURE(RemoveUnusedImport());
    ASSERT_NE(Graph(Function("otherText")), nullptr);
    g_modify->functionSetGraph(function, graph);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_WRONG_STATE);
    ASSERT_NO_FATAL_FAILURE(PersistAndReopen());
    EXPECT_EQ(helpers::FindFirstInst(Graph(Function("untouched")), ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR),
              nullptr);
    ASSERT_NO_FATAL_FAILURE(
        ExpectIndex(Graph(Function("readImport")), ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR, 0));
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicGraphVersionTest, RejectPreparedNewImportReferenceAfterCompaction)
{
    auto *function = Function("untouched");
    auto *graph = Graph(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_NO_FATAL_FAILURE(AddImportLoad(graph));
    ASSERT_NO_FATAL_FAILURE(RemoveUnusedImport());
    auto *prepared = PrepareFunctionSetGraphDynamic(function, graph);
    ASSERT_NE(prepared, nullptr);
    ASSERT_NE(Graph(Function("otherText")), nullptr);
    CommitPreparedFunctionDynamic(prepared);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_WRONG_STATE);
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicGraphVersionTest, RejectNewWideImportReferenceAfterCompaction)
{
    auto *function = Function("untouched");
    auto *graph = Graph(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_NO_FATAL_FAILURE(AddImportLoad(graph, true));
    ASSERT_NO_FATAL_FAILURE(RemoveUnusedImport());
    ASSERT_NE(Graph(Function("otherText")), nullptr);
    g_modify->functionSetGraph(function, graph);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_WRONG_STATE);
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicGraphVersionTest, PreparedGraphWithoutModuleIndicesSurvivesCompaction)
{
    auto *function = Function("untouched");
    auto *graph = Graph(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_NO_FATAL_FAILURE(RemoveUnusedImport());
    auto *prepared = PrepareFunctionSetGraphDynamic(function, graph);
    ASSERT_NE(prepared, nullptr);
    ASSERT_NE(Graph(Function("otherText")), nullptr);
    CommitPreparedFunctionDynamic(prepared);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NO_FATAL_FAILURE(PersistAndReopen());
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicGraphVersionTest, ImportAppendPreservesExistingGraphIndices)
{
    auto *function = Function("untouched");
    auto *graph = Graph(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_NO_FATAL_FAILURE(AddImportLoad(graph));
    auto *imported = g_inspect->importDescriptorGetImportedModule(Import("usedImport"));
    ASSERT_NE(imported, nullptr);
    AbckitJsImportFromDynamicModuleCreateParams params {"unusedImport", "appendedImport"};
    ASSERT_NE(g_jsModify->moduleAddImportFromJsToJs(g_jsInspect->coreModuleToJsModule(Module()),
                                                    g_jsInspect->coreModuleToJsModule(imported), &params),
              nullptr);
    ASSERT_NE(Graph(Function("otherText")), nullptr);
    g_modify->functionSetGraph(function, graph);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NO_FATAL_FAILURE(PersistAndReopen());
    ASSERT_NO_FATAL_FAILURE(
        ExpectIndex(Graph(Function("untouched")), ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR, 1));
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicGraphVersionTest, RepeatedCommitPreservesPersistedDebugLinesAndColumns)
{
    auto *function = Function("debugLines");
    auto *graph = Graph(function);
    ASSERT_NE(graph, nullptr);
    g_modify->functionSetGraph(function, graph);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NO_FATAL_FAILURE(PersistAndReopen());
    auto once = ReadPersistedDebugTables();
    ASSERT_GT(once.first.size(), 2U);
    ASSERT_GT(once.second.size(), 2U);

    g_api->closeFile(file_);
    file_ = g_api->openAbc(INPUT, strlen(INPUT));
    ASSERT_NE(file_, nullptr);
    function = Function("debugLines");
    graph = Graph(function);
    ASSERT_NE(graph, nullptr);
    constexpr size_t COMMIT_COUNT = 2;
    for (size_t i = 0; i < COMMIT_COUNT; ++i) {
        g_modify->functionSetGraph(function, graph);
        ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }
    ASSERT_NO_FATAL_FAILURE(PersistAndReopen());
    auto twice = ReadPersistedDebugTables();
    EXPECT_EQ(once.first, twice.first);
    EXPECT_EQ(once.second, twice.second);
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicGraphVersionTest, RejectGraphAfterImportIndexCompaction)
{
    auto *function = Function("readImport");
    auto *oldGraph = Graph(function);
    ASSERT_NO_FATAL_FAILURE(ExpectIndex(oldGraph, ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR, 1));
    auto *unaffected = Graph(Function("untouched"));
    ASSERT_NE(unaffected, nullptr);
    auto *module = Module();
    ASSERT_NE(module, nullptr);
    AbckitCoreImportDescriptor *removed = nullptr;
    g_inspect->moduleEnumerateImports(module, &removed, [](AbckitCoreImportDescriptor *id, void *data) {
        if (GetString(g_inspect->importDescriptorGetAlias(id)) == "unusedImport") {
            *static_cast<AbckitCoreImportDescriptor **>(data) = id;
        }
        return true;
    });
    ASSERT_NE(removed, nullptr);
    g_jsModify->moduleRemoveImport(g_jsInspect->coreModuleToJsModule(module),
                                   g_jsInspect->coreImportDescriptorToJsImportDescriptor(removed));
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NO_FATAL_FAILURE(
        CheckCompactedGraph(function, oldGraph, unaffected, ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR));
    ASSERT_NO_FATAL_FAILURE(PersistAndReopen());
    ASSERT_NO_FATAL_FAILURE(
        ExpectIndex(Graph(Function("readImport")), ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR, 0));
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicGraphVersionTest, RejectGraphAfterExportIndexCompaction)
{
    // Remove the unused export's initializer store before removing its metadata.
    auto *main = Function("func_main_0");
    auto *mainGraph = Graph(main);
    ASSERT_NE(mainGraph, nullptr);
    auto *store = helpers::FindFirstInst(mainGraph, ABCKIT_ISA_API_DYNAMIC_OPCODE_STMODULEVAR);
    ASSERT_NE(store, nullptr);
    ASSERT_EQ(g_graph->iGetImmediate(store, 0), 0U);
    g_graph->iRemove(store);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_modify->functionSetGraph(main, mainGraph);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);

    auto *function = Function("readExport");
    auto *oldGraph = Graph(function);
    ASSERT_NO_FATAL_FAILURE(ExpectIndex(oldGraph, ABCKIT_ISA_API_DYNAMIC_OPCODE_LDLOCALMODULEVAR, 1));
    auto *unaffected = Graph(Function("untouched"));
    ASSERT_NE(unaffected, nullptr);
    auto *module = Module();
    ASSERT_NE(module, nullptr);
    AbckitCoreExportDescriptor *removed = nullptr;
    g_inspect->moduleEnumerateExports(module, &removed, [](AbckitCoreExportDescriptor *ed, void *data) {
        if (GetString(g_inspect->exportDescriptorGetName(ed)) == "unusedExport") {
            *static_cast<AbckitCoreExportDescriptor **>(data) = ed;
        }
        return true;
    });
    ASSERT_NE(removed, nullptr);
    g_jsModify->moduleRemoveExport(g_jsInspect->coreModuleToJsModule(module),
                                   g_jsInspect->coreExportDescriptorToJsExportDescriptor(removed));
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NO_FATAL_FAILURE(
        CheckCompactedGraph(function, oldGraph, unaffected, ABCKIT_ISA_API_DYNAMIC_OPCODE_LDLOCALMODULEVAR));
    ASSERT_NO_FATAL_FAILURE(PersistAndReopen());
    ASSERT_NO_FATAL_FAILURE(
        ExpectIndex(Graph(Function("readExport")), ABCKIT_ISA_API_DYNAMIC_OPCODE_LDLOCALMODULEVAR, 0));
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicGraphVersionTest, CompactionRemapsSendableExternalModuleVarIndex)
{
    // Pin the fixture layout: `usedImport` is the regular import at index 1.
    ASSERT_NO_FATAL_FAILURE(
        ExpectIndex(Graph(Function("readImport")), ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR, 1));
    auto *function = Function("untouched");
    auto *graph = Graph(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_NO_FATAL_FAILURE(AddSendableImportLoad(graph));
    g_modify->functionSetGraph(function, graph);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NO_FATAL_FAILURE(RemoveUnusedImport());
    ASSERT_NE(Graph(Function("otherText")), nullptr);
    ASSERT_NO_FATAL_FAILURE(
        ExpectIndex(Graph(function), ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLRUNTIME_LDSENDABLEEXTERNALMODULEVAR, 0));
    ASSERT_NO_FATAL_FAILURE(PersistAndReopen());
    ASSERT_NO_FATAL_FAILURE(ExpectIndex(Graph(Function("untouched")),
                                        ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLRUNTIME_LDSENDABLEEXTERNALMODULEVAR, 0));
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicGraphVersionTest, CompactionRemapsWideSendableExternalModuleVarIndex)
{
    // Pin the fixture layout: `usedImport` is the regular import at index 1.
    ASSERT_NO_FATAL_FAILURE(
        ExpectIndex(Graph(Function("readImport")), ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR, 1));
    auto *function = Function("untouched");
    auto *graph = Graph(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_NO_FATAL_FAILURE(AddSendableImportLoad(graph, true));
    g_modify->functionSetGraph(function, graph);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NO_FATAL_FAILURE(RemoveUnusedImport());
    ASSERT_NE(Graph(Function("otherText")), nullptr);
    ASSERT_NO_FATAL_FAILURE(
        ExpectIndex(Graph(function), ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLRUNTIME_WIDELDSENDABLEEXTERNALMODULEVAR, 0));
    ASSERT_NO_FATAL_FAILURE(PersistAndReopen());
    ASSERT_NO_FATAL_FAILURE(ExpectIndex(Graph(Function("untouched")),
                                        ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLRUNTIME_WIDELDSENDABLEEXTERNALMODULEVAR, 0));
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicGraphVersionTest, SameGraphCanCommitSuccessiveEdits)
{
    auto *function = Function("readText");
    auto *graph = Graph(function);
    auto *staleGraph = Graph(function);
    ASSERT_NE(staleGraph, nullptr);
    ASSERT_NO_FATAL_FAILURE(SetText(graph, "first"));
    g_modify->functionSetGraph(function, graph);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NO_FATAL_FAILURE(SetText(graph, "second"));
    g_modify->functionSetGraph(function, graph);
    EXPECT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_modify->functionSetGraph(function, staleGraph);
    EXPECT_EQ(g_api->getLastError(), ABCKIT_STATUS_WRONG_STATE);
    ASSERT_NO_FATAL_FAILURE(PersistAndReopen());
    ASSERT_NO_FATAL_FAILURE(ExpectText("readText", "second"));
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicGraphVersionTest, ReusedGraphRejectsAnotherGraphCommit)
{
    auto *function = Function("readText");
    auto *graph = Graph(function);
    ASSERT_NO_FATAL_FAILURE(SetText(graph, "first"));
    g_modify->functionSetGraph(function, graph);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto *newerGraph = Graph(function);
    ASSERT_NO_FATAL_FAILURE(SetText(newerGraph, "newer"));
    g_modify->functionSetGraph(function, newerGraph);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NO_FATAL_FAILURE(SetText(graph, "stale"));
    g_modify->functionSetGraph(function, graph);
    EXPECT_EQ(g_api->getLastError(), ABCKIT_STATUS_WRONG_STATE);
    ASSERT_NO_FATAL_FAILURE(PersistAndReopen());
    ASSERT_NO_FATAL_FAILURE(ExpectText("readText", "newer"));
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicGraphVersionTest, PreparedAfterPublicCommitSurvivesGraphDestruction)
{
    auto *function = Function("readText");
    auto *graph = Graph(function);
    ASSERT_NO_FATAL_FAILURE(SetText(graph, "first"));
    g_modify->functionSetGraph(function, graph);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NO_FATAL_FAILURE(SetText(graph, "prepared"));
    auto *prepared = PrepareFunctionSetGraphDynamic(function, graph);
    ASSERT_NE(prepared, nullptr);
    DestroyGraphs();
    CommitPreparedFunctionDynamic(prepared);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NO_FATAL_FAILURE(PersistAndReopen());
    ASSERT_NO_FATAL_FAILURE(ExpectText("readText", "prepared"));
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicGraphVersionTest, DifferentFunctionsCanCommitRepeatedlyInParallel)
{
    std::array<const char *, WORKER_COUNT> names {"readText", "otherText"};
    std::array<AbckitCoreFunction *, WORKER_COUNT> functions {};
    std::array<AbckitGraph *, WORKER_COUNT> graphs {};
    std::array<AbckitInst *, WORKER_COUNT> loads {};
    for (size_t i = 0; i < WORKER_COUNT; ++i) {
        functions[i] = Function(names[i]);
        graphs[i] = Graph(functions[i]);
        ASSERT_NE(graphs[i], nullptr);
        loads[i] = helpers::FindFirstInst(graphs[i], ABCKIT_ISA_API_DYNAMIC_OPCODE_LOADSTRING);
        ASSERT_NE(loads[i], nullptr);
    }
    auto *first = g_modify->createString(file_, "first", strlen("first"));
    auto *second = g_modify->createString(file_, "second", strlen("second"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    std::atomic<bool> failed {false};
    std::vector<std::thread> threads;
    for (size_t i = 0; i < WORKER_COUNT; ++i) {
        threads.emplace_back([&functions, &graphs, &loads, first, second, &failed, i]() {
            for (auto *text : {first, second}) {
                g_graph->iSetString(loads[i], text);
                if (g_api->getLastError() != ABCKIT_STATUS_NO_ERROR) {
                    failed.store(true);
                }
                g_modify->functionSetGraph(functions[i], graphs[i]);
                if (g_api->getLastError() != ABCKIT_STATUS_NO_ERROR) {
                    failed.store(true);
                }
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }
    EXPECT_FALSE(failed.load());
    ASSERT_NO_FATAL_FAILURE(PersistAndReopen());
    for (auto *name : names) {
        ASSERT_NO_FATAL_FAILURE(ExpectText(name, "second"));
    }
}
}  // namespace libabckit::test
