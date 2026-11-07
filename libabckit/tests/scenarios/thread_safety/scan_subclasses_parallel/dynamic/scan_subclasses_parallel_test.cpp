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

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "libabckit/c/abckit.h"
#include "libabckit/c/metadata_core.h"
#include "scenarios/thread_safety/common/scenario_harness.h"
#include "scenarios/scan_subclasses/dynamic/subclasses_scanner.h"

namespace libabckit::test {
namespace {

constexpr auto VERSION = ABCKIT_VERSION_RELEASE_1_0_0;
constexpr auto INPUT_PATH =
    ABCKIT_ABC_DIR "scenarios/thread_safety/scan_subclasses_parallel/dynamic/scan_subclasses_parallel.abc";
constexpr size_t WORKER_COUNT = 4;
constexpr size_t EXPECTED_SUBCLASS_COUNT = 12;

const AbckitApi *g_impl = AbckitGetApiImpl(VERSION);
const AbckitInspectApi *g_implI = AbckitGetInspectApiImpl(VERSION);

const std::array<std::string_view, WORKER_COUNT> BUSINESS_MODULES = {
    "modules/order_handlers", "modules/account_handlers", "modules/payment_handlers", "modules/delivery_handlers"};

const std::vector<ClassInfo> BASE_CLASSES = {
    {"modules/base_handlers", "OrderHandler"},
    {"modules/base_handlers", "AccountHandler"},
    {"modules/base_handlers", "PaymentHandler"},
    {"modules/base_handlers", "DeliveryHandler"},
};

using SubclassKey = std::pair<std::string, std::string>;

const std::vector<SubclassKey> EXPECTED_SUBCLASSES = {
    {"modules/account_handlers", "AuditAccountHandler"},      {"modules/account_handlers", "CloseAccountHandler"},
    {"modules/account_handlers", "OpenAccountHandler"},       {"modules/delivery_handlers", "CancelDeliveryHandler"},
    {"modules/delivery_handlers", "ScheduleDeliveryHandler"}, {"modules/delivery_handlers", "TrackDeliveryHandler"},
    {"modules/order_handlers", "AuditOrderHandler"},          {"modules/order_handlers", "CancelOrderHandler"},
    {"modules/order_handlers", "CreateOrderHandler"},         {"modules/payment_handlers", "AuthorizePaymentHandler"},
    {"modules/payment_handlers", "CapturePaymentHandler"},    {"modules/payment_handlers", "RefundPaymentHandler"},
};

void SetFirstError(AbckitStatus &status, AbckitStatus error)
{
    if (status == ABCKIT_STATUS_NO_ERROR && error != ABCKIT_STATUS_NO_ERROR) {
        status = error;
    }
}

class ScopedAbckitFile final {
public:
    explicit ScopedAbckitFile(AbckitFile *file) : file_(file) {}

    ~ScopedAbckitFile()
    {
        if (file_ != nullptr) {
            g_impl->closeFile(file_);
        }
    }

    ScopedAbckitFile(const ScopedAbckitFile &) = delete;
    ScopedAbckitFile &operator=(const ScopedAbckitFile &) = delete;

    AbckitFile *Get() const
    {
        return file_;
    }

