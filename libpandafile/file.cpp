/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#include "file_format_version.h"
#include "file-inl.h"
#include "os/file.h"
#include "os/mem.h"
#include "mem/mem.h"
#include "panda_cache.h"

#include "utils/hash.h"
#include "utils/logger.h"
#include "utils/utf.h"
#include "utils/span.h"
#include "zip_archive.h"
#include "trace/trace.h"
#if !PANDA_TARGET_WINDOWS
#include "securec.h"
#endif

#include <cerrno>
#include <cstring>

#include <algorithm>
#include <memory>
#include <string>
#include <variant>
#include <cstdio>
#include <map>
namespace panda::panda_file {

#ifndef EOK
constexpr int EOK = 0;
#endif  // EOK

// NOLINTNEXTLINE(readability-identifier-naming, modernize-avoid-c-arrays)
const char *ARCHIVE_FILENAME = "classes.abc";
// NOLINTNEXTLINE(readability-identifier-naming, modernize-avoid-c-arrays)
const char *ARCHIVE_SPLIT = "!/";

const std::array<uint8_t, File::MAGIC_SIZE> File::MAGIC {'P', 'A', 'N', 'D', 'A', '\0', '\0', '\0'};

// Name anonymous maps for perfing tools finding symbol file correctly.
// NOLINTNEXTLINE(readability-identifier-naming, modernize-avoid-c-arrays)
const char *ANONMAPNAME_PERFIX = "panda-";

static uint32_t GetProt(panda_file::File::OpenMode mode)
{
    uint32_t prot = os::mem::MMAP_PROT_READ;
    if (mode == File::READ_WRITE) {
        prot |= os::mem::MMAP_PROT_WRITE;
    }
    return prot;
}

class AnonMemSet {
public:
    using MemNameSet = std::map<std::string, std::string>;
    using InsertResult = std::map<std::string, std::string>::iterator;

    static AnonMemSet &GetInstance()
    {
        static AnonMemSet anon_mem_set;
        return anon_mem_set;
    }

    InsertResult Insert(const std::string &file_name, const std::string &anon_mem_name)
    {
        return mem_name_set_.emplace(file_name, anon_mem_name).first;
    }

