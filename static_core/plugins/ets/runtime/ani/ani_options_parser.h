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
#ifndef PANDA_PLUGINS_ETS_RUNTIME_ANI_OPTIONS_PARSER_H
#define PANDA_PLUGINS_ETS_RUNTIME_ANI_OPTIONS_PARSER_H

#include <string>
#include <vector>

#include "ani/ani.h"

namespace ark::ets::ani {

/**
 * @brief Class represents ANI options parser
 *
 * Class unites panda runtime options and ANI specific options parsing
 *
 */
class ANIOptionsParser {
public:
    ANIOptionsParser(size_t optionsSize, const ani_option *inputOptions)
        : optionsSize_(optionsSize), inputOptions_(inputOptions)
    {
        Parse();
    }

    const std::vector<std::string> &GetRuntimeOptions()
    {
        return runtimeOptions_;
    }

    const std::vector<ani_option> &GetANIOptions()
    {
        return aniSpecificOptions_;
    }

private:
    void Parse()
    {
        const std::string prefix = "--ext:";

        for (size_t i = 0; i < optionsSize_; ++i) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            std::string option(inputOptions_[i].option);

            // NOTE: need to skip forbidden options, such as "--load-runtimes"
            if (option.size() >= prefix.size() && option.substr(0, prefix.size()) == prefix) {
                runtimeOptions_.push_back(option.substr(prefix.size()));
            }
        }
    }

    size_t optionsSize_;
    const ani_option *inputOptions_;
    std::vector<ani_option> aniSpecificOptions_;
    std::vector<std::string> runtimeOptions_;
};
}  // namespace ark::ets::ani

#endif  // PANDA_PLUGINS_ETS_RUNTIME_ANI_OPTIONS_PARSER_H
