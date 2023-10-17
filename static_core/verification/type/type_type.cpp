/**
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#include "verification/type/type_type.h"

#include "verification/type/type_system.h"
#include "verification/util/mem.h"

namespace panda::verifier {
using Builtin = Type::Builtin;

// NOLINTBEGIN(fuchsia-statically-constructed-objects,readability-identifier-naming)
namespace {

std::array<std::string, Builtin::LAST> builtin_names = {
    "*undefined*", "top",        "u1",      "i8",     "u8",     "i16",       "u16",        "i32",
    "u32",         "f32",        "f64",     "i64",    "u64",    "integral8", "integral16", "integral32",
    "integral64",  "float32",    "float64", "bits32", "bits64", "primitive", "reference",  "null_reference",
    "object",      "type_class", "array",   "bot"};

constexpr uint32_t Bitmap(std::initializer_list<Builtin> vs)
{
    uint32_t result = 0;
    for (auto v : vs) {
        result |= 1U << v;
    }
    return result;
}

template <size_t SZ>
std::array<uint32_t, SZ> TransitiveClosure(std::array<uint32_t, SZ> const *source)
{
    std::array<uint32_t, SZ> result = *source;
    for (size_t i = 0; i < SZ; i++) {
        result[i] |= Bitmap({Builtin {i}});
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < SZ; i++) {
            uint32_t pre = result[i];
            uint32_t post = pre;
            for (size_t j = 0; j < SZ; j++) {
                if ((post & Bitmap({Builtin {j}})) != 0) {
                    post |= result[j];
                }
            }
            if (post != pre) {
                result[i] = post;
                changed = true;
            }
        }
    }
    return result;
}

std::array<uint32_t, Builtin::LAST> builtin_supertypes_nontrans = {
    /* undefined */ 0,
    /* top */ 0,
    /* u1 */ Bitmap({Builtin::U8, Builtin::I8}),
    /* i8 */ Bitmap({Builtin::I16, Builtin::INTEGRAL8}),
    /* u8 */ Bitmap({Builtin::U16, Builtin::I16, Builtin::INTEGRAL8}),
    /* i16 */ Bitmap({Builtin::I32, Builtin::INTEGRAL16}),
    /* u16 */ Bitmap({Builtin::I32, Builtin::U32, Builtin::INTEGRAL16}),
    /* i32 */ Bitmap({Builtin::INTEGRAL32}),
    /* u32 */ Bitmap({Builtin::INTEGRAL32}),
    /* f32 */ Bitmap({Builtin::FLOAT32}),
    /* f64 */ Bitmap({Builtin::FLOAT64}),
    /* i64 */ Bitmap({Builtin::INTEGRAL64}),
    /* u64 */ Bitmap({Builtin::INTEGRAL64}),
    /* integral8 */ Bitmap({Builtin::INTEGRAL16}),
    /* integral16 */ Bitmap({Builtin::INTEGRAL32}),
    /* integral32 */ Bitmap({Builtin::BITS32}),
    /* integral64 */ Bitmap({Builtin::BITS64}),
    /* float32 */ Bitmap({Builtin::BITS32}),
    /* float64 */ Bitmap({Builtin::BITS64}),
    /* bits32 */ Bitmap({Builtin::PRIMITIVE}),
    /* bits64 */ Bitmap({Builtin::PRIMITIVE}),
    /* primitive */ Bitmap({Builtin::TOP}),
    /* reference */ Bitmap({Builtin::TOP}),
    /* null_reference */ Bitmap({Builtin::REFERENCE, Builtin::OBJECT, Builtin::TYPE_CLASS, Builtin::ARRAY}),
    /* object */ Bitmap({Builtin::REFERENCE}),
    /* type_class */ Bitmap({Builtin::REFERENCE}),
    /* array */ Bitmap({Builtin::OBJECT}),
    /* bot */
    Bitmap({Builtin::U1, Builtin::I8, Builtin::U8, Builtin::I16, Builtin::U16, Builtin::I32, Builtin::U32, Builtin::F32,
            Builtin::F64, Builtin::I64, Builtin::U64, Builtin::NULL_REFERENCE, Builtin::OBJECT, Builtin::ARRAY})};

