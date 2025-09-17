/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
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

#ifndef CPP_ABCKIT_ARKTS_INTERFACE_IMPL_H
#define CPP_ABCKIT_ARKTS_INTERFACE_IMPL_H

#include "field.h"
#include "function.h"
#include "interface.h"
#include "module.h"

// NOLINTBEGIN(performance-unnecessary-value-param)
namespace abckit::arkts {

inline AbckitArktsInterface *Interface::TargetCast() const
{
    const auto ret = GetApiConfig()->cArktsIapi_->coreInterfaceToArktsInterface(GetView());
    CheckError(GetApiConfig());
    return ret;
}

inline Interface::Interface(const core::Interface &coreOther) : core::Interface(coreOther), targetChecker_(this) {}

inline bool Interface::SetName(const std::string &name) const
{
    const auto ret = GetApiConfig()->cArktsMapi_->interfaceSetName(TargetCast(), name.c_str());
    CheckError(GetApiConfig());
    return ret;
}

inline bool Interface::RemoveField(arkts::InterfaceField field) const
{
    const auto ret = GetApiConfig()->cArktsMapi_->interfaceRemoveField(TargetCast(), field.TargetCast());
    CheckError(GetApiConfig());
    return ret;
}

inline Interface Interface::CreateInterface(Module m, const std::string &name)
{
    auto iface = m.GetApiConfig()->cArktsMapi_->createInterface(m.TargetCast(), name.c_str());
    CheckError(m.GetApiConfig());

    auto coreIface = core::Interface(m.GetApiConfig()->cArktsIapi_->arktsInterfaceToCoreInterface(iface),
                                     m.GetApiConfig(), m.GetResource());
    CheckError(m.GetApiConfig());

    return Interface(coreIface);
}

inline bool Interface::AddField(arkts::InterfaceField field)
{
    const auto ret = GetApiConfig()->cArktsMapi_->interfaceAddField(TargetCast(), field.TargetCast());
    CheckError(GetApiConfig());
    return ret;
}

inline bool Interface::AddMethod(arkts::Function function)
{
    const auto ret = GetApiConfig()->cArktsMapi_->interfaceAddMethod(TargetCast(), function.TargetCast());
    CheckError(GetApiConfig());
    return ret;
}

inline bool Interface::AddSuperInterface(const Interface &iface) const
{
    const auto ret = GetApiConfig()->cArktsMapi_->interfaceAddSuperInterface(TargetCast(), iface.TargetCast());
    CheckError(GetApiConfig());
    return ret;
}

inline bool Interface::RemoveSuperInterface(const Interface &iface) const
{
    const auto ret = GetApiConfig()->cArktsMapi_->interfaceRemoveSuperInterface(TargetCast(), iface.TargetCast());
    CheckError(GetApiConfig());
    return ret;
}

inline bool Interface::RemoveMethod(const Function &method) const
{
    const auto ret = GetApiConfig()->cArktsMapi_->interfaceRemoveMethod(TargetCast(), method.TargetCast());
    CheckError(GetApiConfig());
    return ret;
}

inline bool Interface::SetOwningModule(const Module &module) const
{
    const auto ret = GetApiConfig()->cArktsMapi_->interfaceSetOwningModule(TargetCast(), module.TargetCast());
    CheckError(GetApiConfig());
    return ret;
}

}  // namespace abckit::arkts
// NOLINTEND(performance-unnecessary-value-param)

#endif  // CPP_ABCKIT_ARKTS_INTERFACE_IMPL_H
