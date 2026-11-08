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
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "libabckit/c/abckit.h"
#include "libabckit/c/metadata_core.h"
#include "scenarios/thread_safety/common/scenario_harness.h"
#include "scenarios/api_scanner/dynamic/api_scanner.h"

namespace libabckit::test {
namespace {

constexpr auto VERSION = ABCKIT_VERSION_RELEASE_1_0_0;
constexpr auto INPUT_PATH =
    ABCKIT_ABC_DIR "scenarios/thread_safety/api_scanner_parallel/dynamic/api_scanner_parallel.abc";
constexpr size_t WORKER_COUNT = 4;
constexpr size_t ROUND_COUNT = 10;
constexpr size_t EXPECTED_USAGE_COUNT = 32;

const AbckitApi *g_impl = AbckitGetApiImpl(VERSION);
const AbckitInspectApi *g_implI = AbckitGetInspectApiImpl(VERSION);

const std::array<std::string_view, WORKER_COUNT> BUSINESS_MODULES = {
    "modules/order_feature", "modules/account_feature", "modules/payment_feature", "modules/delivery_feature"};

const std::vector<ApiInfo> API_LIST = {
    {"modules/platform_api", "Telemetry", "report"},
    {"modules/platform_api", "authorize", ""},
    {"modules/platform_api", "Storage", "put"},
    {"@ohos.geolocation", "geolocation", "getCurrentLocation"},
};

using UsageKey = std::tuple<size_t, std::string, std::string>;

const std::vector<UsageKey> EXPECTED_USAGES = {
    {0, "modules/account_feature", "auditAccount"},
    {0, "modules/account_feature", "createAccount"},
    {0, "modules/delivery_feature", "dispatchDelivery"},
    {0, "modules/delivery_feature", "trackDelivery"},
    {0, "modules/order_feature", "cancelOrder"},
    {0, "modules/order_feature", "submitOrder"},
    {0, "modules/payment_feature", "capturePayment"},
    {0, "modules/payment_feature", "refundPayment"},
    {1, "modules/account_feature", "AccountController.handle"},
    {1, "modules/account_feature", "createAccount"},
    {1, "modules/delivery_feature", "DeliveryController.authorizeDriver"},
    {1, "modules/delivery_feature", "dispatchDelivery"},
    {1, "modules/order_feature", "OrderController.handle"},
    {1, "modules/order_feature", "submitOrder"},
    {1, "modules/payment_feature", "capturePayment"},
    {1, "modules/payment_feature", "refundPayment"},
    {2, "modules/account_feature", "auditAccount"},
    {2, "modules/account_feature", "createAccount"},
    {2, "modules/delivery_feature", "dispatchDelivery"},
    {2, "modules/delivery_feature", "trackDelivery"},
    {2, "modules/order_feature", "OrderController.handle"},
    {2, "modules/order_feature", "submitOrder"},
    {2, "modules/payment_feature", "PaymentController.persist"},
    {2, "modules/payment_feature", "capturePayment"},
    {3, "modules/account_feature", "AccountController.handle"},
    {3, "modules/account_feature", "createAccount"},
    {3, "modules/delivery_feature", "DeliveryController.authorizeDriver"},
    {3, "modules/delivery_feature", "dispatchDelivery"},
    {3, "modules/order_feature", "OrderController.handle"},
    {3, "modules/order_feature", "submitOrder"},
    {3, "modules/payment_feature", "PaymentController.persist"},
    {3, "modules/payment_feature", "capturePayment"},
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
    ApiUsageMap usages;
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

std::vector<UsageKey> NormalizeUsages(const ApiUsageMap &usages)
{
    std::vector<UsageKey> normalized;
    for (const auto &[apiIndex, entries] : usages) {
        for (const auto &entry : entries) {
            normalized.emplace_back(apiIndex, entry.source, entry.funcName);
        }
    }
    std::sort(normalized.begin(), normalized.end());
    return normalized;
}

bool HasDuplicateUsages(const std::vector<UsageKey> &usages)
{
    return std::adjacent_find(usages.begin(), usages.end()) != usages.end();
}

ScanResult ScanSequential(AbckitFile *file, const std::array<AbckitCoreModule *, WORKER_COUNT> &modules)
{
    ScanResult result;
    ApiScanner scanner(VERSION, file, API_LIST);
    std::vector<AbckitCoreModule *> moduleShard(modules.begin(), modules.end());
    result.usages = scanner.GetApiUsagesForModules(moduleShard);
    result.status = scanner.GetStatus();
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
            ApiScanner scanner(VERSION, file, API_LIST);
            std::vector<AbckitCoreModule *> moduleShard {modules[workerIndex]};
            workerResults[workerIndex].usages = scanner.GetApiUsagesForModules(moduleShard);
            workerResults[workerIndex].status = scanner.GetStatus();
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
        for (auto &[apiIndex, entries] : workerResult.usages) {
            auto &mergedEntries = result.usages[apiIndex];
            mergedEntries.insert(mergedEntries.end(), std::make_move_iterator(entries.begin()),
                                 std::make_move_iterator(entries.end()));
        }
    }
    return result;
}

}  // namespace

class AbckitParallelApiScannerScenarioTest : public ::testing::Test {};

// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c, threads=4
TEST_F(AbckitParallelApiScannerScenarioTest, LibAbcKitTestParallelApiScannerDynamic)
{
    AbckitFile *sequentialFileRaw = g_impl->openAbc(INPUT_PATH, strlen(INPUT_PATH));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(sequentialFileRaw, nullptr);
    ScopedAbckitFile sequentialFile(sequentialFileRaw);

    auto sequentialModules = DiscoverBusinessModules(sequentialFile.Get());
    ASSERT_EQ(sequentialModules.status, ABCKIT_STATUS_NO_ERROR);
    auto sequential = ScanSequential(sequentialFile.Get(), sequentialModules.modules);
    ASSERT_EQ(sequential.status, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(sequentialFile.Close(), ABCKIT_STATUS_NO_ERROR);

    auto sequentialUsages = NormalizeUsages(sequential.usages);
    ASSERT_EQ(sequentialUsages.size(), EXPECTED_USAGE_COUNT);
    ASSERT_EQ(sequentialUsages, EXPECTED_USAGES);
    ASSERT_FALSE(HasDuplicateUsages(sequentialUsages));

    for (size_t round = 0; round < ROUND_COUNT; ++round) {
        SCOPED_TRACE("round=" + std::to_string(round));

        AbckitFile *parallelFileRaw = g_impl->openAbc(INPUT_PATH, strlen(INPUT_PATH));
        ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
        ASSERT_NE(parallelFileRaw, nullptr);
        ScopedAbckitFile parallelFile(parallelFileRaw);

        auto parallelModules = DiscoverBusinessModules(parallelFile.Get());
        ASSERT_EQ(parallelModules.status, ABCKIT_STATUS_NO_ERROR);
        auto parallel = ScanParallel(parallelFile.Get(), parallelModules.modules);
        ASSERT_EQ(parallel.status, ABCKIT_STATUS_NO_ERROR);
        ASSERT_EQ(parallelFile.Close(), ABCKIT_STATUS_NO_ERROR);

        auto parallelUsages = NormalizeUsages(parallel.usages);
        ASSERT_EQ(parallelUsages.size(), EXPECTED_USAGE_COUNT);
        ASSERT_EQ(parallelUsages, sequentialUsages);
        ASSERT_FALSE(HasDuplicateUsages(parallelUsages));
        std::ostringstream evidence;
        evidence << "workers=4;closed=1\n";
        for (const auto &[apiIndex, module, function] : parallelUsages) {
            evidence << apiIndex << ':' << module << ':' << function << '\n';
        }
        RecordProperty("round_" + std::to_string(round), evidence.str());
    }
}

}  // namespace libabckit::test
