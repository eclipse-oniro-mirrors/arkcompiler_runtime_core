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
const { etsVm, getTestModule } = require('scenarios.test.js');

const etsMod = getTestModule('scenarios_test');
const DerivedClassWithDefaultParameterMethods = etsMod.getClass('DerivedClassWithDefaultParameterMethods');

{
  const INT_VALUE = 777;
  const DEFAULT_VALUE = 1;

  let obj = new DerivedClassWithDefaultParameterMethods();

  let ret = obj.int_parameter_method();
  ASSERT_EQ(ret, -DEFAULT_VALUE);

  ret = obj.int_parameter_method(INT_VALUE);
  ASSERT_EQ(ret, -INT_VALUE);
}