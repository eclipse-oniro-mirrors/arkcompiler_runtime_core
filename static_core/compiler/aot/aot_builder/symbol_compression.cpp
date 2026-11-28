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

// .gnu_debugdata mini-ELF construction and the xz compression backend
// (7-Zip SDK). The file is added to the build only when the backend is
// configured, and aot_builder.cpp falls back to full symbols otherwise.

#include "aot_builder.h"
#include "aot/aot_file.h"
#include "elf_builder.h"
#include "include/class.h"
#include "include/method.h"
#include "optimizer/code_generator/encode.h"
#include "code_info/code_info.h"
#include "securec.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <numeric>

#ifdef PANDA_AOT_HAS_XZSDK
#include "7zCrc.h"
#include "7zTypes.h"
#include "XzEnc.h"
#endif

namespace ark::compiler {

#ifdef PANDA_AOT_HAS_XZSDK
namespace {
// The 7-Zip SDK requires the application to initialize the CRC32 table before
// the first encoding call (same convention as faultloggerd's
// MiniDebugInfoFactory). Without it the stream index and footer CRCs are
// never computed and the output is rejected by every decoder.
void InitXzSdk()
{
    static bool initialized = []() {
        CrcGenerateTable();
        return true;
    }();
    (void)initialized;
}

// Buffer-backed ISeqInStream for the one-shot Xz_Encode() call: the SDK is
// stream-based, while the caller holds the mini ELF in memory.
class XzBufferInStream {
public:
    XzBufferInStream(const uint8_t *data, size_t size) : data_(data), size_(size)
    {
        stream_.Read = ReadThunk;
    }

    ISeqInStream *Get()
    {
        return &stream_;
    }

private:
    static SRes ReadThunk(const ISeqInStream *p, void *buf, size_t *size)
    {
        auto *self =
            reinterpret_cast<XzBufferInStream *>(reinterpret_cast<uintptr_t>(p) - offsetof(XzBufferInStream, stream_));
        ASSERT(self->pos_ <= self->size_);
        size_t want = *size;
        size_t avail = self->size_ - self->pos_;
        want = want < avail ? want : avail;
        if (want == 0) {
            *size = 0;
            return SZ_OK;
        }
        errno_t res = memcpy_s(buf, *size, self->data_ + self->pos_, want);
        if (res != 0) {
            UNREACHABLE();
        }
        self->pos_ += want;
        *size = want;
        return SZ_OK;
    }

    ISeqInStream stream_ {};
    const uint8_t *data_;
    size_t size_;
    size_t pos_ {0};
};

// Buffer-backed ISeqOutStream with a growable output vector.
class XzBufferOutStream {
public:
    explicit XzBufferOutStream(size_t reserve)
    {
        out_.reserve(reserve);
        stream_.Write = WriteThunk;
    }

    ISeqOutStream *Get()
    {
        return &stream_;
    }

    std::vector<uint8_t> Take()
    {
        out_.resize(pos_);
        return std::move(out_);
    }

private:
    static size_t WriteThunk(const ISeqOutStream *p, const void *buf, size_t size)
    {
        auto *self = reinterpret_cast<XzBufferOutStream *>(reinterpret_cast<uintptr_t>(p) -
                                                           offsetof(XzBufferOutStream, stream_));
        ASSERT(self->pos_ <= self->out_.size());
        if (self->pos_ + size > self->out_.size()) {
            self->out_.resize(std::max(self->pos_ + size, self->out_.size() * 2U));
        }
        errno_t res = memcpy_s(self->out_.data() + self->pos_, self->out_.size() - self->pos_, buf, size);
        if (res != 0) {
            UNREACHABLE();
        }
        self->pos_ += size;
        return size;
    }

