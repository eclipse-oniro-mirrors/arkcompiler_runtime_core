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

#ifndef LIBABCKIT_TESTS_SCENARIOS_THREAD_SAFETY_COMMON_HARNESS_H
#define LIBABCKIT_TESTS_SCENARIOS_THREAD_SAFETY_COMMON_HARNESS_H

#include <atomic>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "libabckit/c/abckit.h"
#include "libabckit/c/metadata_core.h"

namespace libabckit::test::scenario {

inline const AbckitApi *Api()
{
    return AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
}

class ScopedFile final {
public:
    explicit ScopedFile(AbckitFile *file) : file_(file) {}
    ~ScopedFile()
    {
        if (file_ != nullptr) {
            Api()->closeFile(file_);
        }
    }
    ScopedFile(const ScopedFile &) = delete;
    ScopedFile &operator=(const ScopedFile &) = delete;
    AbckitFile *Get() const
    {
        return file_;
    }
    AbckitStatus Close()
    {
        if (file_ == nullptr) {
            return ABCKIT_STATUS_NO_ERROR;
        }
        Api()->closeFile(file_);
        auto status = Api()->getLastError();
        if (status == ABCKIT_STATUS_NO_ERROR) {
            file_ = nullptr;
        }
        return status;
    }

private:
    AbckitFile *file_;
};

// Owns threads even if starting a later worker fails. No worker starts business work until all were created.
class WorkerGroup final {
public:
    explicit WorkerGroup(size_t count)
    {
        threads.reserve(count);
    }
    ~WorkerGroup()
    {
        cancelled.store(true, std::memory_order_release);
        start.store(true, std::memory_order_release);
        Join();
    }
    void Join()
    {
        for (auto &worker : threads) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    std::vector<std::thread> threads;
    std::atomic<size_t> ready {0};
    std::atomic<bool> start {false};
    std::atomic<bool> cancelled {false};
};

template <class Result>
struct RunResult {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    std::vector<Result> workers;
    std::vector<size_t> shardSizes;
};

// The callback owns every Graph for its shard through create/modify/set/destroy. No gtest assertions in callbacks.
// A one-worker run calls the same callback inline, providing a true serial baseline without thread startup.
template <class Result, class Callback>
RunResult<Result> RunShards(size_t taskCount, size_t workerCount, Callback callback)
{
    RunResult<Result> run;
    if (workerCount == 0) {
        run.status = ABCKIT_STATUS_BAD_ARGUMENT;
        return run;
    }
    run.workers.resize(workerCount);
    run.shardSizes.resize(workerCount);
    std::vector<std::vector<size_t>> shards(workerCount);
    for (size_t index = 0; index < taskCount; ++index) {
        shards[index % workerCount].push_back(index);
    }
    for (size_t index = 0; index < workerCount; ++index) {
        run.shardSizes[index] = shards[index].size();
    }
    if (workerCount == 1) {
        run.workers[0] = callback(shards[0]);
    } else {
        WorkerGroup group(workerCount);
        for (size_t index = 0; index < workerCount; ++index) {
            group.threads.emplace_back([&, index]() {
                group.ready.fetch_add(1, std::memory_order_release);
                while (!group.start.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                if (!group.cancelled.load(std::memory_order_acquire)) {
                    run.workers[index] = callback(shards[index]);
                }
            });
        }
        while (group.ready.load(std::memory_order_acquire) != workerCount) {
            std::this_thread::yield();
        }
        group.start.store(true, std::memory_order_release);
        group.Join();
    }
    for (const auto &worker : run.workers) {
        if (run.status == ABCKIT_STATUS_NO_ERROR) {
            run.status = worker.status;
        }
    }
    return run;
}

inline bool ShouldVerifyRuntimeOutput()
{
    // Only VM execution is optional: ETS GC has independent TSAN reports.
    // All LibAbcKit transformations, write/reopen and structural comparisons still execute.
    const char *skip = std::getenv("ABCKIT_SCENARIO_SKIP_RUNTIME");
    return skip == nullptr || std::string_view(skip) != "1";
}

struct Observation {
    std::vector<std::string> graphs;
    std::string output;
};

// Capture from the reopened output, using RPO positions for edges and instruction inputs and contents for pool IDs.
void CaptureGraph(AbckitCoreFunction *function, Observation &observation);
void Compare(const Observation &serial, const Observation &parallel);
std::string NormalizeElapsedTime(const std::string &output);

}  // namespace libabckit::test::scenario
#endif
