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

/* DO NOT EDIT, file is autogenerated */
// NOLINTBEGIN(readability-identifier-naming, readability-named-parameter)
#include <ets_napi.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 Class:     ETSGLOBAL
 Method:    nativeInGlobal
 Signature: :I
 */
ETS_EXPORT ets_int ETS_CALL ETS_ETSGLOBAL_nativeInGlobal(EtsEnv *, ets_class);

/*
 Class:     EtsnapiNameTest
 Method:    objectArg
 Signature: Lstd/core/Object;:I
 */
ETS_EXPORT ets_int ETS_CALL ETS_EtsnapiNameTest_objectArg(EtsEnv *, ets_class, ets_object);

/*
 Class:     EtsnapiNameTest
 Method:    中文函数2不带参数
 Signature: :Lstd/core/String;
 */
ETS_EXPORT ets_string ETS_CALL ETS_EtsnapiNameTest__04e2d_06587_051fd_065702_04e0d_05e26_053c2_06570(EtsEnv *,
                                                                                                     ets_object);

/*
 Class:     EtsnapiNameTest
 Method:    methodOverloaded
 Signature: Lstd/core/Object;Lstd/core/String;[D:I
 */
ETS_EXPORT ets_int ETS_CALL ETS_EtsnapiNameTest_methodOverloaded__Lstd_core_Object_2Lstd_core_String_2_3D(
    EtsEnv *, ets_class, ets_object, ets_string, ets_object);

/*
 Class:     EtsnapiNameTest
 Method:    methodOverloaded
 Signature: I:I
 */
ETS_EXPORT ets_int ETS_CALL ETS_EtsnapiNameTest_methodOverloaded__I(EtsEnv *, ets_class, ets_int);

/*
 Class:     EtsnapiNameTest
 Method:    methodOverloaded
 Signature: :I
 */
ETS_EXPORT ets_int ETS_CALL ETS_EtsnapiNameTest_methodOverloaded__(EtsEnv *, ets_class);

/*
 Class:     EtsnapiNameTest
 Method:    methodOverloaded
 Signature: ZBCDFIJS:I
 */
ETS_EXPORT ets_int ETS_CALL ETS_EtsnapiNameTest_methodOverloaded__ZBCDFIJS(EtsEnv *, ets_class, ets_boolean, ets_byte,
                                                                           ets_char, ets_double, ets_float, ets_int,
                                                                           ets_long, ets_short);

#ifdef __cplusplus
}
#endif

// NOLINTEND(readability-identifier-naming, readability-named-parameter)