std::array<uint32_t, Builtin::LAST> builtin_supertypes = TransitiveClosure(&builtin_supertypes_nontrans);

std::array<std::array<Builtin, Builtin::LAST>, Builtin::LAST> BuildBuiltinLeastUpperBounds(
    std::array<uint32_t, Builtin::LAST> const &supertypes)
{
    std::array<std::array<Builtin, Builtin::LAST>, Builtin::LAST> result {};
    // Ignore ununsed index 0
    for (size_t lhs = 1; lhs < Builtin::LAST; lhs++) {
        for (size_t rhs = 1; rhs < lhs; rhs++) {
            Builtin candidate = Builtin::TOP;
            uint32_t supertype_bits = supertypes[lhs] & supertypes[rhs];
            for (size_t cc = 1; cc < Builtin::LAST; cc++) {
                if ((supertype_bits & Bitmap({Builtin {cc}})) != 0 && (supertypes[cc] & Bitmap({candidate})) != 0) {
                    candidate = Builtin {cc};
                }
            }
            result[lhs][rhs] = candidate;
            result[rhs][lhs] = candidate;
        }
        result[lhs][lhs] = Builtin {lhs};
    }
    return result;
}

std::array<std::array<Builtin, Builtin::LAST>, Builtin::LAST> builtin_lub =
    BuildBuiltinLeastUpperBounds(builtin_supertypes);

using ClassSubtypingCheckFun = bool (*)(Class const *);

bool AlwaysTrue([[maybe_unused]] Class const *_)
{
    return true;
}

bool IsClassArray(Class const *klass)
{
    return klass->IsArrayClass();
}

bool IsClassClass(Class const *klass)
{
    return klass->IsClassClass();
}

struct ClassSubtypingFuns {
    ClassSubtypingCheckFun is_builtin_supertype_of_class;
    ClassSubtypingCheckFun is_class_supertype_of_builtin;
};

std::array<ClassSubtypingFuns, Builtin::LAST> class_subtyping_funs {
    /* undefined */ ClassSubtypingFuns {nullptr, nullptr},
    /* top */ ClassSubtypingFuns {AlwaysTrue, nullptr},
    /* u1 */ ClassSubtypingFuns {nullptr, nullptr},
    /* i8 */ ClassSubtypingFuns {nullptr, nullptr},
    /* u8 */ ClassSubtypingFuns {nullptr, nullptr},
    /* i16 */ ClassSubtypingFuns {nullptr, nullptr},
    /* u16 */ ClassSubtypingFuns {nullptr, nullptr},
    /* i32 */ ClassSubtypingFuns {nullptr, nullptr},
    /* u32 */ ClassSubtypingFuns {nullptr, nullptr},
    /* f32 */ ClassSubtypingFuns {nullptr, nullptr},
    /* f64 */ ClassSubtypingFuns {nullptr, nullptr},
    /* i64 */ ClassSubtypingFuns {nullptr, nullptr},
    /* u64 */ ClassSubtypingFuns {nullptr, nullptr},
    /* integral8 */ ClassSubtypingFuns {nullptr, nullptr},
    /* integral16 */ ClassSubtypingFuns {nullptr, nullptr},
    /* integral32 */ ClassSubtypingFuns {nullptr, nullptr},
    /* integral64 */ ClassSubtypingFuns {nullptr, nullptr},
    /* float32 */ ClassSubtypingFuns {nullptr, nullptr},
    /* float64 */ ClassSubtypingFuns {nullptr, nullptr},
    /* bits32 */ ClassSubtypingFuns {nullptr, nullptr},
    /* bits64 */ ClassSubtypingFuns {nullptr, nullptr},
    /* primitive */ ClassSubtypingFuns {nullptr, nullptr},
    /* reference */ ClassSubtypingFuns {AlwaysTrue, nullptr},
    /* null_reference */ ClassSubtypingFuns {nullptr, AlwaysTrue},
    /* object */ ClassSubtypingFuns {AlwaysTrue, nullptr},
    /* type_class */ ClassSubtypingFuns {IsClassClass, nullptr},
    /* array */ ClassSubtypingFuns {IsClassArray, nullptr},
    /* bot */ ClassSubtypingFuns {nullptr, AlwaysTrue}};

