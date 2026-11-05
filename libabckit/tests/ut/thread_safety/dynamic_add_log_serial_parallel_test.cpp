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

// 20260807927972 Task 02-04: add_log, serial versus four workers on distinct functions.
// Each worker owns its graph; committed graphs are destroyed before file persistence.

#include "libabckit/c/abckit.h"
#include "libabckit/c/ir_core.h"
#include "libabckit/c/metadata_core.h"
#include "libabckit/c/statuses.h"
#include "libabckit/c/isa/isa_dynamic.h"

#include "helpers/helpers.h"
#include "helpers/helpers_runtime.h"

#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <cstring>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// NOLINTBEGIN(readability-magic-numbers)

namespace libabckit::test {

namespace {
auto g_impl = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implI = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implM = AbckitGetModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implG = AbckitGetGraphApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_dynG = AbckitGetIsaApiDynamicImpl(ABCKIT_VERSION_RELEASE_1_0_0);

constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "ut/thread_safety/add_log_parallel/add_log_parallel_dynamic.abc";
constexpr auto SERIAL_OUT_PATH = ABCKIT_ABC_DIR "ut/thread_safety/add_log_parallel/add_log_parallel_dynamic_serial.abc";
constexpr auto PARALLEL_OUT_PATH =
    ABCKIT_ABC_DIR "ut/thread_safety/add_log_parallel/add_log_parallel_dynamic_parallel.abc";
constexpr auto ENTRY = "add_log_parallel_dynamic";

// Number of handle functions / parallel workers exercised by this scenario.
constexpr int K_WORKER_COUNT = 4;

struct UserData {
    AbckitString *print = nullptr;
    AbckitString *date = nullptr;
    AbckitString *getTime = nullptr;
    AbckitString *str = nullptr;
    AbckitString *consume = nullptr;
};

struct VisitData {
    UserData *ud = nullptr;
    AbckitInst *time = nullptr;
    bool ok = true;
};

// Build the date-object timestamp portion of the epilog (dateClass, dateObj, getTime, time2)
// inserted around the RETURNUNDEFINED instruction. Returns the epilog timestamp (nullptr on
// failure).
static AbckitInst *AppendEpilogTimestamp(AbckitGraph *graph, AbckitInst *inst, UserData *ud)
{
    auto *dateClass2 = g_dynG->iCreateTryldglobalbyname(graph, ud->date);
    if (dateClass2 == nullptr) {
        return nullptr;
    }
    g_implG->iInsertBefore(dateClass2, inst);
    auto *dateObj2 = g_dynG->iCreateNewobjrange(graph, 1, dateClass2);
    if (dateObj2 == nullptr) {
        return nullptr;
    }
    g_implG->iInsertAfter(dateObj2, dateClass2);
    auto *getTime2 = g_dynG->iCreateLdobjbyname(graph, dateObj2, ud->getTime);
    if (getTime2 == nullptr) {
        return nullptr;
    }
    g_implG->iInsertAfter(getTime2, dateObj2);
    auto *time2 = g_dynG->iCreateCallthis0(graph, getTime2, dateObj2);
    if (time2 == nullptr) {
        return nullptr;
    }
    g_implG->iInsertAfter(time2, getTime2);
    return time2;
}

// Build the consume-string label print and the elapsed-time print of the epilog, using the
// prolog timestamp `time` and the epilog timestamp `time2`.
static bool AppendEpilogPrints(AbckitGraph *graph, UserData *ud, AbckitInst *time, AbckitInst *time2)
{
    auto *consume = g_dynG->iCreateLoadString(graph, ud->consume);
    if (consume == nullptr) {
        return false;
    }
    g_implG->iInsertAfter(consume, time2);

    auto *print2 = g_dynG->iCreateTryldglobalbyname(graph, ud->print);
    if (print2 == nullptr) {
        return false;
    }
    g_implG->iInsertAfter(print2, consume);
    auto *callArg2 = g_dynG->iCreateCallarg1(graph, print2, consume);
    if (callArg2 == nullptr) {
        return false;
    }
    g_implG->iInsertAfter(callArg2, print2);
    auto *sub = g_dynG->iCreateSub2(graph, time, time2);
    if (sub == nullptr) {
        return false;
    }
    g_implG->iInsertAfter(sub, callArg2);

    auto *print3 = g_dynG->iCreateTryldglobalbyname(graph, ud->print);
    if (print3 == nullptr) {
        return false;
    }
    g_implG->iInsertAfter(print3, sub);
    auto *callArg3 = g_dynG->iCreateCallarg1(graph, print3, sub);
    if (callArg3 == nullptr) {
        return false;
    }
    g_implG->iInsertAfter(callArg3, print3);
    return true;
}

// Append the logging epilog in front of the RETURNUNDEFINED instruction.
static bool AppendLoggingEpilog(AbckitInst *inst, AbckitBasicBlock *bb, UserData *ud, AbckitInst *time)
{
    auto *graph = g_implG->bbGetGraph(bb);
    auto *time2 = AppendEpilogTimestamp(graph, inst, ud);
    if (time2 == nullptr) {
        return false;
    }
    return AppendEpilogPrints(graph, ud, time, time2);
}

// Return failures to the worker while constructing the logging epilog.
static bool CreateEpilogChecked(AbckitInst *inst, AbckitBasicBlock *bb, UserData *ud, AbckitInst *time)
{
    while (inst != nullptr) {
        if (g_dynG->iGetOpcode(inst) == ABCKIT_ISA_API_DYNAMIC_OPCODE_RETURNUNDEFINED) {
            if (!AppendLoggingEpilog(inst, bb, ud, time)) {
                return false;
            }
        }
        inst = g_implG->iGetNext(inst);
    }
    return true;
}

// Build the prolog (string load + first print + date/getTime timestamp) at the start of the
// function. Returns the timestamp instruction (nullptr on failure).
static AbckitInst *BuildPrologChecked(AbckitGraph *graph, AbckitBasicBlock *bb, UserData *ud)
{
    auto *str = g_dynG->iCreateLoadString(graph, ud->str);
    if (str == nullptr) {
        return nullptr;
    }
    g_implG->bbAddInstFront(bb, str);
    auto *print = g_dynG->iCreateTryldglobalbyname(graph, ud->print);
    if (print == nullptr) {
        return nullptr;
    }
    g_implG->iInsertAfter(print, str);
    auto *callArg = g_dynG->iCreateCallarg1(graph, print, str);
    if (callArg == nullptr) {
        return nullptr;
    }
    g_implG->iInsertAfter(callArg, print);

    auto *dateClass = g_dynG->iCreateTryldglobalbyname(graph, ud->date);
    if (dateClass == nullptr) {
        return nullptr;
    }
    g_implG->iInsertAfter(dateClass, callArg);
    auto *dateObj = g_dynG->iCreateNewobjrange(graph, 1, dateClass);
    if (dateObj == nullptr) {
        return nullptr;
    }
    g_implG->iInsertAfter(dateObj, dateClass);
    auto *getTime = g_dynG->iCreateLdobjbyname(graph, dateObj, ud->getTime);
    if (getTime == nullptr) {
        return nullptr;
    }
    g_implG->iInsertAfter(getTime, dateObj);
    auto *time = g_dynG->iCreateCallthis0(graph, getTime, dateObj);
    if (time == nullptr) {
        return nullptr;
    }
    g_implG->iInsertAfter(time, getTime);
    return time;
}

// Thread-safe variant of the add_log TransformIr (mirrors scenarios/add_log TransformIr).
static bool TransformIrChecked(AbckitGraph *graph, UserData *ud)
{
    AbckitBasicBlock *startBB = g_implG->gGetStartBasicBlock(graph);
    if (startBB == nullptr) {
        return false;
    }
    std::vector<AbckitBasicBlock *> succBBs = helpers::BBgetSuccBlocks(startBB);
    if (succBBs.empty()) {
        return false;
    }
    auto *bb = succBBs[0];

    AbckitInst *time = BuildPrologChecked(graph, bb, ud);
    if (time == nullptr) {
        return false;
    }

    VisitData vd;
    vd.ud = ud;
    vd.time = time;
    vd.ok = true;
    g_implG->gVisitBlocksRpo(graph, &vd, [](AbckitBasicBlock *bb, void *data) {
        auto *visitData = reinterpret_cast<VisitData *>(data);
        auto *inst = g_implG->bbGetFirstInst(bb);
        if (!CreateEpilogChecked(inst, bb, visitData->ud, visitData->time)) {
            visitData->ok = false;
        }
        return true;
    });
    return vd.ok;
}

// Build the UserData for handleN. All AbckitString objects are file-owned + immutable once created,
// so they are safe to share across workers; they are created here on the main thread before the
// workers start.
static UserData BuildUserData(AbckitFile *file, int idx)
{
    UserData ud {};
    ud.print = g_implM->createString(file, "print", std::strlen("print"));
    ud.date = g_implM->createString(file, "Date", std::strlen("Date"));
    ud.getTime = g_implM->createString(file, "getTime", std::strlen("getTime"));
    ud.consume = g_implM->createString(file, "Ellapsed time:", std::strlen("Ellapsed time:"));
    std::string s = "file: src/MyClass, function: MyClass.handle" + std::to_string(idx);
    ud.str = g_implM->createString(file, s.c_str(), s.size());
    return ud;
}

// Build the per-worker UserData array (handle1..handle<K_WORKER_COUNT>) on the main thread.
static std::array<UserData, K_WORKER_COUNT> BuildWorkerUserData(AbckitFile *file)
{
    std::array<UserData, K_WORKER_COUNT> uds {};
    for (int i = 0; i < K_WORKER_COUNT; ++i) {
        uds[i] = BuildUserData(file, i + 1);
    }
    return uds;
}

// Serially create + transform + attach a graph for one function. Sets *outGraph on success;
// returns false (without attaching) on any creation/transform failure so the caller can ASSERT.
static bool SerialTransformOne(AbckitCoreFunction *fn, UserData *ud, AbckitGraph **outGraph)
{
    auto *g = g_implI->createGraphFromFunction(fn);
    if (g == nullptr) {
        return false;
    }
    if (!TransformIrChecked(g, ud)) {
        return false;
    }
    g_implM->functionSetGraph(fn, g);
    *outGraph = g;
    return true;
}

// Per-worker parallel body: create the graph, transform it, and commit it to a distinct
// function. Records failures into the shared atomic flag instead of asserting.
static void RunWorkerTransform(std::array<AbckitCoreFunction *, K_WORKER_COUNT> &fns,
                               std::array<AbckitGraph *, K_WORKER_COUNT> &graphs,
                               std::array<UserData, K_WORKER_COUNT> &uds, int i, std::atomic<bool> &failed)
{
    // createGraphFromFunction exercises the D1 single-builder snapshot protocol
    // (one worker emits the shared program, the rest wait + reuse the READY snapshot).
    AbckitGraph *g = g_implI->createGraphFromFunction(fns[i]);
    if (g == nullptr) {
        failed.store(true);
        return;
    }
    if (!TransformIrChecked(g, &uds[i])) {
        failed.store(true);
        g_impl->destroyGraph(g);
        return;
    }
    // functionSetGraph exercises the D4 prepare/commit (unlocked codegen, locked
    // recheck + atomic ins swap + version++) on a different function than the other
    // workers, so commits serialize but never contend on the same per-function state.
    g_implM->functionSetGraph(fns[i], g);
    if (g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        failed.store(true);
    }
    graphs[i] = g;
}

// Destroy the committed graphs. When skipNull is set, guards against destroying a graph slot
// that a failed worker never populated (parallel path).
static void DestroyGraphs(const std::array<AbckitGraph *, K_WORKER_COUNT> &graphs, bool skipNull)
{
    for (int i = 0; i < K_WORKER_COUNT; ++i) {
        if (!skipNull || graphs[i] != nullptr) {
            g_impl->destroyGraph(graphs[i]);
        }
    }
}

// Find handle functions and serially create + transform + attach each graph. The per-function
// last-error is checked with EXPECT (non-fatal); a missing method or transform/attach failure
// returns false so the caller can ASSERT and skip cleanup of incomplete state.
static bool SerialTransformAll(AbckitFile *file, std::array<UserData, K_WORKER_COUNT> &uds,
                               std::array<AbckitCoreFunction *, K_WORKER_COUNT> &fns,
                               std::array<AbckitGraph *, K_WORKER_COUNT> &graphs)
{
    for (int i = 0; i < K_WORKER_COUNT; ++i) {
        fns[i] = helpers::FindMethodByName(file, "handle" + std::to_string(i + 1));
        if (fns[i] == nullptr) {
            return false;
        }
        if (!SerialTransformOne(fns[i], &uds[i], &graphs[i])) {
            return false;
        }
        EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }
    return true;
}

// Launch the per-worker transform threads and block until all join.
static void LaunchParallelWorkers(std::array<AbckitCoreFunction *, K_WORKER_COUNT> &fns,
                                  std::array<AbckitGraph *, K_WORKER_COUNT> &graphs,
                                  std::array<UserData, K_WORKER_COUNT> &uds, std::atomic<bool> &failed)
{
    std::vector<std::thread> threads;
    threads.reserve(K_WORKER_COUNT);
    for (int i = 0; i < K_WORKER_COUNT; ++i) {
        threads.emplace_back([&fns, &graphs, &uds, i, &failed]() { RunWorkerTransform(fns, graphs, uds, i, failed); });
    }
    for (auto &t : threads) {
        t.join();
    }
}

// Expected K_WORKER_COUNT-block output (handle1..N in main's call order). The elapsed-time line is a
// signed integer (prolog timestamp minus epilog timestamp; typically 0 or negative), hence -?\d+.
static std::string BuildExpectedOutput()
{
    std::string e;
    for (int i = 1; i <= K_WORKER_COUNT; ++i) {
        e += "file: src/MyClass, function: MyClass\\.handle" + std::to_string(i) +
             "\n"
             "abckit\n"
             "Ellapsed time:\n"
             "-?\\d+\n";
    }
    return e;
}

// Normalize the nondeterministic elapsed-time line (a whole-line signed integer) to "N" so the
// serial and parallel outputs can be compared for byte-for-byte equivalence.
static std::string NormalizeNumberLines(const std::string &s)
{
    const std::regex numRe("-?\\d+");
    std::string out;
    out.reserve(s.size());
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) {
        std::smatch m;
        out += std::regex_match(line, m, numRe) ? "N" : line;
        out += '\n';
    }
    return out;
}
}  // namespace

