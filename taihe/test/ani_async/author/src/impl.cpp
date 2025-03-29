/*
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
#include <async_test.impl.hpp>
#include <cstdint>
#include <iostream>

#include "core/runtime.hpp"

int32_t add_impl(int32_t a, int32_t b)
{
    if (a == 0) {
        taihe::core::set_error("some error happen in add impl");
        return b;
    } else {
        std::cout << "add impl " << a + b << std::endl;
        return a + b;
    }
}

::async_test::IBase getIBase_impl()
{
    struct AuthorIBase {
        taihe::core::string name;
        AuthorIBase() : name("My IBase") {}
        ~AuthorIBase() {}
        taihe::core::string get()
        {
            return name;
        }
        void set(taihe::core::string_view a)
        {
            this->name = a;
            return;
        }
    };
    return taihe::core::make_holder<AuthorIBase, ::async_test::IBase>();
}

void fromStructSync_impl(::async_test::Data const &data)
{
    std::cout << data.a.c_str() << " " << data.b.c_str() << " " << data.c << std::endl;
    return;
}

::async_test::Data toStructSync_impl(taihe::core::string_view a, taihe::core::string_view b, int32_t c)
{
    if (c == 0) {
        taihe::core::set_error("some error happen in toStructSync_impl");
        return {a, b, c};
    }
    return {a, b, c};
}

void PrintSync()
{
    std::cout << "print Sync" << std::endl;
}

TH_EXPORT_CPP_API_addSync(add_impl);
TH_EXPORT_CPP_API_getIBase(getIBase_impl);
TH_EXPORT_CPP_API_fromStructSync(fromStructSync_impl);
TH_EXPORT_CPP_API_toStructSync(toStructSync_impl);
TH_EXPORT_CPP_API_PrintSync(PrintSync);