    AbckitStatus Close()
    {
        g_impl->closeFile(file_);
        auto status = g_impl->getLastError();
        if (status == ABCKIT_STATUS_NO_ERROR) {
            file_ = nullptr;
        }
        return status;
    }

private:
    AbckitFile *file_ = nullptr;
};

struct ModuleDiscoveryResult {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    std::array<AbckitCoreModule *, WORKER_COUNT> modules {};
};

struct ScanResult {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    std::vector<ClassInfo> subclasses;
};

ModuleDiscoveryResult DiscoverBusinessModules(AbckitFile *file)
{
    ModuleDiscoveryResult result;
    g_implI->fileEnumerateModules(file, &result, [](AbckitCoreModule *module, void *data) {
        auto &discovery = *static_cast<ModuleDiscoveryResult *>(data);
        auto *name = g_implI->moduleGetName(module);
        SetFirstError(discovery.status, g_impl->getLastError());
        if (name == nullptr || discovery.status != ABCKIT_STATUS_NO_ERROR) {
            SetFirstError(discovery.status, ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
        }
        const char *nameData = g_implI->abckitStringToString(name);
        SetFirstError(discovery.status, g_impl->getLastError());
        if (nameData == nullptr || discovery.status != ABCKIT_STATUS_NO_ERROR) {
            SetFirstError(discovery.status, ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
        }
        auto iter = std::find(BUSINESS_MODULES.begin(), BUSINESS_MODULES.end(), std::string_view(nameData));
        if (iter == BUSINESS_MODULES.end()) {
            return true;
        }
        auto index = static_cast<size_t>(std::distance(BUSINESS_MODULES.begin(), iter));
        if (discovery.modules[index] != nullptr) {
            SetFirstError(discovery.status, ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
        }
        discovery.modules[index] = module;
        return true;
    });
    SetFirstError(result.status, g_impl->getLastError());
    if (std::any_of(result.modules.begin(), result.modules.end(), [](auto *module) { return module == nullptr; })) {
        SetFirstError(result.status, ABCKIT_STATUS_INTERNAL_ERROR);
    }
    return result;
}

std::vector<SubclassKey> NormalizeSubclasses(const std::vector<ClassInfo> &subclasses)
{
    std::vector<SubclassKey> normalized;
    std::transform(subclasses.begin(), subclasses.end(), std::back_inserter(normalized), [](const ClassInfo &info) {
        return SubclassKey {info.path, info.className};
    });
    std::sort(normalized.begin(), normalized.end());
    return normalized;
}

ScanResult ScanSequential(AbckitFile *file, const std::array<AbckitCoreModule *, WORKER_COUNT> &modules)
{
    ScanResult result;
    SubclassesScanner scanner(VERSION, g_impl, file, BASE_CLASSES);
    std::vector<AbckitCoreModule *> moduleShard(modules.begin(), modules.end());
    scanner.RunForModules(moduleShard);
    result.status = scanner.GetStatus();
    result.subclasses = scanner.GetSubclasses();
    SetFirstError(result.status, g_impl->getLastError());
    return result;
}

ScanResult ScanParallel(AbckitFile *file, const std::array<AbckitCoreModule *, WORKER_COUNT> &modules)
{
    std::array<ScanResult, WORKER_COUNT> workerResults;
    scenario::WorkerGroup workers(WORKER_COUNT);

    for (size_t workerIndex = 0; workerIndex < WORKER_COUNT; ++workerIndex) {
        workers.threads.emplace_back([&, workerIndex]() {
            workers.ready.fetch_add(1, std::memory_order_release);
            while (!workers.start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            if (workers.cancelled.load(std::memory_order_acquire)) {
                return;
            }
            SubclassesScanner scanner(VERSION, g_impl, file, BASE_CLASSES);
            std::vector<AbckitCoreModule *> moduleShard {modules[workerIndex]};
            scanner.RunForModules(moduleShard);
            workerResults[workerIndex].status = scanner.GetStatus();
            workerResults[workerIndex].subclasses = scanner.GetSubclasses();
            SetFirstError(workerResults[workerIndex].status, g_impl->getLastError());
        });
    }

    while (workers.ready.load(std::memory_order_acquire) != WORKER_COUNT) {
        std::this_thread::yield();
    }
    workers.start.store(true, std::memory_order_release);
    workers.Join();

    ScanResult result;
    for (auto &workerResult : workerResults) {
        SetFirstError(result.status, workerResult.status);
        result.subclasses.insert(result.subclasses.end(), std::make_move_iterator(workerResult.subclasses.begin()),
                                 std::make_move_iterator(workerResult.subclasses.end()));
    }
    return result;
}

}  // namespace

class AbckitParallelSubclassesScannerScenarioTest : public ::testing::Test {};

// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c, threads=4
TEST_F(AbckitParallelSubclassesScannerScenarioTest, LibAbcKitTestParallelScanSubclassesDynamic)
{
    AbckitFile *sequentialFileRaw = g_impl->openAbc(INPUT_PATH, strlen(INPUT_PATH));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(sequentialFileRaw, nullptr);
    ScopedAbckitFile sequentialFile(sequentialFileRaw);

    auto sequentialModules = DiscoverBusinessModules(sequentialFile.Get());
    ASSERT_EQ(sequentialModules.status, ABCKIT_STATUS_NO_ERROR);
    auto sequential = ScanSequential(sequentialFile.Get(), sequentialModules.modules);
    EXPECT_EQ(sequential.status, ABCKIT_STATUS_NO_ERROR);
    EXPECT_EQ(sequentialFile.Close(), ABCKIT_STATUS_NO_ERROR);

    AbckitFile *parallelFileRaw = g_impl->openAbc(INPUT_PATH, strlen(INPUT_PATH));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(parallelFileRaw, nullptr);
    ScopedAbckitFile parallelFile(parallelFileRaw);

    auto parallelModules = DiscoverBusinessModules(parallelFile.Get());
    ASSERT_EQ(parallelModules.status, ABCKIT_STATUS_NO_ERROR);
    auto parallel = ScanParallel(parallelFile.Get(), parallelModules.modules);
    EXPECT_EQ(parallel.status, ABCKIT_STATUS_NO_ERROR);
    EXPECT_EQ(parallelFile.Close(), ABCKIT_STATUS_NO_ERROR);

    auto sequentialSubclasses = NormalizeSubclasses(sequential.subclasses);
    auto parallelSubclasses = NormalizeSubclasses(parallel.subclasses);
    EXPECT_EQ(sequentialSubclasses.size(), EXPECTED_SUBCLASS_COUNT);
    EXPECT_EQ(parallelSubclasses.size(), EXPECTED_SUBCLASS_COUNT);
    EXPECT_EQ(sequentialSubclasses, EXPECTED_SUBCLASSES);
    EXPECT_EQ(parallelSubclasses, sequentialSubclasses);
}

}  // namespace libabckit::test
