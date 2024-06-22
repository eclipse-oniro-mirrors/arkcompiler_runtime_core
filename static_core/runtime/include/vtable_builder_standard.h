/**
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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
#ifndef PANDA_RUNTIME_VTABLE_BUILDER_STANDARD_H
#define PANDA_RUNTIME_VTABLE_BUILDER_STANDARD_H

#include "runtime/include/vtable_builder_base.h"

namespace ark {

struct VTableProtoIdentical {
    bool operator()(const Method::ProtoId &base, const Method::ProtoId &derv) const
    {
        return base == derv;
    }
};

template <class OverridePred>
class StandardVTableBuilder final : public VTableBuilderBase<true> {
public:
    explicit StandardVTableBuilder(ClassLinkerErrorHandler *errHandler) : VTableBuilderBase(errHandler) {}

private:
    [[nodiscard]] bool ProcessClassMethod(const MethodInfo *info) override;
    [[nodiscard]] bool ProcessDefaultMethod(ITable itable, size_t itableIdx, MethodInfo *methodInfo) override;

    std::optional<MethodInfo const *> ScanConflictingDefaultMethods(const MethodInfo *info);

    static bool IsOverriddenBy(Method::ProtoId const &base, Method::ProtoId const &derv);

    static bool IsMaxSpecificInterfaceMethod(const Class *iface, const Method &method, size_t startindex,
                                             const ITable &itable);
};

}  // namespace ark

#endif  // PANDA_RUNTIME_VTABLE_BUILDER_STANDARD_H