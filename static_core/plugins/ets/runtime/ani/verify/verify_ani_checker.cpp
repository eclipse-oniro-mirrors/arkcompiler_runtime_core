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

#include "plugins/ets/runtime/ani/verify/verify_ani_checker.h"

#include "plugins/ets/runtime/ani/ani_converters.h"
#include "plugins/ets/runtime/ani/ani_interaction_api.h"
#include "plugins/ets/runtime/ani/verify/types/venv.h"
#include "plugins/ets/runtime/ani/verify/types/vvm.h"
#include "plugins/ets/runtime/ets_vm.h"
#include "plugins/ets/runtime/types/ets_method.h"

namespace ark::ets::ani::verify {

class CallArgs {
public:
    explicit CallArgs(const EtsMethod *etsMethod, const ani_value *args)
        : method_(etsMethod->GetPandaMethod()), args_(args)
    {
    }

    size_t GetNrArgs() const
    {
        return method_->GetNumArgs();
    }

    template <typename Callback>
    void ForEachArgs(Callback callback) const
    {
        static_assert(std::is_invocable_r_v<bool, Callback, ani_value, panda_file::Type>, "wrong callback signature");

        panda_file::ShortyIterator it(method_->GetShorty());
        panda_file::ShortyIterator end;
        ++it;  // skip the return value

        size_t i = 0;
        for (; it != end; ++it) {
            panda_file::Type type = *it;
            if (!callback(args_[i++], type)) {  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                return;
            }
        }
    }

private:
    const Method *method_ {};
    const ani_value *args_ {};
};

std::string_view ANIRefTypeToString(ANIRefType refType)
{
    // clang-format off
    switch (refType) {
        case ANIRefType::REFERENCE:             return "ani_ref";
        case ANIRefType::MODULE:                return "ani_module";
        case ANIRefType::NAMESPACE:             return "ani_namespace";
        case ANIRefType::OBJECT:                return "ani_object";
        case ANIRefType::FN_OBJECT:             return "ani_fn_object";
        case ANIRefType::ENUM_ITEM:             return "ani_enum_item";
        case ANIRefType::ERROR:                 return "ani_error";
        case ANIRefType::ARRAYBUFFER:           return "ani_arraybuffer";
        case ANIRefType::STRING:                return "ani_string";
        case ANIRefType::ARRAY:                 return "ani_array";
        case ANIRefType::TYPE:                  return "ani_type";
        case ANIRefType::CLASS:                 return "ani_class";
        case ANIRefType::ENUM:                  return "ani_enum";
        case ANIRefType::FIXED_ARRAY_BOOLEAN:   return "ani_fixedarray_boolean";
        case ANIRefType::FIXED_ARRAY_CHAR:      return "ani_fixedarray_char";
        case ANIRefType::FIXED_ARRAY_BYTE:      return "ani_fixedarray_byte";
        case ANIRefType::FIXED_ARRAY_SHORT:     return "ani_fixedarray_short";
        case ANIRefType::FIXED_ARRAY_INT:       return "ani_fixedarray_int";
        case ANIRefType::FIXED_ARRAY_LONG:      return "ani_fixedarray_long";
        case ANIRefType::FIXED_ARRAY_FLOAT:     return "ani_fixedarray_float";
        case ANIRefType::FIXED_ARRAY_DOUBLE:    return "ani_fixedarray_double";
    }
    // clang-format on
    UNREACHABLE();
}

template <class T>
void FormatPointer(PandaStringStream &ss, T *ptr)
{
    if (ptr == nullptr) {
        ss << "NULL";
    } else {
        ss << ptr;
    }
}

// CC-OFFNXT(G.FUD.05) solid logic
PandaString ANIArg::GetStringValue() const
{
    PandaStringStream ss;
    switch (type_) {
        case ValueType::ANI_SIZE:
            ss << GetValueSize();
            break;
        case ValueType::UINT32:
            ss << "0x" << std::setfill('0') << std::setw(2U * sizeof(uint32_t)) << std::hex << GetValueU32();
            break;
        case ValueType::METHOD_ARGS:
            if (action_ == Action::VERIFY_METHOD_A_ARGS) {
                const ani_value *args = GetValueMethodArgs()->vargs;
                FormatPointer(ss, args);
            } else if (action_ == Action::VERIFY_METHOD_V_ARGS) {
                // Do nothing
            } else {
                UNREACHABLE();
            }
            break;
        default:
            FormatPointer(ss, GetValueRawPointer());
            break;
    }
    return ss.str();
}

PandaString ANIArg::GetStringType() const
{
    // clang-format off
    switch (type_) {
        case ValueType::ANI_ENV:                          return "ani_env *";
        case ValueType::ANI_VM:                           return "ani_vm *";
        case ValueType::ANI_OPTIONS:                      return "ani_options *";
        case ValueType::ANI_SIZE:                         return "ani_size";
        case ValueType::ANI_REF:                          return "ani_ref";
        case ValueType::ANI_CLASS:                        return "ani_class";
        case ValueType::ANI_METHOD:                       return "ani_method";
        case ValueType::ANI_STRING:                       return "ani_string";
        case ValueType::ANI_VALUE_ARGS:                   return "const ani_value *";
        case ValueType::ANI_UTF8_BUFFER:                  return "char *";
        case ValueType::ANI_UTF8_STRING:                  return "const char *";
        case ValueType::ANI_UTF16_BUFFER:                 return "uint16_t *";
        case ValueType::ANI_UTF16_STRING:                 return "const uint16_t *";
        case ValueType::ANI_ENV_STORAGE:                  return "ani_env **";
        case ValueType::ANI_VM_STORAGE:                   return "ani_env **";
        case ValueType::ANI_BOOLEAN_STORAGE:              return "ani_boolean *";
        case ValueType::ANI_REF_STORAGE:                  return "ani_ref *";
        case ValueType::ANI_OBJECT_STORAGE:               return "ani_object *";
        case ValueType::ANI_STRING_STORAGE:               return "ani_string *";
        case ValueType::ANI_SIZE_STORAGE:                 return "ani_size *";
        case ValueType::UINT32:                           return "uint32_t";
        case ValueType::ANI_ERROR:                        return "ani_error";
        case ValueType::ANI_ERROR_STORAGE:                return "ani_error *";
        case ValueType::ANI_FIXED_ARRAY_BOOLEAN_STORAGE:  return "ani_fixedarray_boolean *";
        case ValueType::ANI_FIXED_ARRAY_CHAR_STORAGE:     return "ani_fixedarray_char *";
        case ValueType::ANI_FIXED_ARRAY_BYTE_STORAGE:     return "ani_fixedarray_byte *";
        case ValueType::ANI_FIXED_ARRAY_SHORT_STORAGE:    return "ani_fixedarray_short *";
        case ValueType::ANI_FIXED_ARRAY_INT_STORAGE:      return "ani_fixedarray_int *";
        case ValueType::ANI_FIXED_ARRAY_LONG_STORAGE:     return "ani_fixedarray_long *";
        case ValueType::ANI_FIXED_ARRAY_FLOAT_STORAGE:    return "ani_fixedarray_float *";
        case ValueType::ANI_FIXED_ARRAY_DOUBLE_STORAGE:   return "ani_fixedarray_double *";
        default:                                          UNREACHABLE(); return "";
        case ValueType::METHOD_ARGS:
            if (action_ == Action::VERIFY_METHOD_A_ARGS) {
                return "ani_value *";
            } else if (action_ == Action::VERIFY_METHOD_V_ARGS) {
                return "";
            } else {
                UNREACHABLE();
            }
            break;
    }
    // clang-format on
    UNREACHABLE();
}

static bool IsValidRawAniValue(EnvANIVerifier *envANIVerifier, ani_value v, panda_file::Type type, bool isVaArgs)
{
    panda_file::Type::TypeId typeId = type.GetId();

    if (isVaArgs) {
        constexpr ani_long BOOLEAN_MASK = ~0x1ULL;
        constexpr ani_long CHAR_MASK = ~0xffffULL;
        using I8Limits = std::numeric_limits<int8_t>;
        using I16Limits = std::numeric_limits<int16_t>;
        using I32Limits = std::numeric_limits<int32_t>;

        switch (typeId) {
            // clang-format off
            case panda_file::Type::TypeId::U1:  return 0 == (v.l & BOOLEAN_MASK);  // NOLINT(hicpp-signed-bitwise)
            case panda_file::Type::TypeId::U16: return 0 == (v.l & CHAR_MASK);     // NOLINT(hicpp-signed-bitwise)
            case panda_file::Type::TypeId::I8:  return v.i >= I8Limits::min() && v.i <= I8Limits::max();
            case panda_file::Type::TypeId::I16: return v.i >= I16Limits::min() && v.i <= I16Limits::max();
            case panda_file::Type::TypeId::I32: return v.i >= I32Limits::min() && v.i <= I32Limits::max();
            case panda_file::Type::TypeId::I64: return true;
            case panda_file::Type::TypeId::F32: return true;  // uses double
            case panda_file::Type::TypeId::F64: return true;
            case panda_file::Type::TypeId::REFERENCE:
                return envANIVerifier->IsValidInCurrentFrame(reinterpret_cast<VRef *>(v.r));
            // clang-format on
            default:
                break;
        }
    } else {
        switch (typeId) {
            // clang-format off
            case panda_file::Type::TypeId::U1:  return v.b == ANI_FALSE || v.b == ANI_TRUE;
            case panda_file::Type::TypeId::U16: return true;
            case panda_file::Type::TypeId::I8:  return true;
            case panda_file::Type::TypeId::I16: return true;
            case panda_file::Type::TypeId::I32: return true;
            case panda_file::Type::TypeId::I64: return true;
            case panda_file::Type::TypeId::F32: return true;
            case panda_file::Type::TypeId::F64: return true;
            case panda_file::Type::TypeId::REFERENCE:
                return envANIVerifier->IsValidInCurrentFrame(reinterpret_cast<VRef *>(v.r));
            // clang-format on
            default:
                break;
        }
    }

    LOG(FATAL, ANI) << "Unexpected argument type: " << static_cast<int>(typeId);
    return false;
}

class Verifier {
public:
    explicit Verifier(VVm *vvm, VEnv *venv) : vvm_(vvm), venv_(venv) {}