    void Remove(const std::string &file_name)
    {
        auto it = mem_name_set_.find(file_name);
        if (it != mem_name_set_.end()) {
            mem_name_set_.erase(it);
        }
    }

private:
    MemNameSet mem_name_set_;
};

std::unique_ptr<const File> OpenPandaFileOrZip(std::string_view location, panda_file::File::OpenMode open_mode)
{
    std::string_view archive_filename = ARCHIVE_FILENAME;
    std::size_t archive_split_index = location.find(ARCHIVE_SPLIT);
    if (archive_split_index != std::string::npos) {
        archive_filename = location.substr(archive_split_index + 2);  // 2 - archive split size
        location = location.substr(0, archive_split_index);
    }

    return OpenPandaFile(location, archive_filename, open_mode);
}

// NOLINTNEXTLINE(google-runtime-references)
void OpenPandaFileFromZipErrorHandler(ZipArchiveHandle &handle)
{
    if (handle != nullptr) {
        if (panda::CloseArchiveFile(handle) != ZIPARCHIVE_OK) {
            LOG(ERROR, PANDAFILE) << "CloseArchiveFile failed!";
        }
    }
}

std::unique_ptr<const panda_file::File> OpenPandaFileFromZipFile(ZipArchiveHandle &handle, std::string_view location,
                                                                 EntryFileStat &entry, std::string_view archive_name)
{
    uint32_t uncompressed_length = entry.GetUncompressedSize();
    if (uncompressed_length == 0) {
        CloseCurrentFile(handle);
        OpenPandaFileFromZipErrorHandler(handle);
        LOG(ERROR, PANDAFILE) << "Panda file has zero length!";
        return nullptr;
    }

    size_t size_to_mmap = AlignUp(uncompressed_length, panda::os::mem::GetPageSize());
    void *mem = os::mem::MapRWAnonymousRaw(size_to_mmap, false);
    if (mem == nullptr) {
        CloseCurrentFile(handle);
        OpenPandaFileFromZipErrorHandler(handle);
        LOG(ERROR, PANDAFILE) << "Can't mmap anonymous!";
        return nullptr;
    }
    os::mem::BytePtr ptr(reinterpret_cast<std::byte *>(mem), size_to_mmap, os::mem::MmapDeleter);
    std::stringstream ss;
    ss << ANONMAPNAME_PERFIX << archive_name << " extracted in memory from " << location;
    auto it = AnonMemSet::GetInstance().Insert(std::string(location), ss.str());
    auto ret = os::mem::TagAnonymousMemory(reinterpret_cast<void *>(ptr.Get()), size_to_mmap, it->second.c_str());
    if (ret.has_value()) {
        CloseCurrentFile(handle);
        OpenPandaFileFromZipErrorHandler(handle);
        LOG(ERROR, PANDAFILE) << "Can't tag mmap anonymous!";
        return nullptr;
    }

    auto extract_error = ExtractToMemory(handle, reinterpret_cast<uint8_t *>(ptr.Get()), size_to_mmap);
    if (extract_error != 0) {
        CloseCurrentFile(handle);
        OpenPandaFileFromZipErrorHandler(handle);
        LOG(ERROR, PANDAFILE) << "Can't extract!";
        return nullptr;
    }

    os::mem::ConstBytePtr ConstPtr = ptr.ToConst();
    return panda_file::File::OpenFromMemory(std::move(ConstPtr), location);
}

// NOLINTNEXTLINE(google-runtime-references)
std::unique_ptr<const panda_file::File> HandleArchive(ZipArchiveHandle &handle, FILE *fp, std::string_view location,
                                                      EntryFileStat &entry, std::string_view archive_filename,
                                                      panda_file::File::OpenMode open_mode)
{
    std::unique_ptr<const panda_file::File> file;
    // compressed or not 4 aligned, use anonymous memory
    if (entry.IsCompressed() || (entry.GetOffset() & 0x3U) != 0) {
        file = OpenPandaFileFromZipFile(handle, location, entry, archive_filename);
    } else {
        LOG(INFO, PANDAFILE) << "Pandafile is uncompressed and 4 bytes aligned";
        file = panda_file::File::OpenUncompressedArchive(fileno(fp), location, entry.GetUncompressedSize(),
                                                         entry.GetOffset(), open_mode);
    }
    return file;
}

// CODECHECK-NOLINTNEXTLINE(C_RULE_ID_FUNCTION_SIZE)
std::unique_ptr<const panda_file::File> OpenPandaFile(std::string_view location, std::string_view archive_filename,
                                                      panda_file::File::OpenMode open_mode)
{
    trace::ScopedTrace scoped_trace("Open panda file " + std::string(location));
    uint32_t magic;

#ifdef PANDA_TARGET_WINDOWS
    constexpr char const *mode = "rb";
#else
    constexpr char const *mode = "rbe";
#endif

    FILE *fp = fopen(std::string(location).c_str(), mode);
    if (fp == nullptr) {
        LOG(ERROR, PANDAFILE) << "Can't fopen location: " << location;
        return nullptr;
    }
    fseek(fp, 0, SEEK_SET);
    if (fread(&magic, sizeof(magic), 1, fp) != 1) {
        fclose(fp);
        LOG(ERROR, PANDAFILE) << "Can't read from file!(magic) " << location;
        return nullptr;
    }
    fseek(fp, 0, SEEK_SET);
    std::unique_ptr<const panda_file::File> file;
    if (IsZipMagic(magic)) {
        // Open Zipfile and do the extraction.
        ZipArchiveHandle zipfile = nullptr;
        auto open_error = OpenArchiveFile(zipfile, fp);
        if (open_error != ZIPARCHIVE_OK) {
            LOG(ERROR, PANDAFILE) << "Can't open archive " << location;
            fclose(fp);
            return nullptr;
        }
        bool try_default = archive_filename.empty();
        if (!try_default) {
            if (LocateFile(zipfile, archive_filename.data()) != ZIPARCHIVE_OK) {
                LOG(INFO, PANDAFILE) << "Can't find entry with name '" << archive_filename << "', will try "
                                     << ARCHIVE_FILENAME;
                try_default = true;
            }
        }
        if (try_default) {
            if (LocateFile(zipfile, ARCHIVE_FILENAME) != ZIPARCHIVE_OK) {
                OpenPandaFileFromZipErrorHandler(zipfile);
                LOG(ERROR, PANDAFILE) << "Can't find entry with " << ARCHIVE_FILENAME;
                fclose(fp);
                return nullptr;
            }
        }
        EntryFileStat entry = EntryFileStat();
        if (GetCurrentFileInfo(zipfile, &entry) != ZIPARCHIVE_OK) {
            OpenPandaFileFromZipErrorHandler(zipfile);
            LOG(ERROR, PANDAFILE) << "GetCurrentFileInfo error";
            fclose(fp);
            return nullptr;
        }
        if (OpenCurrentFile(zipfile) != ZIPARCHIVE_OK) {
            CloseCurrentFile(zipfile);
            OpenPandaFileFromZipErrorHandler(zipfile);
            LOG(ERROR, PANDAFILE) << "Can't OpenCurrentFile!";
            fclose(fp);
            return nullptr;
        }
        GetCurrentFileOffset(zipfile, &entry);
        file = HandleArchive(zipfile, fp, location, entry, archive_filename, open_mode);
        CloseCurrentFile(zipfile);
        if (panda::CloseArchiveFile(zipfile) != 0) {
            LOG(ERROR, PANDAFILE) << "CloseArchive failed!";
            fclose(fp);
            return nullptr;
        }
    } else {
        file = panda_file::File::Open(location, open_mode);
    }
    fclose(fp);
    return file;
}

std::unique_ptr<const File> OpenPandaFileFromMemory(const void *buffer, size_t size)
{
    size_t size_to_mmap = AlignUp(size, panda::os::mem::GetPageSize());
    void *mem = os::mem::MapRWAnonymousRaw(size_to_mmap, false);
    if (mem == nullptr) {
        return nullptr;
    }

    if (memcpy_s(mem, size_to_mmap, buffer, size) != EOK) {
        PLOG(ERROR, PANDAFILE) << "Failed to copy buffer into mem'";
    }

    os::mem::ConstBytePtr ptr(reinterpret_cast<std::byte *>(mem), size_to_mmap, os::mem::MmapDeleter);
    if (ptr.Get() == nullptr) {
        PLOG(ERROR, PANDAFILE) << "Failed to open panda file from memory'";
        return nullptr;
    }

    std::hash<void *> hash;
    return panda_file::File::OpenFromMemory(std::move(ptr), std::to_string(hash(mem)));
}

class ClassIdxIterator {
public:
    using value_type = const uint8_t *;
    using difference_type = std::ptrdiff_t;
    using pointer = uint32_t *;
    using reference = uint32_t &;
    using iterator_category = std::random_access_iterator_tag;

