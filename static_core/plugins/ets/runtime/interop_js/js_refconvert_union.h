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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_INTEROP_JS_JS_REFCONVERT_UNION_H
#define PANDA_PLUGINS_ETS_RUNTIME_INTEROP_JS_JS_REFCONVERT_UNION_H

#include "plugins/ets/runtime/interop_js/interop_context.h"
#include "plugins/ets/runtime/interop_js/js_convert.h"

namespace ark::ets::interop::js {

class JSRefConvertUnion : public JSRefConvert {
public:
    explicit JSRefConvertUnion(Class *klass) : JSRefConvert(this), klass_ {EtsClass::FromRuntimeClass(klass)} {}

    napi_value WrapImpl(InteropCtx *ctx, EtsObject *obj);
    EtsObject *UnwrapImpl(InteropCtx *ctx, napi_value jsVal);

private:
    EtsClass *klass_;
};

}  // namespace ark::ets::interop::js

#endif