    std::optional<PandaString> VerifyVm(VVm *vvm)
    {
        if (UNLIKELY(vvm != vvm_)) {
            return "wrong VM pointer";
        }
        return {};
    }

    std::optional<PandaString> VerifyEnv(VEnv *venv, bool checkPendingError)
    {
        if (UNLIKELY(venv_ == nullptr)) {
            return "current native thread is not attached";
        }
        if (UNLIKELY(venv != venv_)) {
            return "called from incorrect the native scope";
        }
        PandaEnv *pandaEnv = PandaEnv::FromAniEnv(venv_->GetEnv());
        ASSERT(pandaEnv == EtsCoroutine::GetCurrent()->GetEtsNapiEnv());
        if (UNLIKELY(checkPendingError && pandaEnv->HasPendingException())) {
            return "has unhandled an error";
        }
        return {};
    }

    std::optional<PandaString> VerifyOptions(const ani_options *options)
    {
        if (options == nullptr) {
            return {};
        }
        if (options->options == nullptr) {
            return "wrong 'options' pointer, options->options == NULL";
        }
        const size_t maxNrOptions = 4096;
        if (options->nr_options > maxNrOptions) {
            PandaStringStream ss;
            ss << "'nr_options' value is too large. options->nr_options == " << options->nr_options;
            return ss.str();
        }

        for (size_t i = 0; i < options->nr_options; ++i) {
            const ani_option &opt = options->options[i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            if (opt.option == nullptr) {
                PandaStringStream ss;
                ss << "wrong 'option' pointer, options->options[" << i << "].option == NULL";
                return ss.str();
            }
            constexpr std::string_view EXT_PREFIX = "--ext:";
            std::string_view name = opt.option;
            if (name.substr(0, EXT_PREFIX.size()) == EXT_PREFIX) {
                std::string_view subName = name.substr(EXT_PREFIX.size());
                if (subName.empty() || !(isascii(subName[0]) != 0 && std::isalpha(subName[0]) != 0)) {
                    PandaStringStream ss;
                    ss << "wrong 'option' value, options->options[" << i << "].option == " << name;
                    return ss.str();
                }
            }
        }
        return {};
    }

    template <typename Type>
    std::optional<PandaString> VerifyTypeStorage(Type value, std::string_view TypeName)
    {
        if (value == nullptr) {
            PandaStringStream ss;
            ss << "wrong pointer for storing '" << TypeName << "'";
            return ss.str();
        }
        return {};
    }

    template <typename Type>
    std::optional<PandaString> VerifyTypePtr(Type value, std::string_view TypeName)
    {
        if (value == nullptr) {
            PandaStringStream ss;
            ss << "wrong pointer to use as argument in '" << TypeName << "'";
            return ss.str();
        }
        return {};
    }

    std::optional<PandaString> VerifyEnvVersion(uint32_t version)
    {
        if (!IsVersionSupported(version)) {
            return "unsupported ANI version";
        }
        return {};
    }

    std::optional<PandaString> VerifyNrRefs(ani_size nrRefs)
    {
        if (nrRefs == 0) {
            return "wrong value";
        }
        if (nrRefs > std::numeric_limits<uint16_t>::max()) {
            PandaStringStream ss;
            ss << "it is too big";
            return ss.str();
        }
        return {};
    }

    std::optional<PandaString> VerifyRef(VRef *vref)
    {
        if (!GetEnvANIVerifier()->IsValidInCurrentFrame(vref)) {
            return "wrong reference";
        }
        return {};
    }

    std::optional<PandaString> VerifyClass(VClass *vclass)
    {
        auto err = VerifyRef(vclass);
        if (err) {
            return err;
        }
        ANIRefType refType = vclass->GetRefType();
        if (refType != ANIRefType::CLASS) {
            PandaStringStream ss;
            ss << "wrong reference type: " << ANIRefTypeToString(refType);
            return ss.str();
        }
        return {};
    }

    std::optional<PandaString> VerifyString(VString *vstr)
    {
        auto err = VerifyRef(vstr);
        if (err) {
            return err;
        }
        ANIRefType refType = vstr->GetRefType();
        if (refType != ANIRefType::STRING) {
            PandaStringStream ss;
            ss << "wrong reference type: " << ANIRefTypeToString(refType);
            return ss.str();
        }
        return {};
    }

    std::optional<PandaString> VerifyError(VError *verr)
    {
        auto errMessage = VerifyRef(verr);
        if (errMessage) {
            return errMessage;
        }
        ANIRefType refType = verr->GetRefType();
        if (refType != ANIRefType::ERROR) {
            PandaStringStream ss;
            ss << "wrong reference type: " << ANIRefTypeToString(refType);
            return ss.str();
        }
        return {};
    }

    std::optional<PandaString> VerifyDelLocalRef(VRef *vref)
    {
        EnvANIVerifier *envANIVerifier = GetEnvANIVerifier();
        if (!envANIVerifier->IsValidInCurrentFrame(vref)) {
            return "it is not local reference";
        }
        if (!envANIVerifier->CanBeDeletedFromCurrentScope(vref)) {
            return "a local reference can only be deleted in the scope where it was created";
        }
        return {};
    }

    std::optional<PandaString> VerifyCtor(ani_method ctor)
    {
        if (ctor == nullptr) {
            return "wrong ctor value";
        }
        // NOTE: Add ctor verification, #26993
        return {};
    }

    std::optional<PandaString> VerifyMethod(ani_method method)
    {
        if (method == nullptr) {
            return "wrong method value";
        }
        // NOTE: Add method verification, #26993
        return {};
    }

    std::optional<PandaString> VerifyMethodAArgs(ANIArg::AniMethodArgs *methodArgs)
    {
        ASSERT(methodArgs != nullptr);
        if (methodArgs->vargs == nullptr) {
            return "wrong arguments value";
        }
        return DoVerifyMethodArgs(methodArgs);
    }

    std::optional<PandaString> VerifyMethodVArgs(ANIArg::AniMethodArgs *methodArgs)
    {
        ASSERT(methodArgs != nullptr);
        return DoVerifyMethodArgs(methodArgs);
    }

    std::optional<PandaString> DoVerifyMethodArgs(ANIArg::AniMethodArgs *methodArgs)
    {
        CallArgs callArgs(ToInternalMethod(methodArgs->method), methodArgs->vargs);
        EnvANIVerifier *envANIVerifier = GetEnvANIVerifier();
        std::optional<PandaString> err;
        callArgs.ForEachArgs([&](ani_value value, panda_file::Type type) -> bool {
            if (UNLIKELY(!IsValidRawAniValue(envANIVerifier, value, type, methodArgs->isVaArgs))) {
                err = "wrong method arguments";
                return false;
            }
            // NOTE: Add checker of ref type
            return true;
        });
        return err;
    }

private:
    EnvANIVerifier *GetEnvANIVerifier()
    {
        ASSERT(venv_ != nullptr);
        return PandaEnv::FromAniEnv(venv_->GetEnv())->GetEnvANIVerifier();
    }

    VVm *vvm_ {};
    VEnv *venv_ {};
};

using CheckerHandler = std::optional<PandaString> (*)(Verifier &, const ANIArg &);

static std::optional<PandaString> VerifyVm(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_VM);
    return v.VerifyVm(arg.GetValueVm());
}

static std::optional<PandaString> VerifyEnv(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_ENV);
    return v.VerifyEnv(arg.GetValueEnv(), true);
}

