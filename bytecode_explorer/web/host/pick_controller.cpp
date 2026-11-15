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

#include "web/host/pick_controller.h"

#include <exception>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

constexpr size_t MAX_OPEN_PATH_BYTES = 4096U;

}  // namespace

PickController::PickController(WebSession &session, DialogOpener opener)
    : session_(session), opener_(std::move(opener))
{
    if (!opener_) {
        opener_ = [] { return FileDialog().Open(); };
    }
}

PickController::~PickController()
{
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool PickController::StartPick()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.name == "picking") {
            return false;
        }
        state_ = State {"picking", {}, {}};
    }
    // The previous worker has already reported its terminal state; joining it
    // only waits for its final bookkeeping statements.
    if (worker_.joinable()) {
        worker_.join();
    }
    try {
        worker_ = std::thread([this] { DialogWorker(); });
        return true;
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = State {"failed", {}, "Cannot start the file dialog"};
        return false;
    }
}

PickController::OpenStatus PickController::OpenPath(std::string path)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.name == "picking") {
            return OpenStatus::BUSY;
        }
    }
    if (path.empty() || path.size() > MAX_OPEN_PATH_BYTES || path.find('\0') != std::string::npos) {
        return OpenStatus::INVALID;
    }
    if (!session_.Reload(std::move(path))) {
        return OpenStatus::BUSY;
    }
    return OpenStatus::ACCEPTED;
}

PickController::State PickController::QueryState() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void PickController::DialogWorker() noexcept
{
    FileDialogResult result;
    try {
        result = opener_();
    } catch (const std::exception &error) {
        result = FileDialogResult {FileDialogOutcome::FAILED, {}, error.what()};
    } catch (...) {
        result = FileDialogResult {FileDialogOutcome::FAILED, {}, "Unknown file dialog failure"};
    }
    // Reload runs before the terminal state is published: "selected" implies
    // the new document load was already scheduled.
    bool reloadAccepted = false;
    if (result.outcome == FileDialogOutcome::SELECTED) {
        reloadAccepted = session_.Reload(result.path);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    switch (result.outcome) {
        case FileDialogOutcome::SELECTED:
            state_ = reloadAccepted ? State {"selected", result.path, {}}
                                    : State {"failed", {}, "Another document load is already in progress"};
            break;
        case FileDialogOutcome::CANCELLED:
            state_ = State {"cancelled", {}, {}};
            break;
        case FileDialogOutcome::FAILED:
            state_ = State {"failed", {}, result.error.empty() ? "The file dialog failed" : result.error};
            break;
    }
}

}  // namespace panda::bytecode_explorer