[[maybe_unused]] bool InvariantHolds(Type tp, TypeSystem const *tsys)
{
    if (tp.IsBuiltin() || tp.IsClass()) {
        return true;
    }
    if (tp.IsIntersection()) {
        auto members = tp.GetIntersectionMembers(tsys);
        if (members.size() < 2) {
            return false;
        }
        for (auto mtp : members) {
            if (mtp.IsIntersection() || mtp.IsUnion() || mtp == Type {Builtin::BOT} || mtp == Type {Builtin::TOP}) {
                return false;
            }
        }
        return true;
    }
    if (tp.IsUnion()) {
        auto members = tp.GetUnionMembers(tsys);
        if (members.size() < 2) {
            return false;
        }
        for (auto mtp : members) {
            if (mtp.IsUnion() || mtp == Type {Builtin::TOP} || mtp == Type {Builtin::BOT}) {
                return false;
            }
        }
        return true;
    }
    UNREACHABLE();
}

}  // namespace
// NOLINTEND(fuchsia-statically-constructed-objects,readability-identifier-naming)

// Careful: span is invalidated whenever a new intersection or union is created.
Span<Type const> Type::GetIntersectionMembers(TypeSystem const *tsys) const
{
    ASSERT(IsIntersection());
    uintptr_t payload = GetPayload(content_);
    return tsys->GetTypeSpan(SpanIndex(payload), SpanSize(payload));
}

// Careful: span is invalidated whenever a new intersection or union is created.
Span<Type const> Type::GetUnionMembers(TypeSystem const *tsys) const
{
    ASSERT(IsUnion());
    uintptr_t payload = GetPayload(content_);
    return tsys->GetTypeSpan(SpanIndex(payload), SpanSize(payload));
}

bool Type::IsConsistent() const
{
    if (IsNone()) {
        return false;
    }
    if (IsBuiltin()) {
        return GetBuiltin() != Builtin::TOP;
    }
    return true;
}

PandaString Type::ToString(TypeSystem const *tsys) const
{
    if (IsNone()) {
        return "<none>";
    }
    if (IsBuiltin()) {
        return PandaString(builtin_names[GetBuiltin()]);
    }
    if (IsClass()) {
        auto const *klass = GetClass();
        if (klass->IsArrayClass()) {
            ASSERT(klass->GetComponentType() != nullptr);
            return (Type {klass->GetComponentType()}).ToString(tsys) + "[]";
        }
        return PandaString {klass->GetName()};
    }
    if (IsIntersection()) {
        auto members = GetIntersectionMembers(tsys);
        ASSERT(!members.empty());
        std::stringstream ss;
        ss << "(";
        bool first = true;
        for (auto t : members) {
            if (!first) {
                ss << " & ";
            } else {
                first = false;
            }
            ss << t.ToString(tsys);
        }
        ss << ")";
        return PandaString(ss.str());
    }
    if (IsUnion()) {
        auto members = GetUnionMembers(tsys);
        ASSERT(!members.empty());
        std::stringstream ss;
        ss << "(";
        bool first = true;
        for (auto t : members) {
            if (!first) {
                ss << " | ";
            } else {
                first = false;
            }
            ss << t.ToString(tsys);
        }
        ss << ")";
        return PandaString(ss.str());
    }
    return "<unexpected kind of AbstractType>";
}

