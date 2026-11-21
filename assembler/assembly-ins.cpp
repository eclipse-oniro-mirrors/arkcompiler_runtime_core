/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

#include <array>
#include <string_view>

#include "assembly-ins.h"
#include "assembly-string-pool.h"

#include "utils/thread_safe_hashset.h"

namespace panda::pandasm {

const std::string Ins::EMPTY_STRING = "";

// Interned strings are looked up on every Type / LocalVariable construction, so the
// hot path is served by a thread-local direct-mapped cache: the entry is validated
// by both the full hash and the string content, a conflict simply falls through to
// the pool path. The cache is bounded (128 KiB per thread) and the memory saving
// of the pool itself is unaffected. The pool is a sharded ThreadSafeHashset:
// writers on distinct buckets (16384 buckets masked by hash) proceed concurrently,
// replacing the single global std::set + mutex whose O(log n) comparisons and
// lock contention dominated the cold (first-intern) path.
namespace {

constexpr size_t INTERN_CACHE_SIZE = 8192;

struct InternCacheEntry {
    size_t hash {0};
    const std::string *value {nullptr};
};

}  // namespace

const std::string *InternString(std::string_view s)
{
    static panda::ThreadSafeHashset<std::string> pool;

    thread_local std::array<InternCacheEntry, INTERN_CACHE_SIZE> cache {};

    const size_t h = std::hash<std::string_view> {}(s);
    InternCacheEntry &entry = cache[h % INTERN_CACHE_SIZE];
    if (entry.value != nullptr && entry.hash == h && std::string_view(*entry.value) == s) {
        return entry.value;
    }

    const std::string *value = pool.InsertOrGet(std::string(s));
    entry.hash = h;
    entry.value = value;
    return value;
}

// Process-wide string intern pool for pandasm::Ins id strings. Returns a stable
// const std::string* (pool node) so identical names share storage. The pool
// is never freed during the process (reaped at exit by the OS); it is bounded by
// the distinct-name vocabulary (not the Ins count), so its footprint is small for
// build tools. Ins subclasses store the returned pointer (no per-Ins std::string
// copy), and ~Ins does not dereference it, so there is no use-after-free against
// pool destruction order.
const std::string* Ins::Intern(std::string_view s)
{
    return InternString(s);
}

std::string panda::pandasm::Ins::RegsToString(bool &first, bool print_args, size_t first_arg_idx) const
{
    std::stringstream translator;
    for (const auto &reg : this->Regs()) {
        if (!first) {
            translator << ",";
        } else {
            first = false;
        }

        if (print_args && reg >= first_arg_idx) {
            translator << " a" << reg - first_arg_idx;
        } else {
            translator << " v" << reg;
        }
    }
    return translator.str();
}

std::string panda::pandasm::Ins::ImmsToString(bool &first) const
{
    std::stringstream translator;
    for (const auto &imm : this->Imms()) {
        if (!first) {
            translator << ",";
        } else {
            first = false;
        }

        auto *number = std::get_if<double>(&imm);
        if (number != nullptr) {
            translator << " " << std::scientific << *number;
        } else {
            translator << " 0x" << std::hex << std::get<int64_t>(imm);
        }
        translator.clear();
    }
    return translator.str();
}

std::string panda::pandasm::Ins::IdsToString(bool &first) const
{
    std::stringstream translator;
    for (const auto &id : this->Ids()) {
        if (!first) {
            translator << ",";
        } else {
            first = false;
        }

        translator << " " << id;
    }
    return translator.str();
}

std::string panda::pandasm::Ins::OperandsToString(bool print_args, size_t first_arg_idx) const
{
    bool first = true;
    std::stringstream ss {};

    ss << this->RegsToString(first, print_args, first_arg_idx) << this->ImmsToString(first) << this->IdsToString(first);

    return ss.str();
}

std::string panda::pandasm::Ins::RegToString(size_t idx, bool is_first, bool print_args,
                                             size_t first_arg_idx) const
{
    if (idx >= Regs().size()) {
        return std::string("");
    }

    std::stringstream translator;

    if (!is_first) {
        translator << ", ";
    } else {
        translator << " ";
    }

    if (print_args && Regs()[idx] >= first_arg_idx) {
        translator << "a" << Regs()[idx] - first_arg_idx;
    } else {
        translator << "v" << Regs()[idx];
    }

    return translator.str();
}

std::string panda::pandasm::Ins::ImmToString(size_t idx, bool is_first) const
{
    if (idx >= Imms().size()) {
        return std::string("");
    }

    auto imm = Imms()[idx];
    auto *number = std::get_if<double>(&(imm));
    std::stringstream translator;

    if (!is_first) {
        translator << ", ";
    } else {
        translator << " ";
    }

    if (number != nullptr) {
        translator << std::scientific << *number;
    } else {
        translator << "0x" << std::hex << std::get<int64_t>(imm);
    }

    return translator.str();
}

std::string panda::pandasm::Ins::IdToString(size_t idx, bool is_first) const
{
    if (idx >= Ids().size()) {
        return std::string("");
    }

    std::stringstream translator;

    if (!is_first) {
        translator << ", ";
    } else {
        translator << " ";
    }

    translator << Ids()[idx];

    return translator.str();
}
}  // namespace panda::pandasm
