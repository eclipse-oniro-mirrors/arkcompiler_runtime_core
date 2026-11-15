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

#ifndef BYTECODE_EXPLORER_WEB_API_WEB_SESSION_H
#define BYTECODE_EXPLORER_WEB_API_WEB_SESSION_H

#include "app/document_loader.h"
#include "core/common/task_runner.h"
#include "web/api/web_api.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace panda::bytecode_explorer {

enum class WebSessionState { LOADING, READY, FAILED, CANCELLED, EMPTY };

struct WebSessionStatus {
    WebSessionState state {WebSessionState::LOADING};
    std::string stage {"opening"};
    std::string error;
    ErrorCode errorCode {ErrorCode::NONE};
    // Set only when a reload failed while an older document kept being served;
    // readers poll it from the session status to report the failed switch.
    std::string lastError;
};

// Owns the active document and publishes only complete, immutable document APIs.
// The document is replaceable: Reload() loads a different ABC in the background
// and swaps the published API when the new document is complete. A failed reload
// keeps the previous document published and reports the failure via lastError.
class WebSession final {
public:
    using Loader = std::function<LoadedDocumentResult(const std::string &, const DocumentLoadOptions &)>;

    // An empty path starts an EMPTY session that serves no document until Reload().
    explicit WebSession(std::string path, Loader loader = {});
    explicit WebSession(LoadedDocument document);
    ~WebSession();

    WebSession(const WebSession &) = delete;
    WebSession &operator=(const WebSession &) = delete;
    WebSession(WebSession &&) = delete;
    WebSession &operator=(WebSession &&) = delete;

    bool Start();
    // Schedules loading another ABC. Rejected while a load is already running.
    // While the new document loads, the previous one keeps serving requests.
    bool Reload(std::string path);
    void Cancel();
    void Wait();
    WebSessionStatus Status() const;
    std::string StatusJson() const;
    std::shared_ptr<const WebApi> Api() const;

    // Runs on the session's load thread whenever a load ends with a newly
    // published document: the initial load and every successful reload. A
    // path that fails to open never reaches it, so recent-file tracking and
    // similar bookkeeping record only documents that actually opened.
    using DocumentOpenedCallback = std::function<void(const std::string &path)>;
    void SetDocumentOpenedCallback(DocumentOpenedCallback callback);

private:
    void Run(std::string path, CancellationToken token) noexcept;
    void SetStage(const char *stage);
    void Fail(ErrorCode code, std::string_view message) noexcept;

    std::string path_;
    Loader loader_;
    DocumentOpenedCallback onDocumentOpened_;
    mutable std::mutex mutex_;
    WebSessionStatus status_;
    std::shared_ptr<const WebApi> api_;
    bool started_ {false};
    std::shared_ptr<CancellationSource> cancellation_;
    TaskRunner runner_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_WEB_API_WEB_SESSION_H
