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

#ifndef BYTECODE_EXPLORER_CORE_COMMON_MEMORY_RESOURCE_H
#define BYTECODE_EXPLORER_CORE_COMMON_MEMORY_RESOURCE_H

#include <cstddef>
#include <new>

#if __has_include(<memory_resource>)
#include <memory_resource>

namespace panda::bytecode_explorer {
namespace pmr = std::pmr;
}  // namespace panda::bytecode_explorer
#else
#include <experimental/memory_resource>
#include <experimental/set>
#include <experimental/string>
#include <experimental/vector>

namespace panda::bytecode_explorer {
namespace pmr = std::experimental::pmr;
}  // namespace panda::bytecode_explorer
#endif

namespace panda::bytecode_explorer {

// A header-only equivalent of pmr::new_delete_resource(). The prebuilt
// platform C++ libraries do not always provide the out-of-line definitions of
// the memory-resource globals (macOS libc++ links fail on the missing
// symbols), so the explorer owns a never-destroyed new/delete resource
// instead. The application never calls pmr::set_default_resource(), so this
// resource also serves every former pmr::get_default_resource() call site.
// Never default-construct a pmr container (a bare `pmr::string s;` or an
// aggregate `{}` member): polymorphic_allocator's implicit default constructor
// calls pmr::get_default_resource() and reintroduces the missing symbol.
// Pass NewDeleteResource() or an explicit resource instead.
class NewDeleteMemoryResource final : public pmr::memory_resource {
public:
    NewDeleteMemoryResource() = default;
    NewDeleteMemoryResource(const NewDeleteMemoryResource &) = delete;
    NewDeleteMemoryResource &operator=(const NewDeleteMemoryResource &) = delete;
    NewDeleteMemoryResource(NewDeleteMemoryResource &&) = delete;
    NewDeleteMemoryResource &operator=(NewDeleteMemoryResource &&) = delete;
    ~NewDeleteMemoryResource() override = default;

private:
    void *do_allocate(size_t bytes, size_t alignment) override
    {
        if (alignment <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
            return ::operator new(bytes);
        }
        return ::operator new(bytes, std::align_val_t(alignment));
    }

    // The platform toolchains do not enable sized deallocation, so only the
    // unsized delete forms are guaranteed to be declared.
    void do_deallocate(void *memory, size_t bytes, size_t alignment) override
    {
        if (alignment <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
            ::operator delete(memory);
            return;
        }
        ::operator delete(memory, std::align_val_t(alignment));
    }

    bool do_is_equal(const pmr::memory_resource &other) const noexcept override
    {
        return this == &other;
    }
};

inline pmr::memory_resource *NewDeleteResource()
{
    // Never destroyed, like the standard library resource: containers may
    // outlive main() and the destruction order between them is unspecified.
    alignas(NewDeleteMemoryResource) static std::byte storage[sizeof(NewDeleteMemoryResource)];
    static NewDeleteMemoryResource *resource = new (storage) NewDeleteMemoryResource();
    return resource;
}

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_COMMON_MEMORY_RESOURCE_H