class LibAbcKitDynamicScenarioParallelTest : public ::testing::Test {};

// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicScenarioParallelTest, LibAbcKitTestDynamicAddLogSerialParallel)
{
    // Baseline: the unmodified abc prints "abckit" once per handle.
    auto base = helpers::ExecuteDynamicAbc(INPUT_PATH, ENTRY);
    EXPECT_TRUE(helpers::Match(base, "(abckit\n){4}"));

    const std::string expected = BuildExpectedOutput();

    // ---- SERIAL baseline: transform all functions sequentially on one file ----
    {
        AbckitFile *file = g_impl->openAbc(INPUT_PATH, std::strlen(INPUT_PATH));
        ASSERT_NE(file, nullptr);
        auto uds = BuildWorkerUserData(file);
        std::array<AbckitCoreFunction *, K_WORKER_COUNT> fns {};
        std::array<AbckitGraph *, K_WORKER_COUNT> graphs {};
        ASSERT_TRUE(SerialTransformAll(file, uds, fns, graphs));
        DestroyGraphs(graphs, false);
        g_impl->writeAbc(file, SERIAL_OUT_PATH, std::strlen(SERIAL_OUT_PATH));
        EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
        g_impl->closeFile(file);
    }
    auto serialOut = helpers::ExecuteDynamicAbc(SERIAL_OUT_PATH, ENTRY);
    EXPECT_TRUE(helpers::Match(serialOut, expected));

    // ---- PARALLEL: K_WORKER_COUNT workers, each a different function on the same shared file ----
    {
        AbckitFile *file = g_impl->openAbc(INPUT_PATH, std::strlen(INPUT_PATH));
        ASSERT_NE(file, nullptr);
        auto uds = BuildWorkerUserData(file);
        std::array<AbckitCoreFunction *, K_WORKER_COUNT> fns {};
        std::array<AbckitGraph *, K_WORKER_COUNT> graphs {};
        for (int i = 0; i < K_WORKER_COUNT; ++i) {
            fns[i] = helpers::FindMethodByName(file, "handle" + std::to_string(i + 1));
            ASSERT_NE(fns[i], nullptr);
        }
        std::atomic<bool> failed {false};
        LaunchParallelWorkers(fns, graphs, uds, failed);
        ASSERT_FALSE(failed.load()) << "parallel add_log transform reported a failure";
        DestroyGraphs(graphs, true);
        g_impl->writeAbc(file, PARALLEL_OUT_PATH, std::strlen(PARALLEL_OUT_PATH));
        EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
        g_impl->closeFile(file);
    }
    auto parallelOut = helpers::ExecuteDynamicAbc(PARALLEL_OUT_PATH, ENTRY);
    EXPECT_TRUE(helpers::Match(parallelOut, expected));

    // Stronger than regex-match: the parallel output must equal the serial output modulo the
    // nondeterministic elapsed-time number line.
    EXPECT_EQ(NormalizeNumberLines(serialOut), NormalizeNumberLines(parallelOut));
}

}  // namespace libabckit::test

// NOLINTEND(readability-magic-numbers)
