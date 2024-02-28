/**
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_INTEROP_JS_JS_PROXY_JS_PROXY_H
#define PANDA_PLUGINS_ETS_RUNTIME_INTEROP_JS_JS_PROXY_JS_PROXY_H

#include "runtime/include/class.h"
#include "runtime/include/method.h"

namespace panda::ets {
class EtsClass;
class EtsObject;
}  // namespace panda::ets

namespace panda::ets::interop::js::js_proxy {

class JSProxy {
public:
    static std::unique_ptr<JSProxy> Create(EtsClass *etsClass, Span<Method *> proxyMethods);

    EtsClass *GetProxyClass() const
    {
        return proxyKlass_;
    }

    static bool IsProxyClass(Class *klass)
    {
        // For class descriptor "Lname;" proxy descriptor is "L$name$;"
        const uint8_t *descriptor = klass->GetDescriptor();
        Span<const uint8_t> desc(descriptor, utf::Mutf8Size(descriptor));
        return desc[0] == 'L' && desc[1] == '$' && desc[desc.size() - 1] == ';' && desc[desc.size() - 2U] == '$';
    }

    ~JSProxy() = default;

    NO_COPY_SEMANTIC(JSProxy);
    NO_MOVE_SEMANTIC(JSProxy);

private:
    explicit JSProxy(EtsClass *proxyKlass) : proxyKlass_(proxyKlass) {}

    EtsClass *const proxyKlass_ {};
    std::unique_ptr<Method[]> proxyMethods_ {};  // NOLINT(modernize-avoid-c-arrays)
    // NOTE(vpukhov): add flag if original class has final methods or public fields
    // NOTE(vpukhov): must ensure compat-class methods except accessors do not access its private state
};

}  // namespace panda::ets::interop::js::js_proxy

#endif  // !PANDA_PLUGINS_ETS_RUNTIME_INTEROP_JS_JS_PROXY_JS_PROXY_H
