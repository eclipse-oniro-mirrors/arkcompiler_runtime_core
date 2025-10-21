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

#ifndef ARK_GUARD_CONFIGURATION_CONFIGURATION_PARSER_H
#define ARK_GUARD_CONFIGURATION_CONFIGURATION_PARSER_H

#include <string>

#include "libarkbase/utils/json_parser.h"

#include "configuration.h"

namespace ark::guard {

/**
 * @brief ConfigurationParser
 * read and parse config json file
 */
class ConfigurationParser {
public:
    /**
     * @brief constructor, initializes the configPath
     * @param configPath config file path
     */
    explicit ConfigurationParser(std::string configPath) : configPath_(std::move(configPath)) {}

    /**
     * @brief read and parse config file.
     */
    void Parse(Configuration &configuration);

private:
    void ParseConfigurationFile(const std::string &content, Configuration &configuration);

    void ParsePrintSeedsOption(const JsonObject *object, Configuration &configuration);

    void ParseFileNameObfuscation(const JsonObject *object, Configuration &configuration);

    void ParseKeepOption(const JsonObject *object, Configuration &configuration);

    std::string configPath_;
};
}  // namespace ark::guard

#endif  // ARK_GUARD_CONFIGURATION_CONFIGURATION_PARSER_H