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

import { __$$ETS_ANNOTATION$$__Router } from "./imported_annotations_rules";
import { __$$ETS_ANNOTATION$$__Router as AliasRouter } from "./imported_annotations_rules";
import * as Decoy from "./imported_annotations_decoy";

@interface __$$ETS_ANNOTATION$$__Local { source: string }

@__$$ETS_ANNOTATION$$__Router({ source: "regular" })
export class Regular {
    @__$$ETS_ANNOTATION$$__Router({ source: "regular" })
    regularMethod() {}
}

@Decoy.__$$ETS_ANNOTATION$$__Router({ source: "namespace" })
export class Namespaced {
    @Decoy.__$$ETS_ANNOTATION$$__Router({ source: "namespace" })
    namespacedMethod() {}
}

@__$$ETS_ANNOTATION$$__AliasRouter({ source: "alias" })
export class Aliased {
    @__$$ETS_ANNOTATION$$__AliasRouter({ source: "alias" })
    aliasedMethod() {}
}

@__$$ETS_ANNOTATION$$__Local({ source: "local" })
export class LocalHandler {
    @__$$ETS_ANNOTATION$$__Local({ source: "local" })
    localMethod() {}
}
