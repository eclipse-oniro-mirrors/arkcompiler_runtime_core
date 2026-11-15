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

#include "web/api/web_session.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <algorithm>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

const char *StageName(DocumentLoadStage stage)
{
    switch (stage) {
        case DocumentLoadStage::OPENING:
            return "opening";
        case DocumentLoadStage::METHODS:
            return "methods";
        case DocumentLoadStage::PHYSICAL:
            return "physical";
        case DocumentLoadStage::STRUCTURE:
            return "structure";
        case DocumentLoadStage::LOCATIONS:
            return "locations";
        case DocumentLoadStage::METADATA:
            return "metadata";
    }
    return "opening";
}

const char *StateName(WebSessionState state)
{
    switch (state) {
        case WebSessionState::LOADING:
            return "loading";
        case WebSessionState::READY:
            return "ready";
        case WebSessionState::FAILED:
            return "failed";
        case WebSessionState::CANCELLED:
            return "cancelled";
        case WebSessionState::EMPTY:
            return "empty";
    }
    return "failed";
}

}  // namespace

WebSession::WebSession(std::string path, Loader loader) : path_(std::move(path)), loader_(std::move(loader))
{
    if (!loader_) {
        loader_ = [](const std::string &file, const DocumentLoadOptions &options) {
            return LoadExplorerDocument(file, options);
        };
    }
    if (path_.empty()) {
        status_.state = WebSessionState::EMPTY;
        status_.stage = "empty";
    }
    cancellation_ = std::make_shared<CancellationSource>();
}

WebSession::WebSession(LoadedDocument document) : api_(std::make_shared<WebApi>(std::move(document)))
{
    // Reload still needs a loader even when the first document is injected directly.
    loader_ = [](const std::string &file, const DocumentLoadOptions &options) {
        return LoadExplorerDocument(file, options);
    };
    status_.state = WebSessionState::READY;
    status_.stage = "ready";
    cancellation_ = std::make_shared<CancellationSource>();
}

WebSession::~WebSession()
{
    Cancel();
    Wait();
}

bool WebSession::Start()
{
    std::shared_ptr<CancellationSource> cancellation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (started_ || status_.state != WebSessionState::LOADING)
            return false;
        started_ = true;
        cancellation = cancellation_;
    }
    try {
        const auto token = cancellation->Token();
        const auto path = path_;
        runner_.Post([this, path, token] { Run(path, token); });
        return true;
    } catch (const std::exception &error) {
        Fail(ErrorCode::BACKEND_ERROR, error.what());
    } catch (...) {
        Fail(ErrorCode::BACKEND_ERROR, "Cannot schedule ABC loading");
    }
    return false;
}

bool WebSession::Reload(std::string path)
{
    if (path.empty()) {
        return false;
    }
    auto cancellation = std::make_shared<CancellationSource>();
    const auto token = cancellation->Token();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state == WebSessionState::LOADING)
            return false;
        started_ = true;
        cancellation_ = cancellation;
        status_.state = WebSessionState::LOADING;
        status_.stage = "opening";
        status_.error.clear();
        status_.errorCode = ErrorCode::NONE;
        status_.lastError.clear();
    }
    try {
        runner_.Post([this, path = std::move(path), token] { Run(path, token); });
        return true;
    } catch (...) {
        // Scheduling failed: fall back to the state the session had before.
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state != WebSessionState::LOADING)
            return false;
        if (api_ != nullptr) {
            status_.state = WebSessionState::READY;
            status_.stage = "ready";
            status_.lastError = "Cannot schedule ABC loading";
        } else {
            status_.state = WebSessionState::FAILED;
            status_.stage = "failed";
            status_.errorCode = ErrorCode::BACKEND_ERROR;
            status_.error = "Cannot schedule ABC loading";
        }
        return false;
    }
}

void WebSession::SetDocumentOpenedCallback(DocumentOpenedCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    onDocumentOpened_ = std::move(callback);
}

