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

#include "web/browser_application.h"

#include "platform/browser_launcher.h"
#include "web/api/recent_files.h"
#include "web/api/web_session.h"
#include "web/host/loopback_host.h"

#include <iostream>
#include <string>

namespace panda::bytecode_explorer {

int RunBrowser(const std::string &path, bool openBrowser)
{
    // Declared before the session: the session's document-opened callback
    // captures this reference and may fire while the session tears down its
    // load thread, so the recents list must outlive it.
    RecentFiles recents(RecentFiles::DefaultStoragePath());
    Preferences preferences(Preferences::DefaultStoragePath());
    WebSession session(path);
    session.SetDocumentOpenedCallback([&recents](const std::string &opened) { recents.Record(opened); });
    LoopbackHost host(session, recents, preferences);
    auto endpoint = host.Start();
    if (!endpoint.HasValue()) {
        std::cerr << "Cannot start local browser session: " << endpoint.Error().Message() << '\n';
        return 1;
    }

    const auto url = endpoint.Value().Url();
    std::cout << "Ark Bytecode Explorer is available at:\n" << url << "\nPress Ctrl+C to stop.\n" << std::flush;
    (void)session.Start();
    if (openBrowser) {
        const auto launch = BrowserLauncher().Open(url);
        if (launch.Code() != ErrorCode::NONE) {
            std::cerr << "Could not open the default browser automatically: " << launch.Message() << '\n';
        }
    }

    const auto status = host.Run();
    session.Cancel();
    session.Wait();
    if (status.Code() != ErrorCode::NONE) {
        std::cerr << "Local browser session failed: " << status.Message() << '\n';
        return 1;
    }
    return session.Status().state == WebSessionState::FAILED ? 1 : 0;
}

}  // namespace panda::bytecode_explorer
