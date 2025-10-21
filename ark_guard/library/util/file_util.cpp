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

#include "file_util.h"

#include <fstream>
#include <sstream>

#include "libarkbase/os/filesystem.h"

#include "assert_util.h"
#include "error.h"
#include "logger.h"

std::string ark::guard::FileUtil::GetFileContent(const std::string &filePath)
{
    std::string content;
    std::string realPath = ark::os::GetAbsolutePath(filePath);
    if (realPath.empty()) {
        LOG_W << "get absolute path failed, path:" << filePath;
        return content;
    }

    std::ifstream inputStream(realPath);
    if (!inputStream.is_open()) {
        LOG_W << "open real file path failed, path:" << realPath;
        return content;
    }

    std::stringstream ss;
    ss << inputStream.rdbuf();
    content = ss.str();
    inputStream.close();
    return content;
}

void ark::guard::FileUtil::WriteFile(const std::string &filePath, const std::string &content)
{
    std::ofstream ofs;
    ofs.open(filePath, std::ios::trunc | std::ios::out);
    ARK_GUARD_ASSERT(!ofs.is_open(), ErrorCode::GENERIC_ERROR, "can not open file, invalid filePath:" + filePath);
    ofs << content;
    ofs.close();
}