    ISeqOutStream stream_ {};
    std::vector<uint8_t> out_;
    size_t pos_ {0};
};
}  // namespace
#endif  // PANDA_AOT_HAS_XZSDK

#ifdef PANDA_AOT_HAS_XZSDK
namespace {
// Size of ELF structures used by the mini debug ELF (always 64-bit layout,
// regardless of the host: the mini ELF mirrors the target .an layout).
constexpr size_t ELF64_EHDR_SIZE = 64U;
constexpr size_t ELF64_PHDR_SIZE = 56U;
constexpr size_t ELF64_SHDR_SIZE = 64U;
constexpr size_t ELF64_SYM_SIZE = 24U;
constexpr uint16_t ELF_TYPE_EXEC = 2U;

uint16_t MachineOfArch(Arch arch)
{
    switch (arch) {
        case Arch::AARCH64:
            return 183U;  // EM_AARCH64
        case Arch::AARCH32:
            return 40U;  // EM_ARM
        case Arch::X86_64:
            return 62U;  // EM_X86_64
        case Arch::X86:
            return 3U;  // EM_386
        default:
            UNREACHABLE();
    }
}

struct MiniDebugEntry {
    std::string name;
    uint64_t value;
    uint64_t size;
};

using MiniSym = std::array<uint8_t, ELF64_SYM_SIZE>;

constexpr uint16_t MINI_ELF_SHN_CODE_RANGE = 4U;

template <typename Container>
void PutLe(Container &buf, uint64_t value, size_t off, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        buf[off + i] = static_cast<uint8_t>(value >> (i * 8U));
    }
}

template <Arch ARCH>
std::vector<MiniDebugEntry> CollectMiniDebugEntries(std::vector<CompiledMethod> &methods,
                                                    const std::vector<MethodHeader> &methodHeaders,
                                                    RuntimeInterface *runtime, ElfBuilder<ARCH> &builder)
{
    auto textSection = builder.GetTextSection();
    std::vector<MiniDebugEntry> entries;
    entries.reserve(methods.size());
    ASSERT(methods.size() == methodHeaders.size());
    for (size_t i = 0; i < methods.size(); i++) {
        auto method = methods.at(i).GetMethod();
        if (method->GetPandaFile() == nullptr) {
            continue;
        }
        auto methodCasted = reinterpret_cast<RuntimeInterface::MethodPtr>(method);
        // thunk resolves to textSection address + offset at Build() time
        size_t offset = methodHeaders[i].codeOffset;
        entries.push_back({runtime->GetMethodFullName(methodCasted, true),
                           textSection->GetAddress() + offset + CodeInfo::GetCodeOffset(ARCH),
                           methodHeaders[i].codeSize});
    }
    // Sort by address: symbol lookup tools binary-search the table.
    std::sort(entries.begin(), entries.end(),
              [](const MiniDebugEntry &a, const MiniDebugEntry &b) { return a.value < b.value; });
    return entries;
}

uint32_t AddShstr(std::vector<uint8_t> &shstrtab, const char *s)
{
    uint32_t off = static_cast<uint32_t>(shstrtab.size());
    while (*s != '\0') {
        shstrtab.push_back(static_cast<uint8_t>(*s++));
    }
    shstrtab.push_back(0x00);
    return off;
}

void EmitMiniDebugSymbol(std::vector<uint8_t> &strtab, std::vector<MiniSym> &symtab, const std::string &name,
                         uint64_t value, uint64_t size, bool isFunc)
{
    uint32_t nameOff = static_cast<uint32_t>(strtab.size());
    strtab.insert(strtab.end(), name.begin(), name.end());
    strtab.push_back(0x00);
    MiniSym sym {};
    PutLe(sym, nameOff, 0U, 4U);                  // st_name (string table offset)
    sym[4U] = isFunc ? 0x12U : 0x10U;             // (STB_GLOBAL << 4) | (STT_FUNC or STT_NOTYPE)
    PutLe(sym, MINI_ELF_SHN_CODE_RANGE, 6U, 2U);  // st_shndx = code-range section
    PutLe(sym, value, 8U, 8U);                    // st_value
    PutLe(sym, size, 16U, 8U);                    // st_size
    symtab.push_back(sym);
}

void BuildMiniDebugSymtab(const std::vector<MiniDebugEntry> &entries, std::vector<uint8_t> &strtab,
                          std::vector<MiniSym> &symtab)
{
    for (const auto &entry : entries) {
        EmitMiniDebugSymbol(strtab, symtab, entry.name, entry.value, entry.size, true);
    }
}

struct MiniDebugNames {
    uint32_t symtab;
    uint32_t strtab;
    uint32_t shstrtab;
};

MiniDebugNames BuildMiniDebugShstrtab(std::vector<uint8_t> &shstrtab)
{
    shstrtab.assign(1, 0x00);
    return {AddShstr(shstrtab, ".symtab"), AddShstr(shstrtab, ".strtab"), AddShstr(shstrtab, ".shstrtab")};
}

void EmitMiniDebugHeader(std::vector<uint8_t> &mini, uint64_t offPhdr, uint64_t offShdr, uint64_t codeRangeEnd,
                         uint16_t machine)
{
    const uint64_t fileSize = offShdr + 5U * ELF64_SHDR_SIZE;
    mini.reserve(static_cast<size_t>(fileSize));
    mini.insert(mini.end(), {0x7f, 'E', 'L', 'F', 2U, 1U, 1U, 0U});
    mini.resize(ELF64_EHDR_SIZE, 0U);
    PutLe(mini, ELF_TYPE_EXEC, 16U, 2U);    // e_type
    PutLe(mini, machine, 18U, 2U);          // e_machine (target arch!)
    PutLe(mini, 1U, 20U, 4U);               // e_version
    PutLe(mini, offPhdr, 32U, 8U);          // e_phoff
    PutLe(mini, offShdr, 40U, 8U);          // e_shoff
    PutLe(mini, ELF64_EHDR_SIZE, 52U, 2U);  // e_ehsize
    PutLe(mini, ELF64_PHDR_SIZE, 54U, 2U);  // e_phentsize
    PutLe(mini, 1U, 56U, 2U);               // e_phnum
    PutLe(mini, ELF64_SHDR_SIZE, 58U, 2U);  // e_shentsize
    PutLe(mini, 5U, 60U, 2U);               // e_shnum
    PutLe(mini, 3U, 62U, 2U);               // e_shstrndx
    // One PT_LOAD: the mini ELF's own bytes map at vaddr 0 (p_filesz), and
    // p_memsz extends BSS-style over the whole outer .an code address range
    // so symbolization tools can map runtime addresses into this file.
    constexpr uint32_t MINI_ELF_PT_LOAD = 1U;
    constexpr uint32_t MINI_ELF_PF_X = 1U;
    constexpr uint32_t MINI_ELF_PF_R = 4U;
    mini.resize(static_cast<size_t>(offPhdr + ELF64_PHDR_SIZE), 0U);
    PutLe(mini, MINI_ELF_PT_LOAD, offPhdr + 0U, 4U);                   // p_type
    PutLe(mini, MINI_ELF_PF_R | MINI_ELF_PF_X, offPhdr + 4U, 4U);      // p_flags
    PutLe(mini, 0U, offPhdr + 8U, 8U);                                 // p_offset
    PutLe(mini, 0U, offPhdr + 16U, 8U);                                // p_vaddr
    PutLe(mini, 0U, offPhdr + 24U, 8U);                                // p_paddr
    PutLe(mini, fileSize, offPhdr + 32U, 8U);                          // p_filesz
    PutLe(mini, std::max(codeRangeEnd, fileSize), offPhdr + 40U, 8U);  // p_memsz (>= p_filesz)
    PutLe(mini, 0x1000U, offPhdr + 48U, 8U);                           // p_align
}

void EmitMiniDebugSectionHeaders(std::vector<uint8_t> &mini, const std::vector<uint8_t> &strtab,
                                 const std::vector<MiniSym> &symtab, const std::vector<uint8_t> &shstrtab,
                                 uint64_t offSymtab, uint64_t offStrtab, uint64_t offShstrtab, uint64_t offShdr,
                                 uint64_t codeRangeEnd, const MiniDebugNames &names)
{
    mini.resize(static_cast<size_t>(offShdr), 0U);
    auto putShdr = [&mini](uint32_t name, uint32_t type, uint64_t flags, uint64_t off, uint64_t size, uint32_t link,
                           uint64_t entsize, uint32_t info) {
        size_t base = mini.size();
        mini.resize(base + ELF64_SHDR_SIZE, 0U);
        PutLe(mini, name, base + 0U, 4U);
        PutLe(mini, type, base + 4U, 4U);
        PutLe(mini, flags, base + 8U, 8U);  // sh_flags
        PutLe(mini, 0U, base + 16U, 8U);    // sh_addr
        PutLe(mini, off, base + 24U, 8U);
        PutLe(mini, size, base + 32U, 8U);
        PutLe(mini, link, base + 40U, 4U);
        PutLe(mini, info, base + 44U, 4U);  // sh_info (see ELF spec for per-type semantics)
        PutLe(mini, 1U, base + 48U, 8U);    // sh_addralign
        PutLe(mini, entsize, base + 56U, 8U);
    };
    putShdr(0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U);  // SHN_UNDEF
    putShdr(names.symtab, 2U, 0U, offSymtab, symtab.size() * ELF64_SYM_SIZE, 2U, ELF64_SYM_SIZE,
            1U);                                                          // .symtab: sh_info = first non-local symbol
    putShdr(names.strtab, 3U, 0U, offStrtab, strtab.size(), 0U, 0U, 0U);  // .strtab
    putShdr(names.shstrtab, 3U, 0U, offShstrtab, shstrtab.size(), 0U, 0U, 0U);  // .shstrtab
    constexpr uint32_t MINI_ELF_SHT_NOBITS = 8U;
    constexpr uint64_t MINI_ELF_SHF_ALLOC = 0x2U;
    constexpr uint64_t MINI_ELF_SHF_EXECINSTR = 0x4U;
    putShdr(0U, MINI_ELF_SHT_NOBITS, MINI_ELF_SHF_ALLOC | MINI_ELF_SHF_EXECINSTR, 0U, codeRangeEnd, 0U, 0U, 0U);
}
}  // namespace