void WebSession::Cancel()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancellation_ != nullptr) {
        cancellation_->Cancel();
    }
    if (status_.state == WebSessionState::LOADING) {
        if (api_ != nullptr) {
            // A reload was cancelled: keep serving the previous document.
            status_.state = WebSessionState::READY;
            status_.stage = "ready";
            status_.lastError = "ABC loading was cancelled";
        } else {
            status_.state = WebSessionState::CANCELLED;
            status_.stage = "cancelled";
        }
    }
}

void WebSession::Wait()
{
    runner_.WaitForIdle();
}

WebSessionStatus WebSession::Status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

std::string WebSession::StatusJson() const
{
    const auto status = Status();
    using Json = nlohmann::json;
    Json error = nullptr;
    if (status.state == WebSessionState::FAILED) {
        error = status.error.empty() ? "Cannot load ABC" : status.error;
    } else if (status.state == WebSessionState::CANCELLED) {
        error = "ABC loading was cancelled";
    }
    // lastError reports a failed reload; it stays until the next reload starts
    // so only readers that initiated a switch surface it.
    Json lastError = status.lastError.empty() ? Json() : Json(status.lastError);
    return Json({{"apiVersion", 1}, {"state", StateName(status.state)}, {"stage", status.stage}, {"error", error},
                 {"lastError", lastError}})
        .dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::shared_ptr<const WebApi> WebSession::Api() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return api_;
}

void WebSession::SetStage(const char *stage)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state == WebSessionState::LOADING)
        status_.stage = stage;
}

void WebSession::Fail(ErrorCode code, std::string_view message) noexcept
{
    try {
        if (code == ErrorCode::CANCELLED) {
            Cancel();
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state != WebSessionState::LOADING)
            return;
        std::string diagnostic;
        diagnostic.assign(message.data(), std::min(message.size(), size_t {4096U}));
        if (api_ != nullptr) {
            // A reload failed: keep serving the previous document and report the switch failure.
            status_.state = WebSessionState::READY;
            status_.stage = "ready";
            status_.lastError = std::move(diagnostic);
            return;
        }
        status_.state = WebSessionState::FAILED;
        status_.stage = "failed";
        status_.errorCode = code;
        // State is already terminal if allocating a diagnostic fails.
        status_.error = std::move(diagnostic);
    } catch (...) {
        // The status endpoint supplies a fixed fallback when the diagnostic cannot be retained.
    }
}

void WebSession::Run(std::string path, CancellationToken token) noexcept
{
    // Copied out under the publication lock and invoked after it: the callback
    // runs on this load thread only for loads that end in a published document.
    DocumentOpenedCallback notify;
    try {
        if (token.IsCancellationRequested())
            return;
        DocumentLoadOptions options {token, [this](DocumentLoadStage stage) { SetStage(StageName(stage)); }};
        auto loaded = loader_(path, options);
        if (!loaded.HasValue()) {
            Fail(loaded.Error().Code(), loaded.Error().Message());
            return;
        }
        if (token.IsCancellationRequested())
            return;
        auto api = std::make_shared<WebApi>(std::move(loaded.Value()));
        SetStage("serializing");
        {
            // Prepare the expensive structure payload off the HTTP event loop before publication.
            const auto structure = api->StructureJson(token);
            if (!structure.HasValue()) {
                Fail(structure.Error().Code(), structure.Error().Message());
                return;
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state != WebSessionState::LOADING || token.IsCancellationRequested())
            return;
        api_ = std::move(api);
        status_.stage = "ready";
        status_.state = WebSessionState::READY;
        status_.lastError.clear();
        notify = onDocumentOpened_;
    } catch (const std::exception &error) {
        Fail(ErrorCode::BACKEND_ERROR, error.what());
    } catch (...) {
        Fail(ErrorCode::BACKEND_ERROR, "Unknown ABC loading failure");
    }
    if (notify != nullptr) {
        // The opened-document notification is auxiliary bookkeeping: a failure
        // inside it must never fail the document that just loaded.
        try {
            notify(path);
        } catch (...) {
            // Deliberately swallowed: the notify call is best-effort only.
        }
    }
}

}  // namespace panda::bytecode_explorer
