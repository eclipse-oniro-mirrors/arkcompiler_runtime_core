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

#include "core/document/abc_document.h"

#include "libpandafile/file.h"
#include "file_format_version.h"
#include "os/file.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <utility>

#if defined(PANDA_TARGET_WINDOWS)
#include <fcntl.h>
#include <io.h>
#endif

namespace panda::bytecode_explorer {
namespace {

std::optional<panda_file::File::Header> ReadHeader(std::ifstream &input)
{
    panda_file::File::Header header {};
    input.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (!input) {
        return std::nullopt;
    }

    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || static_cast<uint64_t>(size) != header.file_size || header.magic != panda_file::File::MAGIC) {
        return std::nullopt;
    }
    return header;
}

// Opens the ABC byte stream for header probing. Returns nullopt when the path cannot be
// opened at all; on Windows the converted native path is stored in *nativePath for the
// later archive open.
std::optional<std::ifstream> OpenAbcInputStream(const std::string &path, std::filesystem::path *nativePath)
{
#if defined(PANDA_TARGET_WINDOWS)
    try {
        *nativePath = std::filesystem::u8path(path);
    } catch (const std::runtime_error &) {
        return std::nullopt;
    }
    std::ifstream input(*nativePath, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    return input;
#else
    (void)nativePath;
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    return input;
#endif
}

// Validates the probed header against the supported dynamic-ABC versions. Returns the
// failure status, or nullopt when the header is acceptable.
std::optional<Status> ValidateAbcHeader(const panda_file::File::Header &header, const std::string &path)
{
    if (header.version[panda_file::File::FILE_TYPE_OFFSET] == panda_file::File::FILE_TYPE_STATIC_FLAG ||
        header.version == panda_file::File::OLD_STATIC_VERSION) {
        return Status(ErrorCode::UNSUPPORTED_FORMAT, "Static ABC files are not supported: " + path);
    }
    if (header.version < panda_file::minVersion || header.version > panda_file::version ||
        panda_file::incompatibleVersion.count(header.version) != 0U) {
        return Status(ErrorCode::UNSUPPORTED_FORMAT,
                      "Unsupported dynamic ABC version " + panda_file::GetVersion(header.version) + ": " + path);
    }
    return std::nullopt;
}

// Opens the validated archive through the platform-specific panda-file entry point.
std::unique_ptr<const panda_file::File> OpenPandaArchive(const std::string &path,
                                                         const std::filesystem::path &nativePath)
{
#if defined(PANDA_TARGET_WINDOWS)
    const int fd = _wopen(nativePath.c_str(), _O_RDONLY | _O_BINARY);
    const os::file::File nativeFile(fd);
    if (!nativeFile.IsValid()) {
        return nullptr;
    }
    os::file::FileHolder fileHolder(nativeFile);
    const auto actualSize = nativeFile.GetFileSize();
    return actualSize ? panda_file::File::OpenUncompressedArchive(fd, path, actualSize.Value(), 0U) : nullptr;
#else
    (void)nativePath;
    return panda_file::File::Open(path);
#endif
}

}  // namespace

AbcDocument::AbcDocument(ConstructorToken, DocumentSummary summary, std::unique_ptr<const panda_file::File> file)
    : summary_(std::move(summary)), file_(std::move(file))
{
}

AbcDocument::~AbcDocument() = default;

AbcDocument::OpenResult AbcDocument::Open(const std::string &path)
{
    std::filesystem::path nativePath;
    auto input = OpenAbcInputStream(path, &nativePath);
    if (!input.has_value()) {
        return OpenResult::Failure(Status(ErrorCode::FILE_NOT_FOUND, "Cannot open ABC file: " + path));
    }
    const auto header = ReadHeader(*input);
    input->close();

    if (!header.has_value()) {
        return OpenResult::Failure(Status(ErrorCode::INVALID_FORMAT, "Invalid or corrupted ABC file: " + path));
    }
    const auto invalidVersion = ValidateAbcHeader(*header, path);
    if (invalidVersion.has_value()) {
        return OpenResult::Failure(*invalidVersion);
    }

    auto file = OpenPandaArchive(path, nativePath);
    if (file == nullptr) {
        return OpenResult::Failure(Status(ErrorCode::INVALID_FORMAT, "Invalid or corrupted ABC file: " + path));
    }

    const auto *openedHeader = file->GetHeader();

    DocumentSummary summary;
    summary.path = path;
    summary.fileSize = openedHeader->file_size;
    summary.checksum = openedHeader->checksum;
    summary.version = openedHeader->version;
    // Best-effort content scan; a malformed interior only downgrades the
    // build verdict to UNKNOWN and never fails the open.
    summary.build = DetectAbcBuildMode(*file);

    auto document = std::make_shared<const AbcDocument>(ConstructorToken {}, std::move(summary), std::move(file));
    return OpenResult::Success(std::move(document));
}

}  // namespace panda::bytecode_explorer