// MakeMiniDebugData assembles the mini debug ELF in fixed sequential phases
// (collect method entries -> build symtab/strtab -> write ELF headers and
// section table -> compress). The phases run one after another and share
// locals, so keeping them in one function is clearer than splitting on
// artificial boundaries.
// CC-OFFNXT(G.FUD.05) sequential mini-ELF phases share locals, see comment above
template <Arch ARCH>
std::vector<uint8_t> AotBuilder::MakeMiniDebugData(ElfBuilder<ARCH> &builder)
{
    // The mini debug ELF uses the fixed ELF64 layout; 32-bit targets are not
    // supported by this optimization yet (gracefully disabled below).
    if constexpr (!ArchTraits<ARCH>::IS_64_BITS) {
        return {};
    }

    auto entries = CollectMiniDebugEntries<ARCH>(methods_, methodHeaders_, runtime_, builder);

    // Layout: ehdr | phdr(1) | symtab | strtab | shstrtab | shdrs(5)
    std::vector<uint8_t> strtab {0x00};
    std::vector<MiniSym> symtab;
    symtab.emplace_back();  // null symbol
    BuildMiniDebugSymtab(entries, strtab, symtab);

    std::vector<uint8_t> shstrtab;
    auto names = BuildMiniDebugShstrtab(shstrtab);

    const uint64_t offPhdr = ELF64_EHDR_SIZE;
    const uint64_t offSymtab = offPhdr + ELF64_PHDR_SIZE;
    const uint64_t offStrtab = offSymtab + symtab.size() * ELF64_SYM_SIZE;
    const uint64_t offShstrtab = offStrtab + strtab.size();
    const uint64_t offShdr = (offShstrtab + shstrtab.size() + 7U) & ~7ULL;
    // The code-range section and p_memsz must cover every symbol address;
    // entries are sorted by value, so the last entry ends highest.
    const uint64_t codeRangeEnd = entries.empty() ? 0U : entries.back().value + entries.back().size;

    std::vector<uint8_t> mini;
    EmitMiniDebugHeader(mini, offPhdr, offShdr, codeRangeEnd, MachineOfArch(ARCH));
    mini.insert(mini.end(), reinterpret_cast<const uint8_t *>(symtab.data()),
                reinterpret_cast<const uint8_t *>(symtab.data()) + symtab.size() * ELF64_SYM_SIZE);
    mini.insert(mini.end(), strtab.begin(), strtab.end());
    mini.insert(mini.end(), shstrtab.begin(), shstrtab.end());

    EmitMiniDebugSectionHeaders(mini, strtab, symtab, shstrtab, offSymtab, offStrtab, offShstrtab, offShdr,
                                codeRangeEnd, names);

    auto compressed = CompressMiniDebugData(mini, symtab.size());
    if (compressed.empty()) {
        // Runtime compression failure: store the mini ELF uncompressed so
        // offline tools (readelf, llvm-symbolizer with manual extraction)
        // can still access the symbols. Note: faultloggerd on device
        // expects an xz stream and does not recognize a plain ELF here,
        // so crash-log symbolization is unavailable in this fallback path.
        LOG(WARNING, COMPILER) << "Falling back to uncompressed mini debug data (" << mini.size() << " bytes)";
        return mini;
    }
    return compressed;
}

