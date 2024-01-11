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

#ifndef PANDA_VERIFIER_DEBUG_LITERAL_PARSER_H_
#define PANDA_VERIFIER_DEBUG_LITERAL_PARSER_H_

#include "runtime/include/mem/panda_containers.h"
#include "runtime/include/mem/panda_string.h"
#include "verification/util/parser/parser.h"

#include <string>

namespace panda::verifier::debug {

template <typename Parser, typename Handler>
const auto &LiteralParser(Handler &handler)
{
    using panda::parser::Action;
    using panda::parser::Charset;
    // using panda::parser::Parser;

    struct Literal;

    using P = typename Parser::template Next<Literal>;

    // NOLINTNEXTLINE(readability-identifier-naming)
    static const auto literalNameHandler = [&handler](Action a, typename P::Ctx &c, auto from, auto to) {
        if (a == Action::PARSED) {
            return handler(c, PandaString {from, to});
        }
        return true;
    };
    // NOLINTNEXTLINE(readability-identifier-naming)
    static const auto literalName = P::OfCharset(Charset {"abcdefghijklmnopqrstuvwxyz_-"}) |= literalNameHandler;
    return literalName;
}

inline const auto &LiteralsParser()
{
    using panda::parser::Action;
    using panda::parser::Charset;
    using panda::parser::Parser;

    struct Literals;

    using Context = PandaVector<PandaString>;

    using P = typename Parser<Context, const char, const char *>::template Next<Literals>;
    using P1 = typename P::P;
    using P2 = typename P1::P;

    static const auto WS = P::OfCharset(" \t");
    static const auto COMMA = P1::OfCharset(",");

    static const auto LITERAL_HANDLER = [](Context &c, PandaString &&str) {
        c.emplace_back(std::move(str));
        return true;
    };

    static const auto LITERALS = ~WS >> *(~WS >> LiteralParser<P2>(LITERAL_HANDLER) >> ~WS >> ~COMMA) >> P::End();

    return LITERALS;
}

}  // namespace panda::verifier::debug

#endif  // !PANDA_VERIFIER_DEBUG_LITERAL_PARSER_H_
