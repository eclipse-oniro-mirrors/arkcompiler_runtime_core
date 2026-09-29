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

#include "plugins/ets/runtime/tooling/helpers.h"

namespace ark::ets::tooling {

ark::tooling::VRegValue UnboxToVRegValueBySig(EtsObject *obj, char sig)
{
    switch (sig) {
        case 'Z':
            return UnboxPrimitiveToVReg<EtsBoolean>(obj);
        case 'B':
            return UnboxPrimitiveToVReg<EtsByte>(obj);
        case 'S':
            return UnboxPrimitiveToVReg<EtsShort>(obj);
        case 'C':
            return UnboxPrimitiveToVReg<EtsChar>(obj);
        case 'I':
            return UnboxPrimitiveToVReg<EtsInt>(obj);
        case 'F':
            return UnboxPrimitiveToVReg<EtsFloat>(obj);
        case 'D':
            return UnboxPrimitiveToVReg<EtsDouble>(obj);
        case 'J':
            return UnboxPrimitiveToVReg<EtsLong>(obj);
        default:
            return ark::tooling::VRegValue(reinterpret_cast<int64_t>(obj->GetCoreType()));
    }
}

EtsObject *BoxVRegValueBySig(ark::tooling::VRegValue vregValue, char sig, EtsExecutionContext *executionCtx)
{
    switch (sig) {
        case 'Z':
            return BoxPrimitiveFromVReg<EtsBoolean>(vregValue, executionCtx);
        case 'B':
            return BoxPrimitiveFromVReg<EtsByte>(vregValue, executionCtx);
        case 'S':
            return BoxPrimitiveFromVReg<EtsShort>(vregValue, executionCtx);
        case 'C':
            return BoxPrimitiveFromVReg<EtsChar>(vregValue, executionCtx);
        case 'I':
            return BoxPrimitiveFromVReg<EtsInt>(vregValue, executionCtx);
        case 'F':
            return BoxPrimitiveFromVReg<EtsFloat>(vregValue, executionCtx);
        case 'D':
            return BoxPrimitiveFromVReg<EtsDouble>(vregValue, executionCtx);
        case 'J':
            return BoxPrimitiveFromVReg<EtsLong>(vregValue, executionCtx);
        default:
            return nullptr;
    }
}

EtsObject *TaggedVRegValueToEtsObject(ark::tooling::VRegValue vregValue, EtsExecutionContext *executionCtx)
{
    coretypes::TaggedValue tagged(static_cast<coretypes::TaggedType>(vregValue.GetValue()));
    if (tagged.IsUndefined() || tagged.IsNull() || tagged.IsHole() || tagged.IsException()) {
        return nullptr;
    }
    if (tagged.IsHeapObject()) {
        auto *obj = EtsObject::FromCoreType(tagged.GetHeapObject());
        return IsNullOrNullValue(obj) ? nullptr : obj;
    }
    if (tagged.IsBoolean()) {
        return EtsBoxPrimitive<EtsBoolean>::Create(executionCtx, static_cast<EtsBoolean>(tagged.IsTrue()))->AsObject();
    }
    if (tagged.IsInt()) {
        return EtsBoxPrimitive<EtsInt>::Create(executionCtx, static_cast<EtsInt>(tagged.GetInt()))->AsObject();
    }
    if (tagged.IsDouble()) {
        return EtsBoxPrimitive<EtsDouble>::Create(executionCtx, tagged.GetDouble())->AsObject();
    }
    // Remaining special tagged values (e.g. weak refs) cannot be represented
    // as EtsObject — treat as null.
    return nullptr;
}

}  // namespace ark::ets::tooling