panda_file::Type::TypeId Type::ToTypeId() const
{
    using TypeId = panda_file::Type::TypeId;
    if (!IsBuiltin()) {
        return TypeId::INVALID;
    }
    switch (GetBuiltin()) {
        case Builtin::TOP:
            return TypeId::VOID;
        case Builtin::U1:
            return TypeId::U1;
        case Builtin::I8:
            return TypeId::I8;
        case Builtin::U8:
            return TypeId::U8;
        case Builtin::I16:
            return TypeId::I16;
        case Builtin::U16:
            return TypeId::U16;
        case Builtin::I32:
            return TypeId::I32;
        case Builtin::U32:
            return TypeId::U32;
        case Builtin::F32:
            return TypeId::F32;
        case Builtin::F64:
            return TypeId::F64;
        case Builtin::I64:
            return TypeId::I64;
        case Builtin::U64:
            return TypeId::U64;
        default:
            return TypeId::INVALID;
    }
}

size_t Type::GetTypeWidth() const
{
    using TypeId = panda_file::Type::TypeId;
    auto type_id = ToTypeId();
    if (type_id != TypeId::INVALID) {
        return panda_file::Type(type_id).GetBitWidth();
    }
    switch (GetBuiltin()) {
        case Builtin::INTEGRAL8:
            return coretypes::INT8_BITS;
        case Builtin::INTEGRAL16:
            return coretypes::INT16_BITS;
        case Builtin::INTEGRAL32:
        case Builtin::FLOAT32:
        case Builtin::BITS32:
            return coretypes::INT32_BITS;
        case Builtin::INTEGRAL64:
        case Builtin::FLOAT64:
        case Builtin::BITS64:
            return coretypes::INT64_BITS;
        case Builtin::REFERENCE:
        case Builtin::NULL_REFERENCE:
            return 0;
        default:
            UNREACHABLE();
    }
}

Type Type::FromTypeId(panda_file::Type::TypeId tid)
{
    using TypeId = panda_file::Type::TypeId;
    switch (tid) {
        case TypeId::VOID:
            return Type {Builtin::TOP};
        case TypeId::U1:
            return Type {Builtin::U1};
        case TypeId::I8:
            return Type {Builtin::I8};
        case TypeId::U8:
            return Type {Builtin::U8};
        case TypeId::I16:
            return Type {Builtin::I16};
        case TypeId::U16:
            return Type {Builtin::U16};
        case TypeId::I32:
            return Type {Builtin::I32};
        case TypeId::U32:
            return Type {Builtin::U32};
        case TypeId::F32:
            return Type {Builtin::F32};
        case TypeId::F64:
            return Type {Builtin::F64};
        case TypeId::I64:
            return Type {Builtin::I64};
        case TypeId::U64:
            return Type {Builtin::U64};
        case TypeId::REFERENCE:
            return Type {Builtin::REFERENCE};
        default:
            UNREACHABLE();
    }
}

static Span<Type> CopySpanToTypeSystem(Span<Type> span, TypeSystem *tsys)
{
    Span<Type> dest = tsys->GetNewTypeSpan(span.size());
    std::copy(span.begin(), span.end(), dest.begin());
    return dest;
}

/* static */
Type Type::Intersection(Span<Type> span, TypeSystem *tsys)
{
    Span<Type> rspan = CopySpanToTypeSystem(span, tsys);
    return Type {ConstructWithTag(INTERSECTION_TAG, ConstructPayload(rspan.size(), tsys->GetSpanIndex(rspan)))};
}

/* static */
Type Type::Union(Span<Type> span, TypeSystem *tsys)
{
    Span<Type> rspan = CopySpanToTypeSystem(span, tsys);
    return Type {ConstructWithTag(UNION_TAG, ConstructPayload(rspan.size(), tsys->GetSpanIndex(rspan)))};
}

