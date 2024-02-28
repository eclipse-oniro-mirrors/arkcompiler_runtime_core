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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_ETS_ITABLE_BUILDER_H
#define PANDA_PLUGINS_ETS_RUNTIME_ETS_ITABLE_BUILDER_H

#include "libpandabase/utils/span.h"
#include "runtime/include/class.h"
#include "runtime/include/itable_builder.h"
#include "runtime/include/itable.h"

namespace panda {

class ClassLinker;

}  // namespace panda

namespace panda::ets {

class EtsITableBuilder : public ITableBuilder {
public:
    void Build(ClassLinker *classLinker, Class *base, Span<Class *> classInterfaces, bool isInterface) override;

    void Resolve(Class *klass) override;

    void UpdateClass(Class *klass) override;

    void DumpITable([[maybe_unused]] Class *klass) override;

    ITable GetITable() const override
    {
        return itable_;
    };

private:
    ITable itable_;
};

}  // namespace panda::ets

#endif  // !PANDA_PLUGINS_ETS_RUNTIME_ETS_ITABLE_BUILDER_H
