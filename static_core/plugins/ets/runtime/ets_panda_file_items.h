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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_ETS_PANDA_FILE_ITEMS_H
#define PANDA_PLUGINS_ETS_RUNTIME_ETS_PANDA_FILE_ITEMS_H

#include <string_view>

namespace panda::ets::panda_file_items {

// clang-format off
namespace class_descriptors {

// Base classes
static constexpr std::string_view ERROR                                = "Lescompat/Error;";
static constexpr std::string_view ARRAY_BUFFER                         = "Lescompat/ArrayBuffer;";
static constexpr std::string_view ASYNC                                = "Lets/coroutine/Async;";
static constexpr std::string_view EXCEPTION                            = "Lstd/core/Exception;";
static constexpr std::string_view CLASS                                = "Lstd/core/Class;";
static constexpr std::string_view OBJECT                               = "Lstd/core/Object;";
static constexpr std::string_view PROMISE                              = "Lstd/core/Promise;";
static constexpr std::string_view VOID                                 = "Lstd/core/void;";
static constexpr std::string_view INTERNAL_UNDEFINED                   = "Lstd/core/__internal_undefined;";
static constexpr std::string_view STRING                               = "Lstd/core/String;";
static constexpr std::string_view WEAK_REF                             = "Lstd/core/WeakRef;";
static constexpr std::string_view TYPE                                 = "Lstd/core/Type;";
static constexpr std::string_view FIELD                                = "Lstd/core/Field;";
static constexpr std::string_view METHOD                               = "Lstd/core/Method;";
static constexpr std::string_view PARAMETER                            = "Lstd/core/Parameter;";

// Box classes
static constexpr std::string_view BOX_BOOLEAN                          = "Lstd/core/Boolean;";
static constexpr std::string_view BOX_BYTE                             = "Lstd/core/Byte;";
static constexpr std::string_view BOX_CHAR                             = "Lstd/core/Char;";
static constexpr std::string_view BOX_SHORT                            = "Lstd/core/Short;";
static constexpr std::string_view BOX_INT                              = "Lstd/core/Int;";
static constexpr std::string_view BOX_LONG                             = "Lstd/core/Long;";
static constexpr std::string_view BOX_FLOAT                            = "Lstd/core/Float;";
static constexpr std::string_view BOX_DOUBLE                           = "Lstd/core/Double;";

// Arrays of base classes
static constexpr std::string_view CLASS_ARRAY                          = "[Lstd/core/Class;";
static constexpr std::string_view STRING_ARRAY                         = "[Lstd/core/String;";

// Exception classes
static constexpr std::string_view ARGUMENT_OUT_OF_RANGE_EXCEPTION      = "Lstd/core/ArgumentOutOfRangeException;";
static constexpr std::string_view ARITHMETIC_EXCEPTION                 = "Lstd/core/ArithmeticException;";
static constexpr std::string_view ARRAY_INDEX_OUT_OF_BOUNDS_EXCEPTION  = "Lstd/core/ArrayIndexOutOfBoundsException;";
static constexpr std::string_view ARRAY_STORE_EXCEPTION                = "Lstd/core/ArrayStoreException;";
static constexpr std::string_view CLASS_CAST_EXCEPTION                 = "Lstd/core/ClassCastException;";
static constexpr std::string_view CLASS_NOT_FOUND_EXCEPTION            = "Lstd/core/ClassNotFoundException;";
static constexpr std::string_view FILE_NOT_FOUND_EXCEPTION             = "Lstd/core/FileNotFoundException;";
static constexpr std::string_view ILLEGAL_ACCESS_EXCEPTION             = "Lstd/core/IllegalAccessException;";
static constexpr std::string_view ILLEGAL_ARGUMENT_EXCEPTION           = "Lstd/core/IllegalArgumentException;";
static constexpr std::string_view ILLEGAL_MONITOR_STATE_EXCEPTION      = "Lstd/core/IllegalMonitorStateException;";
static constexpr std::string_view ILLEGAL_STATE_EXCEPTION              = "Lstd/core/IllegalStateException;";
static constexpr std::string_view INDEX_OUT_OF_BOUNDS_EXCEPTION        = "Lstd/core/IndexOutOfBoundsException;";
static constexpr std::string_view IO_EXCEPTION                         = "Lstd/core/IOException;";
static constexpr std::string_view NEGATIVE_ARRAY_SIZE_EXCEPTION        = "Lstd/core/NegativeArraySizeException;";
static constexpr std::string_view NULL_POINTER_EXCEPTION               = "Lstd/core/NullPointerException;";
static constexpr std::string_view RUNTIME_EXCEPTION                    = "Lstd/core/RuntimeException;";
static constexpr std::string_view STRING_INDEX_OUT_OF_BOUNDS_EXCEPTION = "Lstd/core/StringIndexOutOfBoundsException;";
static constexpr std::string_view UNSUPPORTED_OPERATION_EXCEPTION      = "Lstd/core/UnsupportedOperationException;";

// Error classes
static constexpr std::string_view ABSTRACT_METHOD_ERROR                = "Lescompat/AbstractMethodError;";
static constexpr std::string_view CLASS_CIRCULARITY_ERROR              = "Lescompat/ClassCircularityError;";
static constexpr std::string_view EXCEPTION_IN_INITIALIZER_ERROR       = "Lstd/core/ExceptionInInitializerError;";
static constexpr std::string_view INCOMPATIBLE_CLASS_CHANGE_ERROR      = "Lescompat/IncompatibleClassChangeError;";
static constexpr std::string_view INSTANTIATION_ERROR                  = "Lescompat/InstantiationError;";
static constexpr std::string_view NO_CLASS_DEF_FOUND_ERROR             = "Lescompat/NoClassDefFoundError;";
static constexpr std::string_view NO_SUCH_FIELD_ERROR                  = "Lescompat/NoSuchFieldError;";
static constexpr std::string_view NO_SUCH_METHOD_ERROR                 = "Lescompat/NoSuchMethodError;";
static constexpr std::string_view OUT_OF_MEMORY_ERROR                  = "Lescompat/OutOfMemoryError;";
static constexpr std::string_view STACK_OVERFLOW_ERROR                 = "Lescompat/StackOverflowError;";
static constexpr std::string_view RANGE_ERROR                          = "Lescompat/RangeError;";
static constexpr std::string_view VERIFY_ERROR                         = "Lescompat/VerifyError;";

// interop/js
static constexpr std::string_view JS_RUNTIME                           = "Lstd/interop/js/JSRuntime;";
static constexpr std::string_view JS_VALUE                             = "Lstd/interop/js/JSValue;";
static constexpr std::string_view JS_ERROR                             = "Lstd/interop/js/JSError;";

static constexpr std::string_view ARRAY                                = "Lescompat/Array;";

// escompat
static constexpr std::string_view SHARED_MEMORY                        = "Lescompat/SharedMemory;";

}  // namespace class_descriptors

static constexpr std::string_view CCTOR = "<cctor>";
static constexpr std::string_view CTOR  = "<ctor>";
// clang-format on

}  // namespace panda::ets::panda_file_items

#endif  // PANDA_PLUGINS_ETS_RUNTIME_ETS_PANDA_FILE_ITEMS_H