static bool IsClassSubtypeOfType(Class const *lhs_class, Type rhs, TypeSystem *tsys)
{
    // Deal with arrays; those are covariant
    {
        auto *lhs_component = lhs_class->GetComponentType();
        if (lhs_component != nullptr) {
            if (!rhs.IsClass() || rhs.GetClass()->GetComponentType() == nullptr) {
                Type supertype_of_array = tsys->SupertypeOfArray();
                return supertype_of_array.IsConsistent() && IsSubtype(supertype_of_array, rhs, tsys);
            }
            auto *rhs_component = rhs.GetClass()->GetComponentType();
            auto rhs_component_type = Type {rhs_component};
            return IsClassSubtypeOfType(lhs_component, rhs_component_type, tsys);
        }
    }

    return tsys->SupertypesOfClass(lhs_class)->count(rhs) > 0;
}

bool IsSubtypeImpl(Type lhs, Type rhs, TypeSystem *tsys)
{
    ASSERT(InvariantHolds(lhs, tsys));
    ASSERT(InvariantHolds(rhs, tsys));
    if (lhs.IsBuiltin() && rhs.IsBuiltin()) {
        auto lhs_builtin = lhs.GetBuiltin();
        auto rhs_builtin = rhs.GetBuiltin();
        return (builtin_supertypes[lhs_builtin] & Bitmap({rhs_builtin})) != 0;
    }
    if (lhs.IsBuiltin() && rhs.IsClass()) {
        auto lhs_builtin = lhs.GetBuiltin();
        auto rhs_class = rhs.GetClass();
        auto *checker = class_subtyping_funs[lhs_builtin].is_class_supertype_of_builtin;
        return checker != nullptr && checker(rhs_class);
    }
    if (lhs.IsClass() && rhs.IsBuiltin()) {
        auto const *lhs_class = lhs.GetClass();
        auto rhs_builtin = rhs.GetBuiltin();
        auto *checker = class_subtyping_funs[rhs_builtin].is_builtin_supertype_of_class;
        return checker != nullptr && checker(lhs_class);
    }
    if (lhs.IsClass() && rhs.IsClass()) {
        return IsClassSubtypeOfType(lhs.GetClass(), rhs, tsys);
    }
    if ((lhs.IsBuiltin() || lhs.IsClass()) && rhs.IsIntersection()) {
        for (auto e : rhs.GetIntersectionMembers(tsys)) {
            if (!IsSubtype(lhs, e, tsys)) {
                return false;
            }
        }
        return true;
    }
    if (lhs.IsIntersection() && (rhs.IsBuiltin() || rhs.IsClass())) {
        for (auto e : lhs.GetIntersectionMembers(tsys)) {
            if (IsSubtype(e, rhs, tsys)) {
                return true;
            }
        }
        return false;
    }
    if ((lhs.IsBuiltin() || lhs.IsClass()) && rhs.IsUnion()) {
        for (auto e : rhs.GetUnionMembers(tsys)) {
            if (IsSubtype(lhs, e, tsys)) {
                return true;
            }
        }
        return false;
    }
    if (lhs.IsUnion() && (rhs.IsBuiltin() || rhs.IsClass())) {
        for (auto e : lhs.GetUnionMembers(tsys)) {
            if (!IsSubtype(e, rhs, tsys)) {
                return false;
            }
        }
        return true;
    }
    ASSERT(lhs.IsUnion() || lhs.IsIntersection());
    ASSERT(rhs.IsUnion() || rhs.IsIntersection());
    if (rhs.IsIntersection()) {
        for (auto e : rhs.GetIntersectionMembers(tsys)) {
            if (!IsSubtype(lhs, e, tsys)) {
                return false;
            }
        }
        return true;
    }
    if (lhs.IsUnion()) {
        for (auto e : lhs.GetUnionMembers(tsys)) {
            if (!IsSubtype(e, rhs, tsys)) {
                return false;
            }
        }
        return true;
    }
    ASSERT(lhs.IsIntersection() && rhs.IsUnion());
    for (auto e : rhs.GetUnionMembers(tsys)) {
        if (IsSubtype(lhs, e, tsys)) {
            return true;
        }
    }
    return false;
}