#ifdef PANDA_AOT_HAS_XZSDK
std::vector<uint8_t> AotBuilder::CompressMiniDebugData(const std::vector<uint8_t> &mini, size_t symbols)
{
    // Compress the mini ELF with xz (7-Zip SDK, third_party_lzma) — the
    // `.gnu_debugdata` section format. The
    // stream is consumed by the two-step offline-symbolization wrapper
    // (decompress -> llvm-symbolizer) and by faultloggerd on device. There is
    // no zlib fallback: builds without the SDK compile this path out entirely
    // and generate full symbols instead (see GenerateSymbols). On a runtime
    // compression failure this returns an empty vector, and the caller
    // (MakeMiniDebugData) then stores the mini ELF uncompressed — symbols are
    // never dropped.
    InitXzSdk();
    XzBufferInStream in(mini.data(), mini.size());
    // Initial output reserve: half the input plus headroom for the xz container.
    constexpr uint32_t XZ_OUTPUT_RESERVE = 4096U;
    XzBufferOutStream out(mini.size() / 2U + XZ_OUTPUT_RESERVE);
    CXzProps props;
    XzProps_Init(&props);
    // Single-threaded encoding: the product must not depend on thread
    // scheduling (reproducible builds).
    props.numTotalThreads = 1;
    props.numBlockThreads_Reduced = 1;
    props.numBlockThreads_Max = 1;
    props.lzma2Props.numTotalThreads = 1;
    props.lzma2Props.numBlockThreads_Reduced = 1;
    props.lzma2Props.numBlockThreads_Max = 1;
    // 8 MiB dictionary — matches the previous liblzma preset-6 configuration.
    constexpr uint32_t XZ_DICT_SIZE = 8U << 20;
    props.lzma2Props.lzmaProps.dictSize = XZ_DICT_SIZE;
    SRes res = Xz_Encode(out.Get(), in.Get(), &props, nullptr);
    if (res != SZ_OK) {
        LOG(ERROR, COMPILER) << "Cannot compress mini debug data: xz error " << res;
        return {};
    }
    std::vector<uint8_t> compressed = out.Take();
    LOG(INFO, COMPILER) << "Mini debug data: " << symbols << " symbols, " << mini.size() << " -> " << compressed.size()
                        << " bytes";
    return compressed;
}
#endif  // PANDA_AOT_HAS_XZSDK
#endif

#ifdef PANDA_AOT_HAS_XZSDK
// Explicit instantiations: GenerateSymbols<ARCH> (in aot_builder.cpp) calls
// MakeMiniDebugData<ARCH> from a different translation unit, where only the
// declaration in aot_builder.h is visible.
template std::vector<uint8_t> AotBuilder::MakeMiniDebugData<Arch::AARCH32>(ElfBuilder<Arch::AARCH32> &builder);
template std::vector<uint8_t> AotBuilder::MakeMiniDebugData<Arch::AARCH64>(ElfBuilder<Arch::AARCH64> &builder);
template std::vector<uint8_t> AotBuilder::MakeMiniDebugData<Arch::X86>(ElfBuilder<Arch::X86> &builder);
template std::vector<uint8_t> AotBuilder::MakeMiniDebugData<Arch::X86_64>(ElfBuilder<Arch::X86_64> &builder);
#endif

}  // namespace ark::compiler
