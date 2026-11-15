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

#ifndef BYTECODE_EXPLORER_WEB_HOST_REQUEST_ROUTER_H
#define BYTECODE_EXPLORER_WEB_HOST_REQUEST_ROUTER_H

#include "web/api/preferences.h"
#include "web/api/recent_files.h"
#include "web/api/web_session.h"
#include "web/host/http_request.h"
#include "web/host/pick_controller.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer {

struct HttpResponse {
    int status {500};
    std::string reason {"Internal Server Error"};
    std::string contentType {"text/plain; charset=utf-8"};
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;

    std::string_view Header(std::string_view name) const;
    std::string Serialize() const;
};

class RequestRouter final {
public:
    RequestRouter(const WebSession &session, std::string token, PickController &pick, RecentFiles &recents,
                  Preferences &preferences, uint16_t port);

    HttpResponse Route(const HttpRequest &request) const;

private:
    bool IsAuthorized(const HttpRequest &request) const;
    // Side-effect routes must also carry a loopback Host header (DNS
    // rebinding hardening beyond the session token).
    bool IsLoopbackHost(const HttpRequest &request) const;
    HttpResponse RouteApi(const HttpRequest &request, const WebApi &api) const;
    // Document-backed API routes: require a READY session, then dispatch
    // through RouteApi.
    HttpResponse RouteSessionApi(const HttpRequest &request) const;
    HttpResponse RouteDocumentSwitch(const HttpRequest &request) const;
    HttpResponse RoutePreferences(const HttpRequest &request) const;

    const WebSession &session_;
    PickController &pick_;
    RecentFiles &recents_;
    Preferences &preferences_;
    std::string token_;
    std::string numericLoopbackHost_;
    std::string namedLoopbackHost_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_WEB_HOST_REQUEST_ROUTER_H