static bool IsIntersectionReasonable(Span<Type> members)
{
    // // We know that none of the members is a subtype of any other.
    // bool have_builtins = false;
    // bool have_classes = false;

    // // Nothing is a subclass of both a class and a builtin (except NULL_POINTER and BOT).
    // for (auto const &mtp : *members) {
    //     if (mtp.IsBuiltin()) {
    //         have_builtins = true;
    //         if (have_classes) {
    //             return false;
    //         }
    //     } else {
    //         ASSERT(mtp.IsClass());
    //         have_classes = true;
    //         if (have_builtins) {
    //             return false;
    //         }
    //     }
    // }

    // Java specific?
    bool have_class = false;
    for (auto const &mtp : members) {
        if (mtp.IsClass()) {
            Class const *cls = mtp.GetClass();
            if (cls->IsFinal()) {
                // no meaningful intersection possible
                return false;
            }
            if (!cls->IsInterface()) {
                if (have_class) {
                    // no memaningful intersection between classes,
                    // other than subtyping
                    return false;
                }
                have_class = true;
            }
        }
    }
    return true;
}

static Span<Type const> ToIntersectionSpan(Type const *tp, TypeSystem const *tsys)
{
    if (tp->IsBuiltin() || tp->IsClass()) {
        return Span {tp, 1};
    }
    if (tp->IsIntersection()) {
        return tp->GetIntersectionMembers(tsys);
    }
    UNREACHABLE();
}

static Span<Type const> ToUnionSpan(Type const *tp, TypeSystem const *tsys)
{
    if (tp->IsBuiltin() || tp->IsClass() || tp->IsIntersection()) {
        return Span {tp, 1};
    }
    if (tp->IsUnion()) {
        return tp->GetUnionMembers(tsys);
    }
    UNREACHABLE();
}

Type Type::IntersectSpans(Span<Type const> lhs, Span<Type const> rhs, TypeSystem *tsys)
{
    PandaVector<Type> res;

    for (auto ltp : lhs) {
        bool to_include = true;
        for (auto rtp : rhs) {
            if (IsSubtype(rtp, ltp, tsys) && ltp != rtp) {
                to_include = false;
                break;
            }
        }
        if (to_include) {
            res.push_back(ltp);
        }
    }
    for (auto rtp : rhs) {
        bool to_include = true;
        for (auto ltp : lhs) {
            if (IsSubtype(ltp, rtp, tsys)) {
                to_include = false;
                break;
            }
        }
        if (to_include) {
            res.push_back(rtp);
        }
    }
    ASSERT(res.size() > 1);  // otherwise would mean lhs <: rhs or rhs <: lhs
    if (IsIntersectionReasonable(Span {res})) {
        return Intersection(Span {res}, tsys);
    }
    return Type {Builtin::BOT};
}

