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

#ifndef PANDA_VERIFIER_UTIL_MISC_HPP
#define PANDA_VERIFIER_UTIL_MISC_HPP

#include "libpandabase/utils/hash.h"

#include <functional>
#include <cstddef>
#include <tuple>

namespace panda::verifier {

template <typename T>
size_t StdHash(const T &x)
{
    return std::hash<T> {}(x);
}

}  // namespace panda::verifier

namespace std {

template <typename T1, typename T2>
struct hash<std::pair<T1, T2>> {
    size_t operator()(const std::pair<T1, T2> &pair) const
    {
        return panda::MergeHashes(panda::verifier::StdHash(pair.first),
                                  panda::verifier::StdHash(pair.second));  // NOLINT
    }
};

template <typename... T>
struct hash<std::tuple<T...>> {
    template <size_t N>
    size_t Helper(size_t tmpHash, const std::tuple<T...> &tuple) const
    {
        if constexpr (N < sizeof...(T)) {
            size_t tmp_hash1 = panda::MergeHashes(tmpHash, panda::verifier::StdHash(std::get<N>(tuple)));  // NOLINT
            return Helper<N + 1>(tmp_hash1, tuple);
        }

        return tmpHash;
    }

    size_t operator()(const std::tuple<T...> &tuple) const
    {
        return Helper<1>(panda::verifier::StdHash(std::get<0>(tuple)), tuple);
    }
};

}  // namespace std

#endif  // !PANDA_VERIFIER_UTIL_MISC_HPP