    ClassIdxIterator(const File &file, const Span<const uint32_t> &span, size_t idx)
        : file_(file), span_(span), idx_(idx)
    {
    }

    ClassIdxIterator(const ClassIdxIterator &other) = default;
    ClassIdxIterator(ClassIdxIterator &&other) = default;
    ~ClassIdxIterator() = default;

    ClassIdxIterator &operator=(const ClassIdxIterator &other)
    {
        if (&other != this) {
            idx_ = other.idx_;
        }

        return *this;
    }

    ClassIdxIterator &operator=(ClassIdxIterator &&other) noexcept
    {
        idx_ = other.idx_;
        return *this;
    }

    ClassIdxIterator &operator+=(size_t n)
    {
        idx_ += n;
        return *this;
    }

    ClassIdxIterator &operator-=(size_t n)
    {
        idx_ -= n;
        return *this;
    }

    ClassIdxIterator &operator++()
    {
        ++idx_;
        return *this;
    }

    ClassIdxIterator &operator--()
    {
        --idx_;
        return *this;
    }

    difference_type operator-(const ClassIdxIterator &other) const
    {
        return idx_ - other.idx_;
    }

    value_type operator*() const
    {
        uint32_t id = span_[idx_];
        return file_.GetStringData(File::EntityId(id)).data;
    }