Type TpIntersection(Type lhs, Type rhs, TypeSystem *tsys)
{
    ASSERT(InvariantHolds(lhs, tsys));
    ASSERT(InvariantHolds(rhs, tsys));
    if (IsSubtype(lhs, rhs, tsys)) {
        return lhs;
    }
    if (IsSubtype(rhs, lhs, tsys)) {
        return rhs;
    }
    if (lhs.IsBuiltin() && rhs.IsBuiltin()) {
        // No nontrivial intersections among builtins
        return Type {Builtin::BOT};
    }
    if ((lhs.IsClass() || lhs.IsBuiltin()) && (rhs.IsClass() || lhs.IsBuiltin())) {
        PandaVector<Type> res {lhs, rhs};
        if (IsIntersectionReasonable(Span {res})) {
            return Type::Intersection(Span {res}, tsys);
        }
        return Type {Builtin::BOT};
    }

    // Now at least one of the sides is at least an intersection
    if (!lhs.IsUnion() && !rhs.IsUnion()) {
        // they are also at most intersections.
        // Spans are only invalidated before return form IntersectSpans.
        return Type::IntersectSpans(ToIntersectionSpan(&lhs, tsys), ToIntersectionSpan(&rhs, tsys), tsys);
    }

    // At least one of the sides is a union.
    // Spans will be invalidated in the course of building the intersection, so we need to copy them.
    PandaUnorderedSet<Type> union_res;
    auto lhs_span = ToUnionSpan(&lhs, tsys);
    auto rhs_span = ToUnionSpan(&rhs, tsys);
    PandaVector<Type> lhs_vec {lhs_span.begin(), lhs_span.end()};
    PandaVector<Type> rhs_vec {rhs_span.begin(), rhs_span.end()};

    for (auto m_lhs : lhs_vec) {
        for (auto m_rhs : rhs_vec) {
            Type alt = TpIntersection(m_lhs, m_rhs, tsys);
            if (alt != Type {Type::Builtin::BOT}) {
                union_res.insert(alt);
            }
        }
    }
    if (union_res.empty()) {
        return Type {Type::Builtin::BOT};
    }
    if (union_res.size() == 1) {
        return *union_res.cbegin();
    }
    PandaVector<Type> union_res_vec;
    for (auto &m : union_res) {
        union_res_vec.push_back(m);
    }
    return Type::Union(Span {union_res_vec}, tsys);
}

Type TpUnion(Type lhs, Type rhs, TypeSystem *tsys)
{
    // return lhs for lhs==rhs case
    if (IsSubtype(rhs, lhs, tsys)) {
        return lhs;
    }
    if (IsSubtype(lhs, rhs, tsys)) {
        return rhs;
    }
    if (lhs.IsBuiltin() && rhs.IsBuiltin()) {
        return Type {builtin_lub[lhs.GetBuiltin()][rhs.GetBuiltin()]};
    }
    PandaVector<Type> union_res;
    auto lhs_span = ToUnionSpan(&lhs, tsys);
    auto rhs_span = ToUnionSpan(&rhs, tsys);
    for (auto m_lhs : lhs_span) {
        bool to_include = true;
        for (auto m_rhs : rhs_span) {
            if (IsSubtype(m_lhs, m_rhs, tsys) && m_lhs != m_rhs) {
                to_include = false;
                break;
            }
        }
        if (to_include) {
            union_res.push_back(m_lhs);
        }
    }
    for (auto m_rhs : rhs_span) {
        bool to_include = true;
        for (auto m_lhs : lhs_span) {
            if (IsSubtype(m_rhs, m_lhs, tsys)) {
                to_include = false;
                break;
            }
        }
        if (to_include) {
            union_res.push_back(m_rhs);
        }
    }
    ASSERT(union_res.size() > 1);
    return Type::Union(Span {union_res}, tsys);
}

Type Type::GetArrayElementType(TypeSystem *tsys) const
{
    if (IsClass()) {
        Class const *klass = GetClass();
        if (klass->IsArrayClass()) {
            auto *array_component = klass->GetComponentType();
            ASSERT(array_component != nullptr);
            return Type {array_component};
        }
        return Top();
    }
    if (IsIntersection()) {
        auto members = GetIntersectionMembers(tsys);
        PandaVector<Type> vec;
        for (auto m : members) {
            vec.push_back(m.GetArrayElementType(tsys));
            if (vec.back() == Top()) {
                return Top();
            }
        }
        // Reasonableness should already be encoded in reasonableness of members.
        ASSERT(IsIntersectionReasonable(Span {vec}));
        return Intersection(Span {vec}, tsys);
    }
    if (IsUnion()) {
        // GetArrayElementType may invalidate span, so copy it.
        auto members_span = GetUnionMembers(tsys);
        PandaVector<Type> members {members_span.begin(), members_span.end()};
        PandaVector<Type> vec;
        for (auto m : members) {
            vec.push_back(m.GetArrayElementType(tsys));
            if (vec.back() == Top()) {
                return Top();
            }
        }
        return Union(Span {vec}, tsys);
    }
    return Top();
}
}  // namespace panda::verifier