static std::optional<PandaString> VerifyEnvSkipPendingError(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_ENV_SKIP_PENDING_ERROR);
    return v.VerifyEnv(arg.GetValueEnv(), false);
}

static std::optional<PandaString> VerifyOptions(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_OPTIONS);
    return v.VerifyOptions(arg.GetValueOptions());
}

static std::optional<PandaString> VerifyEnvVersion(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_ENV_VERSION);
    return v.VerifyEnvVersion(arg.GetValueU32());
}

static std::optional<PandaString> VerifyNrRefs(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_NR_REFS);
    return v.VerifyNrRefs(arg.GetValueSize());
}

static std::optional<PandaString> VerifyRef(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_REF);
    return v.VerifyRef(arg.GetValueRef());
}

static std::optional<PandaString> VerifyClass(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_CLASS);
    return v.VerifyClass(arg.GetValueClass());
}

static std::optional<PandaString> VerifyString(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_STRING);
    return v.VerifyString(arg.GetValueString());
}

static std::optional<PandaString> VerifyUTF8Buffer(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_UTF8_BUFFER);
    return v.VerifyTypePtr(arg.GetValueUTF8Buffer(), "char *");
}

static std::optional<PandaString> VerifyError(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_ERROR);
    return v.VerifyError(arg.GetValueError());
}

