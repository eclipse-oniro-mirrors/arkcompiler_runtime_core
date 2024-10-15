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
const { etsVm, getTestModule } = require('test_class_methods.js');

const etsMod = getTestModule('class_methods_test');
const functionReturnMapEts = etsMod.getFunction('functionReturnMapSubsetByRef');

{
	const initialMap = functionReturnMapEts();
	const changedMap = functionReturnMapEts();

	const initialInitialMapSize = initialMap.size;
	const initialChangedMapSize = changedMap.size;
	changedMap.set(3, 'Test');
	const finalInitialMapSize = initialMap.size;
	const finalChangedMapSize = changedMap.size;

	ASSERT_EQ(initialInitialMapSize, 2);
	ASSERT_EQ(initialChangedMapSize, 2);
	ASSERT_EQ(initialMap.has(3), true);
	ASSERT_EQ(finalInitialMapSize, 3);
	ASSERT_EQ(finalChangedMapSize, 3);
}