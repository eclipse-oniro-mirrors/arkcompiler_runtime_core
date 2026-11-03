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

#include "plugins/ets/runtime/types/ets_abc_package.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "libarkbase/utils/logger.h"

#ifdef ENABLE_ABILITY_EXTRACTOR
#include "plugins/ets/runtime/types/extractor_adapter.h"
#endif

namespace ark::ets {

bool EndsWith(std::string_view path, std::string_view suffix)
{
    return path.length() >= suffix.length() &&
           path.compare(path.length() - suffix.length(), suffix.length(), suffix.data()) == 0;
}

std::string GetAbcPathFromPackagePath(std::string_view packagePath, std::string_view suffix)
{
    std::string derived(packagePath.substr(0, packagePath.length() - suffix.length()));
    derived.append(PACKAGE_ABC_PATH.data(), PACKAGE_ABC_PATH.length());
    return derived;
}

namespace {
// NOLINTNEXTLINE(modernize-avoid-c-arrays)
constexpr char EXT_NAME_ABC[] = ".abc";
constexpr const char *LOCAL_CODE_PATH = "/data/storage/el1/bundle";
constexpr std::array<const char *, 3> LOCAL_HSP_CODE_PATHS = {"/data/storage/el1/bundle", "/system/app/appServiceFwk",
                                                              "/system/app/shared_bundles"};
constexpr const char *FILE_SEPARATOR = "/";
constexpr int MAX_INNER_HSP_SLASHES = 3;

bool StringEndWith(const std::string &str, const char *endStr)
{
    const size_t endStrLen = strlen(endStr);
    return (str.length() >= endStrLen) && (str.compare(str.length() - endStrLen, endStrLen, endStr) == 0);
}

bool IsInnerHspPath(const std::string &path)
{
    return std::count(path.begin(), path.end(), '/') <= MAX_INNER_HSP_SLASHES;
}

// std::regex_replace(src, std::regex(what), "") on these plain path constants,
// without the std::regex template cost.
std::string RemoveAllOccurrences(const std::string &src, const char *what)
{
    const size_t whatLen = strlen(what);
    if (whatLen == 0) {
        return src;
    }
    std::string dst;
    size_t pos = 0;
    while (pos <= src.length()) {
        const size_t hit = src.find(what, pos);
        if (hit == std::string::npos) {
            dst.append(src, pos, std::string::npos);
            break;
        }
        dst.append(src, pos, hit - pos);
        pos = hit + whatLen;
    }
    return dst;
}

// Derives the hsp entry name from the hsp path, e.g.
// "/data/storage/el1/bundle/com.example.hsp/library/ets/module_static.abc" -> "ets/module_static.abc".
std::string GetRelativePathForHsp(const std::string &srcPath)
{
    if (srcPath.empty() || srcPath[0] != '/') {
        return srcPath;
    }
    std::string relativePath = srcPath;
    for (const auto *path : LOCAL_HSP_CODE_PATHS) {
        std::string tempRelativePath = RemoveAllOccurrences(srcPath, path);
        if (tempRelativePath != srcPath) {
            relativePath = std::move(tempRelativePath);
            break;
        }
    }
    if (IsInnerHspPath(relativePath)) {
        if (relativePath.find(FILE_SEPARATOR) == 0) {
            relativePath = relativePath.substr(1);
            relativePath = relativePath.substr(relativePath.find(FILE_SEPARATOR) + 1);
        }
    } else {
        if (relativePath.find(FILE_SEPARATOR) == 0) {
            std::string bundleName = relativePath.substr(1);
            std::string moduleName = bundleName;
            if (srcPath.find(LOCAL_CODE_PATH) == 0) {
                moduleName = bundleName.substr(bundleName.find(FILE_SEPARATOR) + 1);
            }
            std::string hspName = moduleName.substr(moduleName.find(FILE_SEPARATOR) + 1);
            relativePath = hspName.substr(hspName.find(FILE_SEPARATOR) + 1);
        }
    }
    return relativePath;
}

// A `.hsp` records its abc under the derived entry path, any other package under the fixed name.
std::string GetPackageEntryName(const std::string &packagePath, std::string_view suffix)
{
    const std::string abcPath = GetAbcPathFromPackagePath(packagePath, suffix);
    if (suffix == HSP_SUFFIX) {
        return GetRelativePathForHsp(abcPath);
    }
    return std::string(PACKAGE_ABC_ENTRY);
}

// Zip local file header layout, as the historical extractortool's zip_file.h defined it.
struct __attribute__((packed)) LocalHeader {
    uint32_t signature = 0;
    uint16_t versionNeeded = 0;
    uint16_t flags = 0;
    uint16_t compressionMethod = 0;
    uint16_t modifiedTime = 0;
    uint16_t modifiedDate = 0;
    uint32_t crc = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint16_t nameSize = 0;
    uint16_t extraSize = 0;
};
constexpr size_t LOCAL_HEADER_FIXED_SIZE = 30U;
static_assert(sizeof(LocalHeader) == LOCAL_HEADER_FIXED_SIZE);

struct __attribute__((packed)) CentralHeader {
    uint32_t signature = 0;
    uint16_t versionMade = 0;
    uint16_t versionNeeded = 0;
    uint16_t flags = 0;
    uint16_t compressionMethod = 0;
    uint16_t modifiedTime = 0;
    uint16_t modifiedDate = 0;
    uint32_t crc = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint16_t nameSize = 0;
    uint16_t extraSize = 0;
    uint16_t commentSize = 0;
    uint16_t diskStart = 0;
    uint16_t internalAttr = 0;
    uint32_t externalAttr = 0;
    uint32_t localHeaderOffset = 0;
};
constexpr size_t CENTRAL_HEADER_FIXED_SIZE = 46U;
static_assert(sizeof(CentralHeader) == CENTRAL_HEADER_FIXED_SIZE);

struct __attribute__((packed)) EndOfCentralDirectory {
    uint32_t signature = 0;
    uint16_t diskNumber = 0;
    uint16_t centralDirDisk = 0;
    uint16_t entriesOnDisk = 0;
    uint16_t totalEntries = 0;
    uint32_t centralDirSize = 0;
    uint32_t centralDirOffset = 0;
    uint16_t commentSize = 0;
};
constexpr size_t EOCD_FIXED_SIZE = 22U;
static_assert(sizeof(EndOfCentralDirectory) == EOCD_FIXED_SIZE);

struct __attribute__((packed)) DataDescriptor {
    uint32_t signature = 0;
    uint32_t crc = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
};
constexpr size_t DATA_DESCRIPTOR_FIXED_SIZE = 16U;
static_assert(sizeof(DataDescriptor) == DATA_DESCRIPTOR_FIXED_SIZE);

struct CentralEntryInfo {
    uint32_t localHeaderOffset = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint32_t crc = 0;
    uint16_t compressionMethod = 0;
};

constexpr uint32_t LOCAL_HEADER_SIGNATURE = 0x04034b50U;
constexpr uint32_t CENTRAL_HEADER_SIGNATURE = 0x02014b50U;
constexpr uint32_t EOCD_SIGNATURE = 0x06054b50U;
constexpr uint16_t FLAG_DATA_DESC = 0x0008U;
constexpr uint32_t MAX_FILE_NAME = 4096U;
constexpr size_t EOCD_MAX_COMMENT_SIZE = 65535U;

bool SeekPackage(FILE *package, uint32_t offset)
{
#ifdef PANDA_TARGET_WINDOWS
    return _fseeki64(package, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    return fseeko(package, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

bool GetFileSize(FILE *package, uint64_t *size)
{
#ifdef PANDA_TARGET_WINDOWS
    if (_fseeki64(package, 0, SEEK_END) != 0) {
        return false;
    }
    const __int64 end = _ftelli64(package);
#else
    if (fseeko(package, 0, SEEK_END) != 0) {
        return false;
    }
    const off_t end = ftello(package);
#endif
    if (end < 0) {
        return false;
    }
    *size = static_cast<uint64_t>(end);
    return true;
}

bool ReadLocalHeader(FILE *package, uint32_t offset, LocalHeader *header, std::string *name)
{
    if (!SeekPackage(package, offset) || fread(header, sizeof(LocalHeader), 1, package) != 1) {
        return false;
    }
    if (header->signature != LOCAL_HEADER_SIGNATURE || header->nameSize >= MAX_FILE_NAME) {
        return false;
    }
    name->resize(header->nameSize);
    return fread(name->data(), 1, header->nameSize, package) == header->nameSize;
}

bool ReadCentralHeader(FILE *package, uint32_t offset, CentralHeader *header, std::string *name)
{
    if (!SeekPackage(package, offset) || fread(header, sizeof(CentralHeader), 1, package) != 1) {
        return false;
    }
    if (header->signature != CENTRAL_HEADER_SIGNATURE || header->nameSize >= MAX_FILE_NAME) {
        return false;
    }
    name->resize(header->nameSize);
    return fread(name->data(), 1, header->nameSize, package) == header->nameSize;
}

bool FindEocd(FILE *package, uint32_t *centralOffset, uint32_t *centralSize)
{
    uint64_t fileSize = 0;
    if (!GetFileSize(package, &fileSize) || fileSize < EOCD_FIXED_SIZE) {
        return false;
    }
    const uint64_t tailSize = std::min<uint64_t>(fileSize, EOCD_FIXED_SIZE + EOCD_MAX_COMMENT_SIZE);
    std::string tail(tailSize, '\0');
    if (!SeekPackage(package, static_cast<uint32_t>(fileSize - tailSize)) ||
        fread(tail.data(), 1, tailSize, package) != tailSize) {
        return false;
    }
    for (size_t pos = tailSize - EOCD_FIXED_SIZE + 1; pos-- > 0;) {
        if (tail[pos] != 'P') {
            continue;
        }
        EndOfCentralDirectory eocd;
        const uint64_t eocdFilePos = fileSize - tailSize + pos;
        if (!SeekPackage(package, static_cast<uint32_t>(eocdFilePos)) || fread(&eocd, sizeof(eocd), 1, package) != 1) {
            return false;
        }
        if (eocd.signature != EOCD_SIGNATURE) {
            continue;
        }
        if (eocd.centralDirOffset <= eocdFilePos && eocd.centralDirSize <= eocdFilePos - eocd.centralDirOffset) {
            *centralOffset = eocd.centralDirOffset;
            *centralSize = eocd.centralDirSize;
            return true;
        }
    }
    return false;
}

// Selects the entry like the historical extractortool did: the LAST central-directory record
// under @a entryName. A malformed archive (no EOCD, unparseable or boundary-exceeding central
// records) returns OPEN_FAILED; a valid archive without the sought entry returns NO_ABC_ENTRY.
PackageReadResult FindLastCentralEntry(FILE *package, const std::string &entryName, CentralEntryInfo *info)
{
    uint32_t centralOffset = 0;
    uint32_t centralSize = 0;
    if (!FindEocd(package, &centralOffset, &centralSize)) {
        return PackageReadResult::OPEN_FAILED;
    }
    const uint64_t end = static_cast<uint64_t>(centralOffset) + centralSize;
    bool found = false;
    uint64_t pos = centralOffset;
    while (pos < end) {
        if (pos > UINT32_MAX || end - pos < CENTRAL_HEADER_FIXED_SIZE) {
            return PackageReadResult::OPEN_FAILED;
        }
        CentralHeader header;
        std::string name;
        if (!ReadCentralHeader(package, static_cast<uint32_t>(pos), &header, &name)) {
            return PackageReadResult::OPEN_FAILED;
        }
        const uint64_t recordSize = CENTRAL_HEADER_FIXED_SIZE + header.nameSize + header.extraSize + header.commentSize;
        if (recordSize > end - pos) {
            return PackageReadResult::OPEN_FAILED;
        }
        if (name == entryName) {
            info->localHeaderOffset = header.localHeaderOffset;
            info->compressedSize = header.compressedSize;
            info->uncompressedSize = header.uncompressedSize;
            info->crc = header.crc;
            info->compressionMethod = header.compressionMethod;
            found = true;
        }
        pos += recordSize;
    }
    return found ? PackageReadResult::OK : PackageReadResult::NO_ABC_ENTRY;
}

// The historical extractortool's IsEntryDataConsistent: walking the local headers sequentially
// from the archive start, the first header named @a entryName must sit at @a localHeaderOffset.
bool IsEntryDataConsistent(FILE *package, const std::string &entryName, uint32_t localHeaderOffset)
{
    uint32_t currentOffset = 0;
    while (currentOffset <= localHeaderOffset) {
        LocalHeader header;
        std::string name;
        if (!ReadLocalHeader(package, currentOffset, &header, &name)) {
            return false;
        }
        if (name == entryName) {
            return currentOffset == localHeaderOffset;
        }
        if ((header.flags & FLAG_DATA_DESC) != 0U) {
            return false;
        }
        const uint64_t nextOffset = static_cast<uint64_t>(currentOffset) + LOCAL_HEADER_FIXED_SIZE + header.nameSize +
                                    header.extraSize + header.compressedSize;
        if (nextOffset > UINT32_MAX) {
            return false;
        }
        currentOffset = static_cast<uint32_t>(nextOffset);
    }
    return false;
}

#ifdef ENABLE_ABILITY_EXTRACTOR
// The foundation GetSafeData tolerates DATA_DESCRIPTOR_SET and READ_ERROR, which the historical
// reader rejected; gate `.hap` / `.hqf` with the strict traversal first (`.hsp` stays exempt).
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
PackageReadResult ReadPackageAbcByFoundation(const std::string &packagePath, std::string_view suffix,
                                             const std::string &entryName, const std::string &openedAs,
                                             std::unique_ptr<const panda_file::File> &outPf)
{
    if (suffix != HSP_SUFFIX) {
        FILE *package = fopen(packagePath.c_str(), "rbe");
        if (package == nullptr) {
            return PackageReadResult::OPEN_FAILED;
        }
        CentralEntryInfo central;
        const auto result = FindLastCentralEntry(package, entryName, &central);
        if (result != PackageReadResult::OK) {
            (void)fclose(package);
            return result;
        }
        bool consistent = IsEntryDataConsistent(package, entryName, central.localHeaderOffset);
        (void)fclose(package);
        if (!consistent) {
            return PackageReadResult::NO_ABC_ENTRY;
        }
    }
    std::shared_ptr<FileMapper> safeData;
    if (suffix == HSP_SUFFIX) {
        ZipFile packageZip(packagePath);
        if (!packageZip.Open()) {
            return PackageReadResult::OPEN_FAILED;
        }
        safeData = GetSafeDataForHspAsShared(packageZip, entryName);
    } else {
        auto extractor = std::make_shared<Extractor>(packagePath);
        if (extractor == nullptr || !extractor->Init()) {
            return PackageReadResult::OPEN_FAILED;
        }
        safeData = GetSafeDataAsShared(*extractor, entryName);
    }
    if (safeData == nullptr) {
        return PackageReadResult::NO_ABC_ENTRY;
    }

    outPf = panda_file::OpenPandaFileFromSecureMemory(safeData->GetDataPtr(), safeData->GetDataLen(), openedAs);
    return outPf == nullptr ? PackageReadResult::OPEN_FAILED : PackageReadResult::OK;
}
#else
#ifdef PANDA_TARGET_OHOS
// XPM-backed file mapping like the historical extractor: OpenPandaFileFromSecureMemory validates
// the address against /proc/self/xpm_region on XPM-enabled systems. Stored entries map their
// payload, compressed entries map compressed bytes and fail the abc validation downstream.
constexpr uint32_t MAP_XPM = 0x40U;

// NOLINTNEXTLINE(modernize-avoid-c-arrays)
uint8_t *MapZipEntry(FILE *package, uint64_t dataOffset, uint32_t mapLength, size_t *entrySize)
{
    const size_t pageSize = os::mem::GetPageSize();
    const uint64_t adjust = dataOffset % pageSize;
    void *base = mmap(nullptr, mapLength + adjust, os::mem::MMAP_PROT_READ, os::mem::MMAP_FLAG_PRIVATE | MAP_XPM,
                      fileno(package), static_cast<off_t>(dataOffset - adjust));
    if (base == MAP_FAILED) {
        LOG(ERROR, RUNTIME) << "Cannot map package entry at offset " << dataOffset;
        return nullptr;
    }
    *entrySize = mapLength;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return reinterpret_cast<uint8_t *>(base) + adjust;
}
#else
// The mapping deliberately outlives the caller (SAFE_ABC lifetime) and carries no allocator
// bookkeeping.
// NOLINTNEXTLINE(modernize-avoid-c-arrays)
uint8_t *ReadZipEntryData(FILE *package, uint32_t dataOffset, uint32_t size, size_t *mapSize)
{
    const size_t pageSize = os::mem::GetPageSize();
    *mapSize = (size + pageSize - 1) & ~(pageSize - 1);
    auto *data = reinterpret_cast<uint8_t *>(os::mem::MapRWAnonymousRaw(*mapSize, false));
    if (data == nullptr) {
        return nullptr;
    }
    if (!SeekPackage(package, dataOffset) || fread(data, 1, size, package) != size) {
        (void)os::mem::UnmapRaw(data, *mapSize);
        return nullptr;
    }
    return data;
}
#endif

constexpr uint32_t DATA_DESC_SIGNATURE = 0x08074b50U;
constexpr uint16_t COMPRESSION_METHOD_DEFLATED = 8U;

// The historical extractortool's CheckCoherencyLocalHeader + CheckDataDesc: the local header
// must be coherent with the central record (method, CRC, sizes), validating the trailing data
// descriptor when the DD flag is set instead of using the local header's own values.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool CheckEntryCoherency(FILE *package, const std::string &entryName, const CentralEntryInfo &central,
                         uint32_t *dataOffset)
{
    LocalHeader header;
    std::string name;
    if (!ReadLocalHeader(package, central.localHeaderOffset, &header, &name) || name != entryName) {
        return false;
    }
    if (header.compressionMethod != central.compressionMethod ||
        (central.compressionMethod != 0U && central.compressionMethod != COMPRESSION_METHOD_DEFLATED)) {
        return false;
    }
    *dataOffset = central.localHeaderOffset + LOCAL_HEADER_FIXED_SIZE + header.nameSize + header.extraSize;
    uint32_t crc = header.crc;
    uint32_t compressedSize = header.compressedSize;
    uint32_t uncompressedSize = header.uncompressedSize;
    if ((header.flags & FLAG_DATA_DESC) != 0U) {
        DataDescriptor desc;
        if (!SeekPackage(package, *dataOffset + central.compressedSize) ||
            fread(&desc, sizeof(desc), 1, package) != 1) {
            return false;
        }
        if (desc.signature != DATA_DESC_SIGNATURE) {
            return false;
        }
        crc = desc.crc;
        compressedSize = desc.compressedSize;
        uncompressedSize = desc.uncompressedSize;
    }
    return central.crc == crc && central.compressedSize == compressedSize &&
           central.uncompressedSize == uncompressedSize;
}

// Locates and validates the abc entry; `.hap` / `.hqf` must pass the consistency traversal,
// `.hsp` stays exempt. On success @a dataOffset and @a compressedSize describe the entry payload.
PackageReadResult FindAndValidateEntry(FILE *package, std::string_view suffix, const std::string &entryName,
                                       uint32_t *dataOffset, uint32_t *compressedSize)
{
    CentralEntryInfo central;
    const auto result = FindLastCentralEntry(package, entryName, &central);
    if (result != PackageReadResult::OK) {
        return result;
    }
    if (suffix != HSP_SUFFIX && !IsEntryDataConsistent(package, entryName, central.localHeaderOffset)) {
        LOG(ERROR, RUNTIME) << "Package entry '" << entryName << "' fails the local header consistency check";
        return PackageReadResult::NO_ABC_ENTRY;
    }
    if (!CheckEntryCoherency(package, entryName, central, dataOffset)) {
        LOG(ERROR, RUNTIME) << "Package entry '" << entryName << "' fails the coherency check";
        return PackageReadResult::NO_ABC_ENTRY;
    }
    *compressedSize = central.compressedSize;
    return PackageReadResult::OK;
}

PackageReadResult ReadPackageAbcByBuiltin(const std::string &packagePath, std::string_view suffix,
                                          const std::string &entryName, const std::string &openedAs,
                                          std::unique_ptr<const panda_file::File> &outPf)
{
#ifdef PANDA_TARGET_WINDOWS
    constexpr char const *MODE = "rb";
#else
    constexpr char const *MODE = "rbe";
#endif
    FILE *package = fopen(packagePath.c_str(), MODE);
    if (package == nullptr) {
        LOG(ERROR, RUNTIME) << "Cannot open package '" << packagePath << "'";
        return PackageReadResult::OPEN_FAILED;
    }
    uint32_t dataOffset = 0;
    uint32_t compressedSize = 0;
    const auto result = FindAndValidateEntry(package, suffix, entryName, &dataOffset, &compressedSize);
    if (result != PackageReadResult::OK) {
        (void)fclose(package);
        return result;
    }
    size_t entrySize = 0;
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    uint8_t *entryData = nullptr;
#ifdef PANDA_TARGET_OHOS
    entryData = MapZipEntry(package, dataOffset, compressedSize, &entrySize);
#else
    size_t mapSize = 0;
    entryData = ReadZipEntryData(package, dataOffset, compressedSize, &mapSize);
    entrySize = compressedSize;
#endif
    (void)fclose(package);
    if (entryData == nullptr) {
        LOG(ERROR, RUNTIME) << "Cannot read package entry '" << entryName << "' from '" << packagePath << "'";
        return PackageReadResult::NO_ABC_ENTRY;
    }
    outPf = panda_file::OpenPandaFileFromSecureMemory(entryData, entrySize, openedAs);
    if (outPf == nullptr) {
#ifndef PANDA_TARGET_OHOS
        (void)os::mem::UnmapRaw(entryData, mapSize);
#endif
        return PackageReadResult::OPEN_FAILED;
    }
    return PackageReadResult::OK;
}
#endif
}  // namespace

PackageReadResult ReadPackageAbc(const std::string &packagePath, std::string_view suffix,
                                 std::unique_ptr<const panda_file::File> &outPf, std::string_view fileLabel)
{
    const std::string entryName = GetPackageEntryName(packagePath, suffix);
    if (!StringEndWith(entryName, EXT_NAME_ABC)) {
        return PackageReadResult::NO_ABC_ENTRY;
    }
    const std::string abcPathForLabel = GetAbcPathFromPackagePath(packagePath, suffix);
    const std::string openedAs(fileLabel.empty() ? std::string_view(abcPathForLabel) : fileLabel);

#ifdef ENABLE_ABILITY_EXTRACTOR
    return ReadPackageAbcByFoundation(packagePath, suffix, entryName, openedAs, outPf);
#else
    return ReadPackageAbcByBuiltin(packagePath, suffix, entryName, openedAs, outPf);
#endif
}

}  // namespace ark::ets