static std::optional<PandaString> VerifyUTF8String(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_UTF8_STRING);
    return v.VerifyTypePtr(arg.GetValueUTF8String(), "const char *");
}

static std::optional<PandaString> VerifyUTF16Buffer(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_UTF16_BUFFER);
    return v.VerifyTypePtr(arg.GetValueUTF16Buffer(), "uint16_t *");
}

static std::optional<PandaString> VerifyUTF16String(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_UTF16_STRING);
    return v.VerifyTypePtr(arg.GetValueUTF16String(), "const uint16_t *");
}

static std::optional<PandaString> VerifyDelLocalRef(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_DEL_LOCAL_REF);
    return v.VerifyDelLocalRef(arg.GetValueRef());
}

static std::optional<PandaString> VerifyCtor(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_CTOR);
    return v.VerifyCtor(arg.GetValueMethod());
}

static std::optional<PandaString> VerifyMethod(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_METHOD);
    return v.VerifyMethod(arg.GetValueMethod());
}

static std::optional<PandaString> VerifyMethodAArgs(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_METHOD_A_ARGS);
    return v.VerifyMethodAArgs(arg.GetValueMethodArgs());
}

static std::optional<PandaString> VerifyMethodVArgs(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_METHOD_V_ARGS);
    return v.VerifyMethodVArgs(arg.GetValueMethodArgs());
}

