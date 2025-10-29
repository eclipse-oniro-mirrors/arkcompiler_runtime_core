/*
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

#include "ets_exceptions.h"
#include "ets_handle.h"
#include "include/thread_scopes.h"
#include "types/ets_escompat_array.h"
#include "types/ets_map.h"
#include "types/ets_object.h"
#include "json_helper.h"
#include "ets_to_string_cache.h"
#include "plugins/ets/runtime/types/ets_method.h"
namespace ark::ets::intrinsics::helpers {

bool JSONStringifier::AppendJSONString(EtsHandle<EtsObject> &value, bool hasContent)
{
    if (hasContent) {
        buffer_ += ",";
    }
    buffer_ += "\"";
    buffer_ += key_;
    buffer_ += "\":";
    return SerializeObject(value);
}

void JSONStringifier::AppendJSONPrimitive(const PandaString &value, bool hasContent)
{
    if (hasContent) {
        buffer_ += ",";
    }

    buffer_ += "\"";
    buffer_ += key_;
    buffer_ += "\":";

    if ((value == "NaN") || (value == "Infinity") || (value == "-Infinity")) {
        buffer_ += "null";
    } else {
        buffer_ += value;
    }
}

bool JSONStringifier::SerializeFields(EtsHandle<EtsObject> &value)
{
    ASSERT(value.GetPtr() != nullptr);
    bool hasContent = false;
    auto keys = PandaUnorderedSet<PandaString>();
    bool isSuccessful = false;

    value->GetClass()->EnumerateBaseClasses([&](EtsClass *c) {
        auto fields = c->GetRuntimeClass()->GetFields();
        for (auto &field : fields) {
            if (!HandleField(value, EtsField::FromRuntimeField(&field), hasContent, keys)) {
                isSuccessful = false;
                return true;
            }
            isSuccessful = true;
        }
        return false;
    });
    return isSuccessful;
}

bool JSONStringifier::SerializeJSONObject(EtsHandle<EtsObject> &value)
{
    bool isContain = PushValue(value);
    if (isContain) {
        ThrowEtsException(EtsCoroutine::GetCurrent(), panda_file_items::class_descriptors::TYPE_ERROR,
                          "cyclic object value");
        return false;
    }
    buffer_ += "{";
    if (!SerializeFields(value)) {
        return false;
    };
    buffer_ += "}";
    PopValue(value);
    return true;
}

bool JSONStringifier::SerializeJSONObjectArray(EtsHandle<EtsObject> &value)
{
    auto coro = EtsCoroutine::GetCurrent();
    EtsHandleScope scope(coro);

    bool isSuccessful = false;
    buffer_ += "[";

    auto array = EtsHandle<EtsEscompatArray>(coro, EtsEscompatArray::FromEtsObject(value.GetPtr()));
    EtsInt length = 0;
    if (UNLIKELY(!array->GetLength(coro, &length))) {
        ASSERT(coro->HasPendingException());
        return false;
    }

    for (EtsInt i = 0; i < length; ++i) {
        auto optElement = array->GetRef(coro, i);
        if (UNLIKELY(!optElement)) {
            ASSERT(coro->HasPendingException());
            return false;
        }
        auto elem = EtsHandle<EtsObject>(coro, optElement.value());
        if (elem.GetPtr() == nullptr || elem->GetClass()->IsFunction()) {
            buffer_ += "null";
            isSuccessful = true;
        } else {
            isSuccessful = SerializeObject(elem);
        }
        if (!isSuccessful) {
            return false;
        }
        if (i != length - 1) {
            buffer_ += ",";
        }
    }

    buffer_ += "]";
    return isSuccessful;
}

bool JSONStringifier::DealSpecialCharacter(uint8_t ch)
{
    bool isSpecialCharacter = true;
    switch (ch) {
        case '\"':
            buffer_ += "\\\"";
            break;
        case '\\':
            buffer_ += "\\\\";
            break;
        case '\b':
            buffer_ += "\\b";
            break;
        case '\f':
            buffer_ += "\\f";
            break;
        case '\n':
            buffer_ += "\\n";
            break;
        case '\r':
            buffer_ += "\\r";
            break;
        case '\t':
            buffer_ += "\\t";
            break;
        case '\0':
            buffer_ += "\\u0000";
            break;
        default:
            if (ch < CODE_SPACE) {
                AppendUnicodeEscape(static_cast<uint32_t>(ch));
            } else {
                isSpecialCharacter = false;
            }
    }
    return isSpecialCharacter;
}

void JSONStringifier::DealUtf16Character(uint16_t ch0, uint16_t ch1)
{
    if ((IsHighSurrogate(ch0) && !IsLowSurrogate(ch1)) || IsLowSurrogate(ch0)) {
        AppendUnicodeEscape(static_cast<uint32_t>(ch0));
        return;
    }

    utf::Utf8Char uc = utf::ConvertUtf16ToUtf8(ch0, ch1, modify_);
    for (size_t j = 0; j < uc.n; ++j) {
        if (uc.n == utf::UtfLength::ONE && DealSpecialCharacter(uc.ch[j])) {
            break;
        }
        buffer_ += uc.ch[j];
    }
}

void JSONStringifier::AppendUtf16ToQuotedString(const Span<const uint16_t> &sp)
{
    buffer_ += "\"";
    uint16_t next16Code = 0;
    for (uint32_t i = 0; i < sp.size(); ++i) {
        const auto ch = sp[i];
        if (IsHighSurrogate(ch) && (i + 1 < sp.size()) && IsLowSurrogate(sp[i + 1])) {
            next16Code = sp[i + 1];
            i++;
        } else {
            next16Code = 0;
        }
        DealUtf16Character(ch, next16Code);
    }
    buffer_ += "\"";
}

uint32_t JSONStringifier::AppendUtf8Character(const Span<const uint8_t> &sp, uint32_t index)
{
    uint8_t len {0};
    const auto ch = sp[index];
    if (ch < utf::BIT_MASK_1) {
        len = utf::UtfLength::ONE;
    } else if ((ch & utf::BIT_MASK_3) == utf::BIT_MASK_2) {
        len = utf::UtfLength::TWO;
    } else if ((ch & utf::BIT_MASK_4) == utf::BIT_MASK_3) {
        len = utf::UtfLength::THREE;
    } else if ((ch & utf::BIT_MASK_5) == utf::BIT_MASK_4) {
        len = utf::UtfLength::FOUR;
    } else {
        UNREACHABLE();
    }
    ASSERT(index + len <= sp.size());
    for (uint32_t i = 0; i < len; i++) {
        buffer_ += sp[index + i];
    }
    return len;
}

void JSONStringifier::AppendUtf8ToQuotedString(const Span<const uint8_t> &sp)
{
    buffer_ += "\"";
    for (uint32_t i = 0; i < sp.size();) {
        const auto ch = sp[i];
        if (DealSpecialCharacter(ch)) {
            i++;
            continue;
        }
        i += AppendUtf8Character(sp, i);
    }
    buffer_ += "\"";
}

bool JSONStringifier::SerializeJSONString(EtsHandle<EtsObject> &value)
{
    EtsHandleScope scope(EtsCoroutine::GetCurrent());
    PandaVector<uint8_t> tree8Buf;
    PandaVector<uint16_t> tree16Buf;
    auto stringHandle = EtsHandle<EtsString>(EtsCoroutine::GetCurrent(), EtsString::FromEtsObject(value.GetPtr()));
    ASSERT(stringHandle.GetPtr() != nullptr);
    if (stringHandle->IsEmpty()) {
        buffer_ += "\"\"";
    } else if (stringHandle->IsUtf16()) {
        Span<const uint16_t> sp(stringHandle->IsTreeString() ? stringHandle->GetTreeStringDataUtf16(tree16Buf)
                                                             : stringHandle->GetDataUtf16(),
                                stringHandle->GetLength());
        AppendUtf16ToQuotedString(sp);
    } else {
        Span<const uint8_t> sp(stringHandle->IsTreeString() ? stringHandle->GetTreeStringDataMUtf8(tree8Buf)
                                                            : stringHandle->GetDataMUtf8(),
                               stringHandle->GetLength());
        AppendUtf8ToQuotedString(sp);
    }
    return true;
}

bool JSONStringifier::SerializeJSONBoxedBoolean(EtsHandle<EtsObject> &value)
{
    auto val = EtsBoxPrimitive<EtsBoolean>::Unbox(value.GetPtr());
    if (val == 0) {
        buffer_ += "false";
    } else {
        buffer_ += "true";
    }
    return true;
}

bool JSONStringifier::SerializeJSONBoxedDouble(EtsHandle<EtsObject> &value)
{
    auto val = EtsBoxPrimitive<EtsDouble>::Unbox(value.GetPtr());
    auto cache = PandaEtsVM::GetCurrent()->GetDoubleToStringCache();
    auto coro = EtsCoroutine::GetCurrent();
    PandaString v = cache->GetOrCache(coro, val)->GetMutf8();
    if ((v == "NaN") || (v == "Infinity") || (v == "-Infinity")) {
        buffer_ += "null";
    } else {
        buffer_ += v;
    }
    return true;
}

bool JSONStringifier::SerializeJSONBoxedFloat(EtsHandle<EtsObject> &value)
{
    auto val = EtsBoxPrimitive<EtsFloat>::Unbox(value.GetPtr());
    auto cache = PandaEtsVM::GetCurrent()->GetFloatToStringCache();
    auto coro = EtsCoroutine::GetCurrent();
    PandaString v = cache->GetOrCache(coro, val)->GetMutf8();
    if ((v == "NaN") || (v == "Infinity") || (v == "-Infinity")) {
        buffer_ += "null";
    } else {
        buffer_ += v;
    }
    return true;
}

bool JSONStringifier::SerializeJSONBoxedLong(EtsHandle<EtsObject> &value)
{
    auto val = EtsBoxPrimitive<EtsLong>::Unbox(value.GetPtr());
    auto cache = PandaEtsVM::GetCurrent()->GetLongToStringCache();
    auto coro = EtsCoroutine::GetCurrent();
    PandaString v = cache->GetOrCache(coro, val)->GetMutf8();
    if ((v == "NaN") || (v == "Infinity") || (v == "-Infinity")) {
        buffer_ += "null";
    } else {
        buffer_ += v;
    }
    return true;
}

template <typename T>
bool JSONStringifier::SerializeJSONBoxedPrimitiveNoCache(EtsHandle<EtsObject> &value)
{
    T val = EtsBoxPrimitive<T>::Unbox(value.GetPtr());
    if constexpr (std::is_same_v<T, EtsChar>) {
        buffer_ += "\"";
        buffer_ += static_cast<char>(val);
        buffer_ += "\"";
    } else {
        buffer_ += std::to_string(val);
    }
    return true;
}

bool JSONStringifier::SerializeEmptyObject()
{
    buffer_ += "{}";
    return true;
}

bool JSONStringifier::SerializeJSONNullValue()
{
    buffer_ += "null";
    return true;
}

template <typename T>
PandaString JSONStringifier::HandleNumeric(EtsHandle<EtsObject> &value)
{
    PandaString result;
    auto coro = EtsCoroutine::GetCurrent();
    if constexpr (std::is_same_v<T, EtsDouble>) {
        auto val = EtsBoxPrimitive<EtsDouble>::FromCoreType(value.GetPtr())->GetValue();
        auto cache = PandaEtsVM::GetCurrent()->GetDoubleToStringCache();
        auto v = cache->GetOrCache(coro, val)->GetMutf8();
        if ((v == "NaN") || (v == "Infinity") || (v == "-Infinity")) {
            result = "null";
        } else {
            result = v;
        }
    } else if constexpr (std::is_same_v<T, EtsFloat>) {
        auto val = EtsBoxPrimitive<EtsFloat>::FromCoreType(value.GetPtr())->GetValue();
        auto cache = PandaEtsVM::GetCurrent()->GetFloatToStringCache();
        auto v = cache->GetOrCache(coro, val)->GetMutf8();
        if ((v == "NaN") || (v == "Infinity") || (v == "-Infinity")) {
            result = "null";
        } else {
            result = v;
        }
    } else if constexpr (std::is_same_v<T, EtsLong>) {
        auto val = EtsBoxPrimitive<EtsLong>::FromCoreType(value.GetPtr())->GetValue();
        auto cache = PandaEtsVM::GetCurrent()->GetLongToStringCache();
        auto v = cache->GetOrCache(coro, val)->GetMutf8();
        if ((v == "NaN") || (v == "Infinity") || (v == "-Infinity")) {
            result = "null";
        } else {
            result = v;
        }
    } else if constexpr (std::is_same<T, EtsBoolean>()) {
        auto val = EtsBoxPrimitive<EtsBoolean>::FromCoreType(value.GetPtr())->GetValue();
        result = val == 0 ? "false" : "true";
    } else if constexpr (std::is_same<T, EtsChar>()) {
        auto val = EtsBoxPrimitive<EtsChar>::FromCoreType(value.GetPtr())->GetValue();
        result = static_cast<char>(val);
    } else {
        T val = EtsBoxPrimitive<T>::FromCoreType(value.GetPtr())->GetValue();
        result = std::to_string(val);
    }
    return result;
}

bool JSONStringifier::HandleRecordKey(EtsHandle<EtsObject> &key)
{
    if (key->IsStringClass()) {
        key_ = EtsString::FromEtsObject(key.GetPtr())->GetMutf8();
    } else {
        auto platformTypes = PlatformTypes(EtsCoroutine::GetCurrent());
        if (key->IsInstanceOf(platformTypes->coreBoolean)) {
            key_ = HandleNumeric<EtsBoolean>(key);
        } else if (key->IsInstanceOf(platformTypes->coreDouble)) {
            key_ = HandleNumeric<EtsDouble>(key);
        } else if (key->IsInstanceOf(platformTypes->coreFloat)) {
            key_ = HandleNumeric<EtsFloat>(key);
        } else if (key->IsInstanceOf(platformTypes->coreLong)) {
            key_ = HandleNumeric<EtsLong>(key);
        } else if (key->IsInstanceOf(platformTypes->coreByte)) {
            key_ = HandleNumeric<EtsByte>(key);
        } else if (key->IsInstanceOf(platformTypes->coreShort)) {
            key_ = HandleNumeric<EtsShort>(key);
        } else if (key->IsInstanceOf(platformTypes->coreChar)) {
            key_ = HandleNumeric<EtsChar>(key);
        } else if (key->IsInstanceOf(platformTypes->coreInt)) {
            key_ = HandleNumeric<EtsInt>(key);
        }
    }
    return true;
}

bool JSONStringifier::SerializeJSONRecord(EtsHandle<EtsObject> &value)
{
    auto coro = EtsCoroutine::GetCurrent();

    EtsHandleScope scope(coro);
    auto recordObj = EtsHandle<EtsEscompatMap>(coro, reinterpret_cast<EtsEscompatMap *>(value->GetCoreType()));
    if (recordObj->GetSize() == 0) {
        buffer_ += "{}";
        return true;
    }

    bool isContain = PushValue(value);
    if (isContain) {
        ThrowEtsException(coro, panda_file_items::class_descriptors::TYPE_ERROR, "cyclic object value");
        return false;
    }

    auto hasContent = false;
    EtsEscompatMap::MapIdx recIdx = recordObj->GetFirstIdx(coro);
    if (recIdx == EtsEscompatMap::MAP_IDX_END) {
        buffer_ += "{}";
        return true;
    }
    buffer_ += "{";
    do {
        EtsHandle<EtsObject> key(coro, recordObj->GetKey(coro, recIdx));
        EtsHandle<EtsObject> val(coro, recordObj->GetValue(coro, recIdx));
        recIdx = recordObj->GetNextIdx(coro, recIdx);
        if (key.GetPtr() == nullptr || val.GetPtr() == nullptr) {
            continue;
        }
        if (val->GetClass()->IsFunction()) {
            continue;
        }
        HandleRecordKey(key);
        AppendJSONString(val, hasContent);
        hasContent = true;
    } while (recIdx != EtsEscompatMap::MAP_IDX_END);
    buffer_ += "}";
    PopValue(value);
    return true;
}

bool JSONStringifier::PushValue(EtsHandle<EtsObject> &value)
{
    ASSERT(value.GetPtr() != nullptr);
    if (path_.find(value->GetHashCode()) != path_.end()) {
        return true;
    }
    path_.insert(value->GetHashCode());
    return false;
}

void JSONStringifier::PopValue(EtsHandle<EtsObject> &value)
{
    ASSERT(value.GetPtr() != nullptr);
    path_.erase(value->GetHashCode());
}

PandaString JSONStringifier::ResolveDisplayName(const EtsField *field)
{
    auto fieldNameData = field->GetCoreType()->GetName();
    auto fieldNameLength = fieldNameData.utf16Length;
    std::string_view fieldName(utf::Mutf8AsCString(fieldNameData.data), fieldNameLength);
    return PandaString(reinterpret_cast<const char *>(fieldNameData.data), fieldNameLength);
}

bool JSONStringifier::CheckUnsupportedAnnotation(EtsField *field)
{
    auto *runtimeClass = field->GetDeclaringClass()->GetRuntimeClass();
    const panda_file::File &pf = *runtimeClass->GetPandaFile();
    panda_file::FieldDataAccessor fda(pf, field->GetRuntimeField()->GetFileId());
    bool hasAnnotation = false;
    fda.EnumerateAnnotations([&hasAnnotation]([[maybe_unused]] panda_file::File::EntityId annId) {
        hasAnnotation = true;
        return;
    });
    return hasAnnotation;
}

// CC-OFFNXT(huge_method[C++], huge_cyclomatic_complexity[C++], G.FUN.01-CPP) solid logic
// CC-OFFNXT(huge_cca_cyclomatic_complexity[C++]) solid logic
bool JSONStringifier::SerializeObject(EtsHandle<EtsObject> &value)
{
    if (value.GetPtr() == nullptr) {
        return true;
    }

    auto coro = EtsCoroutine::GetCurrent();
    auto platformTypes = PlatformTypes(coro);
    bool isSuccessful = false;

    auto *valueCls = value->GetClass();
    auto *valueRtCls = valueCls->GetRuntimeClass();
    auto desc = valueCls->GetDescriptor();
    if (desc == panda_file_items::class_descriptors::BOX_BOOLEAN) {
        buffer_ += HandleNumeric<EtsBoolean>(value);
        isSuccessful = true;
    } else if (desc == panda_file_items::class_descriptors::BOX_DOUBLE) {
        buffer_ += HandleNumeric<EtsDouble>(value);
        isSuccessful = true;
    } else if (desc == panda_file_items::class_descriptors::BOX_FLOAT) {
        buffer_ += HandleNumeric<EtsFloat>(value);
        isSuccessful = true;
    } else if (desc == panda_file_items::class_descriptors::BOX_LONG) {
        buffer_ += HandleNumeric<EtsLong>(value);
        isSuccessful = true;
    } else if (desc == panda_file_items::class_descriptors::BOX_BYTE) {
        buffer_ += HandleNumeric<EtsByte>(value);
        isSuccessful = true;
    } else if (desc == panda_file_items::class_descriptors::BOX_SHORT) {
        buffer_ += HandleNumeric<EtsShort>(value);
        isSuccessful = true;
    } else if (desc == panda_file_items::class_descriptors::BOX_CHAR) {
        buffer_ += "\"" + HandleNumeric<EtsChar>(value) + "\"";
        isSuccessful = true;
    } else if (desc == panda_file_items::class_descriptors::BOX_INT) {
        isSuccessful = SerializeJSONBoxedPrimitiveNoCache<EtsInt>(value);
    } else if (desc == panda_file_items::class_descriptors::STRING ||
               desc == panda_file_items::class_descriptors::LINE_STRING ||
               desc == panda_file_items::class_descriptors::SLICED_STRING ||
               desc == panda_file_items::class_descriptors::TREE_STRING) {
        isSuccessful = SerializeJSONString(value);
    } else if (desc == panda_file_items::class_descriptors::RECORD) {
        coro->ManagedCodeEnd();
        {
            ScopedManagedCodeThread v(coro);
            isSuccessful = SerializeJSONRecord(value);
        }
        coro->ManagedCodeBegin();
    } else if (desc == panda_file_items::class_descriptors::DATE) {
        isSuccessful = false;
    } else if (desc == panda_file_items::class_descriptors::ARRAY) {
        coro->ManagedCodeEnd();
        {
            ScopedManagedCodeThread v(coro);
            isSuccessful = SerializeJSONObjectArray(value);
        }
        coro->ManagedCodeBegin();
    } else if (desc == panda_file_items::class_descriptors::PROMISE ||
               desc == panda_file_items::class_descriptors::SET || desc == panda_file_items::class_descriptors::MAP ||
               desc == panda_file_items::class_descriptors::MAPENTRY) {
        isSuccessful = SerializeEmptyObject();
    } else if (desc == panda_file_items::class_descriptors::NULL_VALUE) {
        isSuccessful = SerializeJSONNullValue();
    } else if (desc == panda_file_items::class_descriptors::JS_VALUE) {
        isSuccessful = false;
    } else {
        if (value->IsInstanceOf(platformTypes->stdcoreRegExpExecArray)) {
            coro->ManagedCodeEnd();
            {
                ScopedManagedCodeThread v(coro);
                auto result = EtsHandle<EtsObject>(coro, value->GetFieldObject(valueCls->GetFieldByIndex(1)));
                isSuccessful = SerializeJSONObjectArray(result);
            }
            coro->ManagedCodeBegin();
        } else if (value->IsInstanceOf(platformTypes->stdcoreJsonReplacer)) {
            PandaVector<Value> args {Value(value->GetCoreType())};
            auto jsonReplacerMethod = valueCls->GetInstanceMethod("jsonReplacer", nullptr)->GetPandaMethod();
            auto ret = jsonReplacerMethod->Invoke(coro, args.data());
            if (UNLIKELY(coro->HasPendingException())) {
                return false;
            }
            auto retobj = EtsHandle<EtsObject>(coro, EtsObject::FromCoreType(ret.GetAs<ObjectHeader *>()));
            coro->ManagedCodeEnd();
            {
                ScopedManagedCodeThread v(coro);
                isSuccessful = SerializeJSONRecord(retobj);
            }
            coro->ManagedCodeBegin();
        } else if (valueCls->IsFunction()) {
            buffer_ += "undefined";
        } else if (value->IsInstanceOf(platformTypes->coreTuple)) {
            isSuccessful = false;
        } else if (value->IsInstanceOf(platformTypes->coreBigint)) {
            isSuccessful = false;
        } else if (valueRtCls->Implements(platformTypes->coreJsonElementSerializable->GetRuntimeClass())) {
            isSuccessful = false;
        } else if (valueCls->IsClass()) {
            coro->ManagedCodeEnd();
            {
                ScopedManagedCodeThread v(coro);
                isSuccessful = SerializeJSONObject(value);
            }
            coro->ManagedCodeBegin();
        } else {
            LOG(ERROR, ETS) << "Unsupported type: " << valueCls->GetDescriptor();
            return false;
        }
    }
    return isSuccessful;
}

// CC-OFFNXT(huge_method[C++], G.FUN.01-CPP) solid logic
bool JSONStringifier::HandleField(EtsHandle<EtsObject> &obj, EtsField *etsField, bool &hasContent,
                                  PandaUnorderedSet<PandaString> &keys)
{
    if (!etsField->IsPublic() || etsField->IsStatic()) {
        return true;
    }

    if (CheckUnsupportedAnnotation(etsField)) {
        return false;
    }
    key_ = ResolveDisplayName(etsField);
    if (keys.find(key_) != keys.end()) {
        return false;
    }
    keys.insert(key_);
    auto coro = EtsCoroutine::GetCurrent();
    auto etsType = etsField->GetEtsType();

    switch (etsType) {
        case EtsType::BOOLEAN: {
            auto val = obj->GetFieldPrimitive<EtsBoolean>(etsField);
            PandaString value = val == 0 ? "false" : "true";
            AppendJSONPrimitive(value, hasContent);
            break;
        }
        case EtsType::BYTE: {
            auto val = obj->GetFieldPrimitive<EtsByte>(etsField);
            AppendJSONPrimitive(PandaString(std::to_string(val)), hasContent);
            break;
        }
        case EtsType::CHAR: {
            auto val = obj->GetFieldPrimitive<EtsChar>(etsField);
            AppendJSONPrimitive(PandaString(std::to_string(val)), hasContent);
            break;
        }
        case EtsType::SHORT: {
            auto val = obj->GetFieldPrimitive<EtsShort>(etsField);
            AppendJSONPrimitive(PandaString(std::to_string(val)), hasContent);
            break;
        }
        case EtsType::INT: {
            auto val = obj->GetFieldPrimitive<EtsInt>(etsField);
            AppendJSONPrimitive(PandaString(std::to_string(val)), hasContent);
            break;
        }
        case EtsType::LONG: {
            ASSERT(obj.GetPtr() != nullptr);
            auto val = obj->GetFieldPrimitive<EtsLong>(etsField);
            auto cache = PandaEtsVM::GetCurrent()->GetLongToStringCache();
            PandaString v = cache->GetOrCache(coro, val)->GetMutf8();
            AppendJSONPrimitive(v, hasContent);
            break;
        }
        case EtsType::FLOAT: {
            auto val = obj->GetFieldPrimitive<EtsFloat>(etsField);
            auto cache = PandaEtsVM::GetCurrent()->GetFloatToStringCache();
            PandaString v = cache->GetOrCache(coro, val)->GetMutf8();
            AppendJSONPrimitive(v, hasContent);
            break;
        }
        case EtsType::DOUBLE: {
            auto val = obj->GetFieldPrimitive<EtsDouble>(etsField);
            auto cache = PandaEtsVM::GetCurrent()->GetDoubleToStringCache();
            PandaString v = cache->GetOrCache(coro, val)->GetMutf8();
            AppendJSONPrimitive(v, hasContent);
            break;
        }
        default:
            auto fieldObj = EtsHandle<EtsObject>(EtsCoroutine::GetCurrent(), obj->GetFieldObject(etsField));
            if (fieldObj.GetPtr() == nullptr || fieldObj->GetClass()->IsFunction()) {
                return true;
            }
            if (!AppendJSONString(fieldObj, hasContent)) {
                return false;
            }
            break;
    }
    hasContent = true;
    return true;
}

EtsString *JSONStringifier::Stringify(EtsHandle<EtsObject> &value)
{
    bool result = false;
    auto coro = EtsCoroutine::GetCurrent();
    ASSERT(coro != nullptr);
    result = SerializeObject(value);
    if (!result || coro->HasPendingException()) {
        return nullptr;
    }
    return EtsString::CreateFromUtf8(buffer_.c_str(), buffer_.length());
}

}  // namespace ark::ets::intrinsics::helpers
