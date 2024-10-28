/**
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#ifndef LIBABCKIT_SRC_ADAPTER_STATIC_ABCKIT_STATIC_H
#define LIBABCKIT_SRC_ADAPTER_STATIC_ABCKIT_STATIC_H

#include "libabckit/include/c/ir_core.h"
#include "libabckit/include/c/metadata_core.h"

namespace libabckit {

AbckitFile *OpenAbcStatic(const char *path);
void WriteAbcStatic(AbckitFile *file, const char *path);
void DestroyGraphStatic(AbckitGraph *graph);
void CloseFileStatic(AbckitFile *file);

}  // namespace libabckit

#endif