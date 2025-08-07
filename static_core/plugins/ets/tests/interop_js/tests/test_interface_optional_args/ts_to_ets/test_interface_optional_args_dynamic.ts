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

let etsVm = globalThis.gtest.etsVm;
let checkInterfaceArgs0 = etsVm.getFunction('Ltest_interface_optional_args_static/ETSGLOBAL;', 'checkInterfaceArgs0');
let checkInterfaceArgs1 = etsVm.getFunction('Ltest_interface_optional_args_static/ETSGLOBAL;', 'checkInterfaceArgs1');

export interface User {
	id: number;
	name?: string;
	email?: string;
}

ASSERT_TRUE(checkInterfaceArgs0());
ASSERT_TRUE(checkInterfaceArgs1());