    bool operator==(const ClassIdxIterator &other) const
    {
        return span_.cbegin() == other.span_.cbegin() && span_.cend() == other.span_.cend() && idx_ == other.idx_;
    }

    bool operator!=(const ClassIdxIterator &other) const
    {
        return !(*this == other);
    }

    bool IsValid() const
    {
        return idx_ < span_.Size();
    }

    uint32_t GetId() const
    {
        return span_[idx_];
    }

    static ClassIdxIterator Begin(const File &file, const Span<const uint32_t> &span)
    {
        return ClassIdxIterator(file, span, 0);
    }

    static ClassIdxIterator End(const File &file, const Span<const uint32_t> &span)
    {
        return ClassIdxIterator(file, span, span.Size());
    }

private:
    const File &file_;
    const Span<const uint32_t> &span_;
    size_t idx_;
};

static bool ReadAndCheckMagic(os::file::File file)
{
    std::array<uint8_t, File::MAGIC_SIZE> buf {};
    if (!file.ReadAll(&buf[0], buf.size())) {
        return false;
    }

    return buf == File::MAGIC;
}

File::File(std::string filename, os::mem::ConstBytePtr &&base)
    : FILENAME(std::move(filename)),
      FILENAME_HASH(CalcFilenameHash(FILENAME)),
      base_(std::forward<os::mem::ConstBytePtr>(base)),
      panda_cache_(std::make_unique<PandaCache>()),
      UNIQ_ID(GetHash32(reinterpret_cast<const uint8_t *>(GetHeader()), sizeof(Header) / 2U))
{
}

File::~File()
{
    AnonMemSet::GetInstance().Remove(FILENAME);
}

inline std::string VersionToString(const std::array<uint8_t, File::VERSION_SIZE> &array)
{
    std::stringstream ss;

    for (size_t i = 0; i < File::VERSION_SIZE - 1; ++i) {
        ss << static_cast<int>(array[i]);
        ss << ".";
    }
    ss << static_cast<int>(array[File::VERSION_SIZE - 1]);

    return ss.str();
}

/* static */
std::unique_ptr<const File> File::Open(std::string_view filename, OpenMode open_mode)
{
    trace::ScopedTrace scoped_trace("Open panda file " + std::string(filename));
    os::file::File file = os::file::Open(filename, os::file::Mode::READONLY);

    if (!file.IsValid()) {
        PLOG(ERROR, PANDAFILE) << "Failed to open panda file '" << filename << "'";
        return nullptr;
    }

    os::file::FileHolder fh_holder(file);

    auto res = file.GetFileSize();

    if (!res) {
        PLOG(ERROR, PANDAFILE) << "Failed to get size of panda file '" << filename << "'";
        return nullptr;
    }

    size_t size = res.Value();

    if (size < sizeof(File::Header) || !ReadAndCheckMagic(file)) {
        LOG(ERROR, PANDAFILE) << "Invalid panda file '" << filename << "'";
        return nullptr;
    }

    uint32_t checksum = 0;
    if (!file.ReadAll(&checksum, sizeof(uint32_t))) {
        LOG(ERROR, PANDAFILE) << "Failed to read checksum of panda file '" << filename << "'";
        return nullptr;
    }

    std::array<uint8_t, File::VERSION_SIZE> buf {};
    if (!file.ReadAll(&buf[0], buf.size())) {
        return nullptr;
    }
    if (buf < minVersion || buf > version) {
        LOG(ERROR, PANDAFILE) << "Unable to open file '" << filename
                              << "' with bytecode version "  // CODECHECK-NOLINTNEXTLINE(C_RULE_ID_INDENT_CHECK)
                              << VersionToString(buf);
        if (buf < minVersion) {
            LOG(ERROR, PANDAFILE)
                << "Minimum supported version is "  // CODECHECK-NOLINTNEXTLINE(C_RULE_ID_INDENT_CHECK)
                << VersionToString(minVersion);
        } else {
            LOG(ERROR, PANDAFILE)
                << "Maximum supported version is "  // CODECHECK-NOLINTNEXTLINE(C_RULE_ID_INDENT_CHECK)
                << VersionToString(version);
        }
        return nullptr;
    }

    os::mem::ConstBytePtr ptr = os::mem::MapFile(file, GetProt(open_mode), os::mem::MMAP_FLAG_PRIVATE, size).ToConst();
    if (ptr.Get() == nullptr) {
        PLOG(ERROR, PANDAFILE) << "Failed to map panda file '" << filename << "'";
        return nullptr;
    }

    // CODECHECK-NOLINTNEXTLINE(CPP_RULE_ID_SMARTPOINTER_INSTEADOF_ORIGINPOINTER, CPP_RULE_ID_NO_USE_NEW_UNIQUE_PTR)
    return std::unique_ptr<File>(new File(filename.data(), std::move(ptr)));
}

std::unique_ptr<const File> File::OpenUncompressedArchive(int fd, const std::string_view &filename, size_t size,
                                                          uint32_t offset, OpenMode open_mode)
{
    trace::ScopedTrace scoped_trace("Open panda file " + std::string(filename));
    auto file = os::file::File(fd);
    if (!file.IsValid()) {
        PLOG(ERROR, PANDAFILE) << "OpenUncompressedArchive: Failed to open panda file '" << filename << "'";
        return nullptr;
    }

    if (size < sizeof(File::Header)) {
        LOG(ERROR, PANDAFILE) << "Invalid panda file size '" << filename << "'";
        return nullptr;
    }
    LOG(DEBUG, PANDAFILE) << " size=" << size << " offset=" << offset << " " << filename;

    os::mem::ConstBytePtr ptr =
        os::mem::MapFile(file, GetProt(open_mode), os::mem::MMAP_FLAG_PRIVATE, size, offset).ToConst();
    if (ptr.Get() == nullptr) {
        PLOG(ERROR, PANDAFILE) << "Failed to map panda file '" << filename << "'";
        return nullptr;
    }
    if (!CheckHeader(ptr, filename)) {
        return nullptr;
    }

    // CODECHECK-NOLINTNEXTLINE(CPP_RULE_ID_SMARTPOINTER_INSTEADOF_ORIGINPOINTER, CPP_RULE_ID_NO_USE_NEW_UNIQUE_PTR)
    return std::unique_ptr<File>(new File(filename.data(), std::move(ptr)));
}

template <typename T = uint32_t>
bool CheckHeaderElementOffset(size_t offset, size_t number, size_t file_size)
{
    auto number_size = number * sizeof(T);
    if (offset > file_size || number_size > file_size || offset > file_size - number_size) {
        return false;
    }
    return true;
}

bool CheckHeader(const os::mem::ConstBytePtr &ptr, const std::string_view &filename)
{
    auto header = reinterpret_cast<const File::Header *>(ptr.Get());
    if (header->magic != File::MAGIC) {
        LOG(ERROR, PANDAFILE) << "Invalid panda file '" << filename << "'";
        return false;
    }

    if (header->file_size < sizeof(File::Header) || header->file_size > ptr.GetSize()) {
        LOG(ERROR, PANDAFILE) << "Invalid panda file size " << header->file_size;
        return false;
    }

    if (!CheckHeaderElementOffset<uint8_t>(header->foreign_off, header->foreign_size, header->file_size)) {
        LOG(ERROR, PANDAFILE) << "Invalid panda file foreign_off " << header->foreign_off <<
            " or foreign_size " << header->foreign_size;
        return false;
    }

    if (!CheckHeaderElementOffset(header->class_idx_off, header->num_classes, header->file_size)) {
        LOG(ERROR, PANDAFILE) << "Invalid panda file class_idx_off " << header->class_idx_off <<
            " or num_classes " << header->num_classes;
        return false;
    }

    if (!CheckHeaderElementOffset(header->lnp_idx_off, header->num_lnps, header->file_size)) {
        LOG(ERROR, PANDAFILE) << "Invalid panda file lnp_idx_off " << header->lnp_idx_off <<
            " or num_lnps " << header->num_lnps;
        return false;
    }

    if (!CheckHeaderElementOffset(header->literalarray_idx_off, header->num_literalarrays, header->file_size)) {
        LOG(ERROR, PANDAFILE) << "Invalid panda file literalarray_idx_off " << header->literalarray_idx_off <<
            " or num_literalarrays " << header->num_literalarrays;
        return false;
    }

    if (!CheckHeaderElementOffset<File::IndexHeader>(header->index_section_off, header->num_indexes,
        header->file_size)) {
        LOG(ERROR, PANDAFILE) << "Invalid panda file index_section_off " << header->index_section_off <<
            " or num_indexes " << header->num_indexes;
        return false;
    }

    return true;
}

/* static */
std::unique_ptr<const File> File::OpenFromMemory(os::mem::ConstBytePtr &&ptr)
{
    auto header = reinterpret_cast<const Header *>(ptr.Get());
    if (header->magic != File::MAGIC) {
        LOG(ERROR, PANDAFILE) << "Invalid panda file";
        return nullptr;
    }

    if (header->file_size < sizeof(File::Header)) {
        LOG(ERROR, PANDAFILE) << "Invalid panda file";
        return nullptr;
    }

    // CODECHECK-NOLINTNEXTLINE(CPP_RULE_ID_SMARTPOINTER_INSTEADOF_ORIGINPOINTER, CPP_RULE_ID_NO_USE_NEW_UNIQUE_PTR)
    return std::unique_ptr<File>(new File("", std::forward<os::mem::ConstBytePtr>(ptr)));
}

/* static */
std::unique_ptr<const File> File::OpenFromMemory(os::mem::ConstBytePtr &&ptr, std::string_view filename)
{
    trace::ScopedTrace scoped_trace("Open panda file from RAM " + std::string(filename));
    auto header = reinterpret_cast<const Header *>(ptr.Get());

    if (header->magic != File::MAGIC) {
        LOG(ERROR, PANDAFILE) << "Invalid panda file";
        return nullptr;
    }

    if (header->file_size < sizeof(File::Header)) {
        LOG(ERROR, PANDAFILE) << "Invalid panda file '" << filename << "'";
        return nullptr;
    }

    // CODECHECK-NOLINTNEXTLINE(CPP_RULE_ID_SMARTPOINTER_INSTEADOF_ORIGINPOINTER, CPP_RULE_ID_NO_USE_NEW_UNIQUE_PTR)
    return std::unique_ptr<File>(new File(filename.data(), std::forward<os::mem::ConstBytePtr>(ptr)));
}

File::EntityId File::GetClassId(const uint8_t *mutf8_name) const
{
    auto class_idx = GetClasses();

    auto it = std::lower_bound(ClassIdxIterator::Begin(*this, class_idx), ClassIdxIterator::End(*this, class_idx),
                               mutf8_name, utf::Mutf8Less());

    if (!it.IsValid()) {
        return EntityId();
    }

    if (utf::CompareMUtf8ToMUtf8(mutf8_name, *it) == 0) {
        return EntityId(it.GetId());
    }

    return EntityId();
}

uint32_t File::CalcFilenameHash(const std::string &filename)
{
    return GetHash32String(reinterpret_cast<const uint8_t *>(filename.c_str()));
}

File::EntityId File::GetLiteralArraysId() const
{
    const Header *header = GetHeader();
    return EntityId(header->literalarray_idx_off);
}

}  // namespace panda::panda_file
