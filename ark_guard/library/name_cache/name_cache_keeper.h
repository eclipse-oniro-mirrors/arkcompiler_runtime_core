/**
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#ifndef ARK_GUARD_NAME_CACHE_NAME_CACHE_KEEPER_H
#define ARK_GUARD_NAME_CACHE_NAME_CACHE_KEEPER_H

#include "name_cache_constants.h"

#include "abckit_wrapper/file_view.h"

namespace ark::guard {

/**
 * @brief NameCacheKeeper
 */
class NameCacheKeeper {
public:
    /**
     * @brief constructor, initializes fileView
     * @param fileView abc fileView
     */
    explicit NameCacheKeeper(abckit_wrapper::FileView &fileView) : fileView_(fileView) {}

    /**
     * @brief set module name cache to fileView
     * @param moduleCaches module name caches
     */
    void Process(const ObjectNameCacheTable &moduleCaches);

private:
    void ProcessObjectNameCaches(const std::string &packageName, const ObjectNameCacheTable &objectCaches);

    void ProcessMemberNameCaches(const std::string &packageName,
                                 const std::unordered_map<std::string, std::string> &methodCaches);

    abckit_wrapper::FileView &fileView_;
};
}  // namespace ark::guard

#endif  // ARK_GUARD_NAME_CACHE_NAME_CACHE_KEEPER_H