static std::optional<PandaString> VerifyVmStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_VM_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueVmStorage(), "ani_vm *");
}

static std::optional<PandaString> VerifyEnvStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_ENV_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueEnvStorage(), "ani_env *");
}

static std::optional<PandaString> VerifyBooleanStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_BOOLEAN_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueBooleanStorage(), "ani_boolean");
}

static std::optional<PandaString> VerifyStringStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_STRING_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueStringStorage(), "ani_string");
}

static std::optional<PandaString> VerifySizeStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_SIZE_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueSizeStorage(), "ani_size");
}

static std::optional<PandaString> VerifySize([[maybe_unused]] Verifier &v, [[maybe_unused]] const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_SIZE);
    return {};
}

static std::optional<PandaString> VerifyRefStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_REF_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueRefStorage(), "ani_ref");
}

static std::optional<PandaString> VerifyObjectStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_OBJECT_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueObjectStorage(), "ani_object");
}

static std::optional<PandaString> VerifyErrorStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_ERROR_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueErrorStorage(), "ani_error");
}

static std::optional<PandaString> VerifyFixedArrayBooleanStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_FIXED_ARRAY_BOOLEAN_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueFixedArrayBooleanStorage(), "ani_fixedarray_boolean");
}

