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

#include "plugins/ets/runtime/types/extractor_adapter.h"

#ifdef ENABLE_ABILITY_EXTRACTOR

namespace ark::ets {

std::shared_ptr<FileMapper> GetSafeDataAsShared(Extractor &extractor, const std::string &entryName)
{
    return std::shared_ptr<FileMapper>(extractor.GetSafeData(entryName).release());
}

std::shared_ptr<FileMapper> GetSafeDataForHspAsShared(ZipFile &zipFile, const std::string &entryName)
{
    return std::shared_ptr<FileMapper>(zipFile.CreateFileMapper(entryName, FileMapperType::SAFE_ABC).release());
}

}  // namespace ark::ets

#endif  // ENABLE_ABILITY_EXTRACTOR
