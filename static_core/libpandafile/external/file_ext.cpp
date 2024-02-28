/**
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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
//

#include "file_ext.h"
#include "panda_file_external.h"
#include "libpandafile/file-inl.h"
#include "libpandafile/class_data_accessor-inl.h"
#include "libpandafile/method_data_accessor-inl.h"
#include "libpandafile/code_data_accessor-inl.h"
#include "libpandafile/debug_helpers.h"
#include "libpandabase/utils/logger.h"
#include "libpandabase/os/native_stack.h"
#include <map>

namespace panda::panda_file::ext {

struct MethodSymEntry {
    panda::panda_file::File::EntityId id;
    uint32_t length {0};
    std::string name;
};

}  // namespace panda::panda_file::ext

#ifdef __cplusplus
extern "C" {
#endif

struct PandaFileExt {
public:
    explicit PandaFileExt(std::unique_ptr<const panda::panda_file::File> &&pandaFile) : pandaFile_(std::move(pandaFile))
    {
    }

    size_t GetExtFileLineNumber(panda::panda_file::MethodDataAccessor mda, uint32_t bcOffset)
    {
        return panda::panda_file::debug_helpers::GetLineNumber(mda, bcOffset, pandaFile_.get());
    }

    panda::panda_file::ext::MethodSymEntry *QueryMethodSymByOffset(uint64_t offset)
    {
        auto it = methodSymbols_.upper_bound(offset);
        if (it != methodSymbols_.end() && offset >= it->second.id.GetOffset()) {
            return &it->second;
        }

        // Enmuate all methods and put them to local cache.
        panda::panda_file::ext::MethodSymEntry *found = nullptr;
        auto callBack = [this, offset, &found](panda::panda_file::MethodDataAccessor &mda) -> void {
            if (!mda.GetCodeId().has_value()) {
                return;
            }
            panda::panda_file::CodeDataAccessor ca {*pandaFile_, mda.GetCodeId().value()};
            panda::panda_file::ext::MethodSymEntry entry;
            entry.id = mda.GetCodeId().value();
            entry.length = ca.GetCodeSize();
            entry.name = std::string(panda::utf::Mutf8AsCString(pandaFile_->GetStringData(mda.GetNameId()).data));

            auto ret = methodSymbols_.emplace(offset, entry);
            if (mda.GetCodeId().value().GetOffset() <= offset &&
                offset < mda.GetCodeId().value().GetOffset() + ca.GetCodeSize()) {
                found = &ret.first->second;
            }
        };

        for (uint32_t id : pandaFile_->GetClasses()) {
            if (pandaFile_->IsExternal(panda::panda_file::File::EntityId(id))) {
                continue;
            }
            panda::panda_file::ClassDataAccessor cda {*pandaFile_, panda::panda_file::File::EntityId(id)};
            cda.EnumerateMethods(callBack);
        }
        return found;
    }

    panda::panda_file::ext::MethodSymEntry *QueryMethodSymAndLineByOffset(uint64_t offset)
    {
        auto it = methodSymbols_.upper_bound(offset);
        if (it != methodSymbols_.end() && offset >= it->second.id.GetOffset()) {
            return &it->second;
        }

        // Enmuate all methods and put them to local cache.
        panda::panda_file::ext::MethodSymEntry *found = nullptr;
        for (uint32_t id : pandaFile_->GetClasses()) {
            if (pandaFile_->IsExternal(panda::panda_file::File::EntityId(id))) {
                continue;
            }
            panda::panda_file::ClassDataAccessor cda {*pandaFile_, panda::panda_file::File::EntityId(id)};
            cda.EnumerateMethods([&](panda::panda_file::MethodDataAccessor &mda) -> void {
                if (mda.GetCodeId().has_value()) {
                    panda::panda_file::CodeDataAccessor ca {*pandaFile_, mda.GetCodeId().value()};
                    panda::panda_file::ext::MethodSymEntry entry;
                    entry.id = mda.GetCodeId().value();
                    entry.length = ca.GetCodeSize();
                    entry.name =
                        std::string(panda::utf::Mutf8AsCString(pandaFile_->GetStringData(mda.GetNameId()).data));

                    auto ret = methodSymbols_.emplace(offset, entry);
                    if (mda.GetCodeId().value().GetOffset() <= offset &&
                        offset < mda.GetCodeId().value().GetOffset() + ca.GetCodeSize()) {
                        size_t lineNumber = GetExtFileLineNumber(mda, offset - mda.GetCodeId().value().GetOffset());
                        found = &ret.first->second;
                        panda::panda_file::File::EntityId idNew(lineNumber);
                        auto nameId = cda.GetDescriptor();
                        found->id = idNew;
                        found->name =
                            panda::os::native_stack::ChangeJaveStackFormat(reinterpret_cast<const char *>(nameId)) +
                            "." + found->name;
                    }
                }
            });
        }
        return found;
    }

    std::vector<struct MethodSymInfoExt> QueryAllMethodSyms()
    {
        // Enmuate all methods and put them to local cache.
        std::vector<panda::panda_file::ext::MethodSymEntry> res;
        auto queryCb = [this, &res](panda::panda_file::MethodDataAccessor &mda) -> void {
            if (!mda.GetCodeId().has_value()) {
                return;
            }
            panda::panda_file::CodeDataAccessor ca {*pandaFile_, mda.GetCodeId().value()};
            std::stringstream ss;
            std::string_view cname(
                    panda::utf::Mutf8AsCString(pandaFile_->GetStringData(mda.GetClassId()).data));
            if (!cname.empty()) {
                ss << cname.substr(0, cname.size() - 1);
            }
            ss << "." << panda::utf::Mutf8AsCString(pandaFile_->GetStringData(mda.GetNameId()).data);
            res.push_back({mda.GetCodeId().value(), ca.GetCodeSize(), ss.str()});
        };
        for (uint32_t id : pandaFile_->GetClasses()) {
            if (pandaFile_->IsExternal(panda::panda_file::File::EntityId(id))) {
                continue;
            }
            panda::panda_file::ClassDataAccessor cda {*pandaFile_, panda::panda_file::File::EntityId(id)};
            cda.EnumerateMethods(queryCb);
        }

        std::vector<struct MethodSymInfoExt> methodInfo;
        methodInfo.reserve(res.size());
        for (auto const &me : res) {
            struct MethodSymInfoExt msi {};
            msi.offset = me.id.GetOffset();
            msi.length = me.length;
            msi.name = me.name;
            methodInfo.push_back(msi);
        }
        return methodInfo;
    }

private:
    std::map<uint64_t, panda::panda_file::ext::MethodSymEntry> methodSymbols_;
    std::unique_ptr<const panda::panda_file::File> pandaFile_;
};

bool OpenPandafileFromMemoryExt(void *addr, const uint64_t *size, [[maybe_unused]] const std::string &fileName,
                                PandaFileExt **pandaFileExt)
{
    if (pandaFileExt == nullptr) {
        return false;
    }

    panda::os::mem::ConstBytePtr ptr(
        reinterpret_cast<std::byte *>(addr), *size,
        []([[maybe_unused]] std::byte *paramBuffer, [[maybe_unused]] size_t paramSize) noexcept {});
    auto pf = panda::panda_file::File::OpenFromMemory(std::move(ptr));
    if (pf == nullptr) {
        return false;
    }

    auto pfExt = std::make_unique<PandaFileExt>(std::move(pf));
    *pandaFileExt = pfExt.release();
    return true;
}

bool OpenPandafileFromFdExt([[maybe_unused]] int fd, [[maybe_unused]] uint64_t offset, const std::string &fileName,
                            PandaFileExt **pandaFileExt)
{
    auto pf = panda::panda_file::OpenPandaFile(fileName);
    if (pf == nullptr) {
        return false;
    }

    auto pfExt = std::make_unique<PandaFileExt>(std::move(pf));
    *pandaFileExt = pfExt.release();
    return true;
}

bool QueryMethodSymByOffsetExt(struct PandaFileExt *pf, uint64_t offset, struct MethodSymInfoExt *methodInfo)
{
    auto entry = pf->QueryMethodSymByOffset(offset);
    if ((entry != nullptr) && (methodInfo != nullptr)) {
        methodInfo->offset = entry->id.GetOffset();
        methodInfo->length = entry->length;
        methodInfo->name = entry->name;
        return true;
    }
    return false;
}

bool QueryMethodSymAndLineByOffsetExt(struct PandaFileExt *pf, uint64_t offset, struct MethodSymInfoExt *methodInfo)
{
    auto entry = pf->QueryMethodSymAndLineByOffset(offset);
    if ((entry != nullptr) && (methodInfo != nullptr)) {
        methodInfo->offset = entry->id.GetOffset();
        methodInfo->length = entry->length;
        methodInfo->name = entry->name;
        return true;
    }
    return false;
}

void QueryAllMethodSymsExt(PandaFileExt *pf, MethodSymInfoExtCallBack callback, void *userData)
{
    auto methodInfos = pf->QueryAllMethodSyms();
    for (auto mi : methodInfos) {
        callback(&mi, userData);
    }
}

#ifdef __cplusplus
}  // extern "C"
#endif
