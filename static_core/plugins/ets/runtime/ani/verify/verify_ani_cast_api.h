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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_ANI_VERIFY_ANI_CAST_API_H
#define PANDA_PLUGINS_ETS_RUNTIME_ANI_VERIFY_ANI_CAST_API_H

#include "plugins/ets/runtime/ani/ani.h"

namespace ark::ets::ani::verify {

class VVm;
class VEnv;
class VRef;
class VModule;
class VObject;
class VClass;
class VString;
class VMethod;
class VError;

namespace internal {

// TypeMapping
template <typename T>
struct TypeMapping {
    using Type = T;
};

template <>
struct TypeMapping<VVm *> {
    using Type = ani_vm *;
};

template <>
struct TypeMapping<VEnv *> {
    using Type = ani_env *;
};

template <>
struct TypeMapping<VEnv **> {
    using Type = ani_env **;
};

template <>
struct TypeMapping<VRef *> {
    using Type = ani_ref;
};

template <>
struct TypeMapping<VModule *> {
    using Type = ani_module;
};

template <>
struct TypeMapping<VString *> {
    using Type = ani_string;
};

template <>
struct TypeMapping<VClass *> {
    using Type = ani_class;
};

template <>
struct TypeMapping<VRef **> {
    using Type = ani_ref *;
};

template <>
struct TypeMapping<VModule **> {
    using Type = ani_module *;
};

template <>
struct TypeMapping<VObject **> {
    using Type = ani_object *;
};

template <>
struct TypeMapping<VClass **> {
    using Type = ani_class *;
};

template <>
struct TypeMapping<VString **> {
    using Type = ani_string *;
};

template <>
struct TypeMapping<VMethod *> {
    using Type = ani_method;
};

template <>
struct TypeMapping<VError **> {
    using Type = ani_error *;
};

template <>
struct TypeMapping<VError *> {
    using Type = ani_error;
};

template <typename X>
using ConvertToANIType = typename TypeMapping<X>::Type;

// FnCaster
template <typename R, typename... Args>
struct FnCaster;

template <typename R, typename... Args>
struct FnCaster<R(Args...)> {
    using Type = R (*)(ConvertToANIType<Args>...);
};

template <typename R, typename... Args>
struct FnCaster<R(Args..., ...)> {
    using Type = R (*)(ConvertToANIType<Args>..., ...);
};

}  // namespace internal
}  // namespace ark::ets::ani::verify

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// CC-OFFNXT(G.PRE.02) should be with define
#define VERIFY_ANI_CAST_API(fn) reinterpret_cast<ark::ets::ani::verify::internal::FnCaster<decltype(fn)>::Type>(fn)
// NOLINTEND(cppcoreguidelines-macro-usage)

#endif  // PANDA_PLUGINS_ETS_RUNTIME_ANI_VERIFY_ANI_CAST_API_H
