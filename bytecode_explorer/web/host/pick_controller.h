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

#ifndef BYTECODE_EXPLORER_WEB_HOST_PICK_CONTROLLER_H
#define BYTECODE_EXPLORER_WEB_HOST_PICK_CONTROLLER_H

#include "platform/file_dialog.h"
#include "web/api/web_session.h"

#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace panda::bytecode_explorer {

// Drives "open another ABC" intents. The platform dialog blocks until the user
// closes it, so it runs on its own worker thread; the HTTP event loop keeps
// serving pick status polling and the still-published previous document.
// A confirmed pick or an open request switches the session document.
class PickController final {
public:
    using DialogOpener = std::function<FileDialogResult()>;

    explicit PickController(WebSession &session, DialogOpener opener = {});
    ~PickController();

    PickController(const PickController &) = delete;
    PickController &operator=(const PickController &) = delete;
    PickController(PickController &&) = delete;
    PickController &operator=(PickController &&) = delete;

    // Shows the file dialog on the worker thread. Rejected while a dialog is
    // already open or the previous dialog worker still runs.
    bool StartPick();

    enum class OpenStatus { ACCEPTED, BUSY, INVALID };

    // Switches the session to a server-side path (recent files, empty page).
    OpenStatus OpenPath(std::string path);

    struct State {
        std::string name {"idle"};  // idle|picking|selected|cancelled|failed
        std::string path;
        std::string error;
    };

    State QueryState() const;

private:
    void DialogWorker() noexcept;

    WebSession &session_;
    DialogOpener opener_;
    mutable std::mutex mutex_;
    State state_;
    std::thread worker_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_WEB_HOST_PICK_CONTROLLER_H