static std::optional<PandaString> VerifyFixedArrayCharStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_FIXED_ARRAY_CHAR_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueFixedArrayCharStorage(), "ani_fixedarray_char");
}

static std::optional<PandaString> VerifyFixedArrayByteStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_FIXED_ARRAY_BYTE_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueFixedArrayByteStorage(), "ani_fixedarray_byte");
}

static std::optional<PandaString> VerifyFixedArrayShortStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_FIXED_ARRAY_SHORT_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueFixedArrayShortStorage(), "ani_fixedarray_short");
}

static std::optional<PandaString> VerifyFixedArrayIntStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_FIXED_ARRAY_INT_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueFixedArrayIntStorage(), "ani_fixedarray_int");
}

static std::optional<PandaString> VerifyFixedArrayLongStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_FIXED_ARRAY_LONG_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueFixedArrayLongStorage(), "ani_fixedarray_long");
}

static std::optional<PandaString> VerifyFixedArrayFloatStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_FIXED_ARRAY_FLOAT_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueFixedArrayFloatStorage(), "ani_fixedarray_float");
}

static std::optional<PandaString> VerifyFixedArrayDoubleStorage(Verifier &v, const ANIArg &arg)
{
    ASSERT(arg.GetAction() == ANIArg::Action::VERIFY_FIXED_ARRAY_DOUBLE_STORAGE);
    return v.VerifyTypeStorage(arg.GetValueFixedArrayDoubleStorage(), "ani_fixedarray_double");
}

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
static constexpr std::array<CheckerHandler, helpers::ToUnderlying(ANIArg::Action::NUMBER_OF_ELEMENTS)> HANDLERS = {
// CC-OFFNXT(G.PRE.02) code generation
#define X(ACTION, FN) FN,
    ANI_VERIFICATION_MAP
#undef X
};
// NOLINTEND(cppcoreguidelines-macro-usage)

