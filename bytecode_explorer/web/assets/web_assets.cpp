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

#include "web/assets/web_assets.h"

#include "bytecode_explorer_web_assets.gen.h"

#include <algorithm>

namespace panda::bytecode_explorer {

std::optional<EmbeddedWebAsset> FindEmbeddedWebAsset(std::string_view path)
{
    if (path == "/index.html") {
        path = "/";
    }
    const auto found = std::find_if(EMBEDDED_WEB_ASSETS.begin(), EMBEDDED_WEB_ASSETS.end(),
                                    [path](const auto &asset) { return asset.path == path; });
    return found == EMBEDDED_WEB_ASSETS.end() ? std::nullopt : std::optional<EmbeddedWebAsset>(*found);
}

}  // namespace panda::bytecode_explorer
