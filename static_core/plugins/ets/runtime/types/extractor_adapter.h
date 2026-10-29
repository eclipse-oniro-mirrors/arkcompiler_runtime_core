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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_TYPES_EXTRACTOR_ADAPTER_H_
#define PANDA_PLUGINS_ETS_RUNTIME_TYPES_EXTRACTOR_ADAPTER_H_

#include <memory>
#include <string>

#ifdef ENABLE_ABILITY_EXTRACTOR
#include "extractor.h"
#include "file_mapper.h"
#include "zip_file.h"

namespace ark::ets {

using Extractor = OHOS::AbilityBase::Extractor;
using FileMapper = OHOS::AbilityBase::FileMapper;
using FileMapperType = OHOS::AbilityBase::FileMapperType;
using ZipFile = OHOS::AbilityBase::ZipFile;

/**
 * @brief Bridge onto the foundation extractor: unique_ptr -> shared_ptr ownership only.
 *
 * Deliberately does NOT call `SetAutoReleaseMem(true)`: a SAFE_ABC mapping must outlive the
 * extractor and every FileMapper copy, because the runtime keeps executing bytecode from it
 * (`OpenPandaFileFromSecureMemory` installs an empty deleter on the mapping).
 */
std::shared_ptr<FileMapper> GetSafeDataAsShared(Extractor &extractor, const std::string &entryName);

/**
 * @brief Hsp entry reading, which the foundation extractor does not provide.
 *
 * Reads @a entryName without the `IsEntryDataConsistent` check, mirroring the historical
 * `GetSafeDataForHsp` semantics: only genuine `.hsp` targets use the unchecked reader.
 * The entry-name derivation (the hsp path style) lives in ets_abc_package.cpp.
 */
std::shared_ptr<FileMapper> GetSafeDataForHspAsShared(ZipFile &zipFile, const std::string &entryName);

}  // namespace ark::ets
#endif  // ENABLE_ABILITY_EXTRACTOR

#endif  // PANDA_PLUGINS_ETS_RUNTIME_TYPES_EXTRACTOR_ADAPTER_H_