static void DoAbortANI(PandaEtsVM *etsVm, std::string_view functionName, std::string_view message)
{
    PandaStringStream ss;
    ss << "DETECT AN ERROR WHEN USING ANI:";
    ss << "\n  ANI method: " << functionName;
    ss << "\n" << message;
    etsVm->GetANIVerifier()->Abort(ss.str());
}

struct ArgInfo {
    ANIArg arg;
    std::optional<PandaString> err;
};

struct ExtArgInfo {
    PandaString name;
    int64_t value;
    panda_file::Type type;
    bool isValid;
};

struct ArgsInfo {
    PandaVector<ArgInfo> argInfoList;
    std::optional<PandaVector<ExtArgInfo>> extArgInfoList;
};

static PandaString GetAniTypeByType(panda_file::Type type)
{
    // clang-format off
    switch (type.GetId()) {
        case panda_file::Type::TypeId::U1:        return "ani_boolean";
        case panda_file::Type::TypeId::U16:       return "ani_char";
        case panda_file::Type::TypeId::I8:        return "ani_byte";
        case panda_file::Type::TypeId::I16:       return "ani_short";
        case panda_file::Type::TypeId::I32:       return "ani_int";
        case panda_file::Type::TypeId::I64:       return "ani_long";
        case panda_file::Type::TypeId::F32:       return "ani_float";
        case panda_file::Type::TypeId::F64:       return "ani_double";
        case panda_file::Type::TypeId::REFERENCE: return "ani_ref";
        default: UNREACHABLE();
    }
    // clang-format on
    UNREACHABLE();
}

// CC-OFFNXT(huge_method[C++]) solid logic
static void DoANIArgsAbort(PandaEtsVM *etsVm, std::string_view functionName, const ArgsInfo &argsInfo)
{
    struct MsgArgInfo {
        std::string_view name;
        PandaString value;
        PandaString type;
        PandaString error;
    };
    size_t maxNameSize = 0;
    size_t maxValueSize = 0;
    size_t maxTypeSize = 0;

    PandaVector<MsgArgInfo> msgArgInfoList;
    for (const ArgInfo &v : argsInfo.argInfoList) {
        PandaStringStream ssError;
        if (v.err) {
            ssError << "INVALID: " << v.err.value();
        } else {
            ssError << "VALID";
        }
        MsgArgInfo argInfo {v.arg.GetName(), v.arg.GetStringValue(), v.arg.GetStringType(), ssError.str()};
        if (argInfo.name.size() > maxNameSize) {
            maxNameSize = argInfo.name.size();
        }
        if (argInfo.value.size() > maxValueSize) {
            maxValueSize = argInfo.value.size();
        }
        if (argInfo.type.size() > maxTypeSize) {
            maxTypeSize = argInfo.type.size();
        }
        msgArgInfoList.emplace_back(argInfo);
    }
    if (argsInfo.extArgInfoList) {
        for (const ExtArgInfo &v : argsInfo.extArgInfoList.value()) {
            PandaStringStream ssError;
            if (v.isValid) {
                ssError << "VALID";
            } else {
                ssError << "INVALID: wrong value";
            }
            PandaStringStream ssValue;
            ssValue << "0x" << std::hex << v.value;
            MsgArgInfo argInfo {v.name, ssValue.str(), GetAniTypeByType(v.type), ssError.str()};
            if (argInfo.name.size() > maxNameSize) {
                maxNameSize = argInfo.name.size();
            }
            if (argInfo.value.size() > maxValueSize) {
                maxValueSize = argInfo.value.size();
            }
            if (argInfo.type.size() > maxTypeSize) {
                maxTypeSize = argInfo.type.size();
            }
            msgArgInfoList.emplace_back(argInfo);
        }
    }

    PandaStringStream ss;
    ss << std::setfill(' ');
    bool isFirst = true;
    for (auto &arg : msgArgInfoList) {
        std::string_view name = arg.name;
        PandaString &value = arg.value;
        PandaString &type = arg.type;
        PandaString &err = arg.error;
        if (isFirst) {
            isFirst = false;
        } else {
            ss << "\n";
        }
        ss << "    " << std::right << std::setw(maxNameSize) << name << ": " << std::setw(maxValueSize) << value
           << " | " << std::left << std::setw(maxTypeSize) << type << " | " << err;
    }

    DoAbortANI(etsVm, functionName, ss.str());
}

