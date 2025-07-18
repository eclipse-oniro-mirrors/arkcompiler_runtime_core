/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "errorconstructandcopy_fuzzer.h"
#include <fuzzer/FuzzedDataProvider.h>
#include "libpandabase/os/error.h"

namespace OHOS {
    void ErrorConstructAndCopyFuzzTest(const uint8_t* data, size_t size)
    {
        if (data == nullptr || size < sizeof(int)) {
            return;
        }
        FuzzedDataProvider fdp(data, size);
        // init error with int
        int err = fdp.ConsumeIntegral<int>();
        panda::os::Error error_(err);
        panda::os::Error error_copy(error_);
    }
} // namespace OHOS

/* Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    /* Run your code on data */
    OHOS::ErrorConstructAndCopyFuzzTest(data, size);
    return 0;
}