static PandaVector<ExtArgInfo> MakeExtArgInfoList(EnvANIVerifier *envANIVerifier, ANIArg::AniMethodArgs *methodArgs)
{
    if (methodArgs->method == nullptr || methodArgs->vargs == nullptr) {
        return {};
    }

    ASSERT(methodArgs != nullptr);
    auto getName = [](size_t index) -> PandaString {
        PandaStringStream ss;
        ss << '[' << index << ']';
        return ss.str();
    };

    PandaVector<ExtArgInfo> extArgInfoList;
    size_t i = 0;
    CallArgs callArgs(ToInternalMethod(methodArgs->method), methodArgs->vargs);

    callArgs.ForEachArgs([&](ani_value value, panda_file::Type type) -> bool {
        PandaString name = getName(i++);
        bool isValid = IsValidRawAniValue(envANIVerifier, value, type, methodArgs->isVaArgs);
        extArgInfoList.emplace_back(ExtArgInfo {std::move(name), value.l, type, isValid});
        return true;
    });
    return extArgInfoList;
}

// CC-OFFNXT(G.NAM.03) false positive
bool VerifyANIArgs(std::string_view functionName, std::initializer_list<ANIArg> args)
{
    VVm *vvm = VVm::GetInstance();
    VEnv *venv = VEnv::GetCurrent();

    bool success = true;
    PandaVector<ArgInfo> argInfoList;
    Verifier verifier(vvm, venv);
    for (const ANIArg &arg : args) {
        auto id = helpers::ToUnderlying(arg.GetAction());
        ASSERT(id < HANDLERS.size());
        CheckerHandler handler = HANDLERS[id];
        ASSERT(handler != nullptr);
        auto err = handler(verifier, arg);
        success &= !err.has_value();
        argInfoList.emplace_back(ArgInfo {arg, std::move(err)});
    }

    if (!success) {
        const ArgInfo &lastArgInfo = argInfoList.back();
        std::optional<PandaVector<ExtArgInfo>> methodArgInfoList {};
        auto action = lastArgInfo.arg.GetAction();
        if (action == ANIArg::Action::VERIFY_METHOD_V_ARGS || action == ANIArg::Action::VERIFY_METHOD_A_ARGS) {
            auto *methodArgs = lastArgInfo.arg.GetValueMethodArgs();
            PandaEnv *pandaEnv = PandaEnv::FromAniEnv(venv->GetEnv());
            methodArgInfoList = MakeExtArgInfoList(pandaEnv->GetEnvANIVerifier(), methodArgs);
        }
        ArgsInfo argsInfo {std::move(argInfoList), std::move(methodArgInfoList)};
        DoANIArgsAbort(PandaEtsVM::FromAniVM(vvm->GetVm()), functionName, argsInfo);
    }
    return success;
}

void VerifyAbortANI(std::string_view functionName, std::string_view message)
{
    PandaEtsVM *etsVm = PandaEtsVM::GetCurrent();
    DoAbortANI(etsVm, functionName, "    " + std::string(message));
}

}  // namespace ark::ets::ani::verify
