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

#include "unit_test.h"
#include <vector>
#include "optimizer/analysis/dominators_tree.h"

namespace panda::compiler {
class DomTreeTest : public GraphTest {
public:
    void CheckImmediateDominators(BasicBlock *dominator, const std::set<BasicBlock *> &&expected)
    {
        ASSERT_EQ(dominator->GetDominatedBlocks().size(), expected.size());

        for (auto block : dominator->GetDominatedBlocks()) {
            EXPECT_EQ(block->GetDominator(), dominator);
            EXPECT_TRUE(expected.find(block) != expected.end());
        }
    }

    void CheckImmediateDominatorsIdSet(int id_dom, std::vector<int> &&bb_ids)
    {
        std::set<BasicBlock *> bb_set;
        for (auto id : bb_ids) {
            bb_set.insert(&BB(id));
        }
        CheckImmediateDominators(&BB(id_dom), std::move(bb_set));
    }

    template <const bool CONDITION>
    void CheckListDominators(BasicBlock *dominator, const std::vector<BasicBlock *> &&expected)
    {
        for (auto dom : expected) {
            EXPECT_EQ(dominator->IsDominate(dom), CONDITION);
        }
    }
};

// NOLINTBEGIN(readability-magic-numbers)
TEST_F(DomTreeTest, OneBlock)
{
    GRAPH(GetGraph())
    {
        BASIC_BLOCK(2, -1)
        {
            INST(0, Opcode::ReturnVoid);
        };
    }

    auto block = GetGraph()->GetStartBlock();
    GetGraph()->RunPass<DominatorsTree>();

    EXPECT_TRUE(GetGraph()->IsAnalysisValid<DominatorsTree>());
    EXPECT_TRUE(block->IsDominate(block));
}
/*
 *                      [entry]
 *                         |
 *                         v
 *                /-------[2]-------\
 *                |                 |
 *                v                 v
 *               [3]               [4]
 *                |                 |
 *                |                 v
 *                |        /-------[5]-------\
 *                |        |                 |
 *                |        v                 v
 *                |       [6]               [7]
 *                |        |                 |
 *                |        v	             |
 *                \----->[exit]<-------------/
 *
 *  Dominators tree:
 *
 *                      [entry]
 *                         |
 *                         v
 *                        [2]
 *                  /      |      \
 *                 v       v       v
 *                [3]   [exit]    [4]
 *                                 |
 *                                 v
 *                                [5]
 *                             /       \
 *                            v         v
 *                           [6]       [7]
 *
 *
 */
TEST_F(DomTreeTest, GraphNoCycles)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 0);
        CONSTANT(1, 1);
        BASIC_BLOCK(2, 3, 4)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0, 1);
            INST(3, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(2);
        }
        BASIC_BLOCK(3, -1)
        {
            INST(4, Opcode::ReturnVoid);
        }
        BASIC_BLOCK(4, 5) {}
        BASIC_BLOCK(5, 6, 7)
        {
            INST(6, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0, 1);
            INST(7, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(6);
        }
        BASIC_BLOCK(6, -1)
        {
            INST(8, Opcode::ReturnVoid);
        }
        BASIC_BLOCK(7, -1)
        {
            INST(9, Opcode::ReturnVoid);
        }
    }

    auto entry = GetGraph()->GetStartBlock();
    auto a = &BB(2);
    auto b = &BB(3);
    auto c = &BB(4);
    auto d = &BB(5);
    auto e = &BB(6);
    auto f = &BB(7);
    auto exit = GetGraph()->GetEndBlock();

    // Check if DomTree is valid after building Dom tree
    GetGraph()->RunPass<DominatorsTree>();
    EXPECT_TRUE(GetGraph()->IsAnalysisValid<DominatorsTree>());

    // Check if DomTree is not valid after adding new block
    auto g = GetGraph()->CreateEmptyBlock();
    auto return_void = GetGraph()->CreateInstReturnVoid();
    g->AppendInst(return_void);
    auto cmp =
        GetGraph()->CreateInstCompare(DataType::BOOL, INVALID_PC, &INS(0), &INS(1), DataType::Type::INT64, CC_NE);
    c->AppendInst(cmp);
    auto if_inst = GetGraph()->CreateInstIfImm(DataType::NO_TYPE, INVALID_PC, cmp, 0, DataType::BOOL, CC_NE);
    c->AppendInst(if_inst);
    c->AddSucc(g);
    g->AddSucc(exit);
    GetGraph()->GetRootLoop()->AppendBlock(g);

    EXPECT_FALSE(GetGraph()->IsAnalysisValid<DominatorsTree>());
    GraphChecker(GetGraph()).Check();

    // Rebuild DomTree and checks dominators
    GetGraph()->RunPass<DominatorsTree>();
    CheckImmediateDominators(entry, {a});
    CheckImmediateDominators(a, {b, exit, c});
    CheckImmediateDominators(b, {});
    CheckImmediateDominators(c, {d, g});
    CheckImmediateDominators(d, {e, f});
    CheckImmediateDominators(e, {});
    CheckImmediateDominators(f, {});
    CheckImmediateDominators(g, {});
    CheckImmediateDominators(exit, {});

    CheckListDominators<true>(entry, {entry, a, b, c, d, e, f, g, exit});
    CheckListDominators<true>(a, {a, b, c, d, e, f, g, exit});
    CheckListDominators<true>(c, {c, d, e, f, g});
    CheckListDominators<true>(d, {d, e, f});

    CheckListDominators<false>(b, {entry, a, c, d, e, f, g, exit});
    CheckListDominators<false>(e, {entry, a, b, c, d, f, g, exit});
    CheckListDominators<false>(f, {entry, a, b, c, d, e, g, exit});
    CheckListDominators<false>(g, {entry, a, b, c, d, e, f, exit});
    CheckListDominators<false>(exit, {entry, a, b, c, d, e, f, g});
}

/*
 *                           [entry]
 *                              |
 *                              v
 *       /-------------------->[2]--------------\
 *       |                      |               |
 *       |                      |               v
 *       |                      |              [3]
 *       |                      v               |
 *       |                     [4]<-------------/
 *       |                      ^
 *       |                      |
 *       |                      v
 *       |                     [5]
 *       |                   /  ^
 *       |                  v   |
 *       |               [6]    |
 *       |                  \   |
 *       |                   \  |
 *       |                    v |
 *       |                     [8]
 *       |                      |
 *       |                      v
 *       |                     [9]
 *       |                   /     \
 *       |                  v       v
 *       \----------------[10]     [11]
 *                                  |
 *                                  |
 *                                  v
 *                                [exit]
 *
 *  Dominators tree:
 *                          [entry]
 *                             |
 *                             v
 *                            [2]
 *                         /       \
 *                        v         v
 *                       [3]       [4]
 *                                  |
 *                                 [5]
 *                                  |
 *                                  v
 *                                 [6]
 *                                  |
 *                                  v
 *                                 [9]
 *                              /       \
 *                             v         v
 *                            [10]      [11]
 *                                       |
 *                                       v
 *                                     [exit]
 */
TEST_F(DomTreeTest, GraphWithCycles)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 0);
        CONSTANT(1, 1);
        BASIC_BLOCK(2, 3, 4)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0, 1);
            INST(3, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(2);
        }
        BASIC_BLOCK(3, 4) {}
        BASIC_BLOCK(4, 5) {}
        BASIC_BLOCK(5, 6, 4)
        {
            INST(6, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0, 1);
            INST(7, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(6);
        }
        BASIC_BLOCK(6, 8) {}
        BASIC_BLOCK(8, 5, 9)
        {
            INST(9, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0, 1);
            INST(10, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(9);
        }
        BASIC_BLOCK(9, 10, 11)
        {
            INST(11, Opcode::Compare).b().SrcType(DataType::Type::INT64).Inputs(0, 1);
            INST(12, Opcode::IfImm).SrcType(DataType::BOOL).CC(CC_NE).Imm(0).Inputs(11);
        }
        BASIC_BLOCK(10, 2) {}
        BASIC_BLOCK(11, -1)
        {
            INST(14, Opcode::ReturnVoid);
        }
    }
    auto entry = GetGraph()->GetStartBlock();
    auto k = &BB(2);
    auto a = &BB(3);
    auto b = &BB(4);
    auto c = &BB(5);
    auto d = &BB(6);
    auto f = &BB(8);
    auto g = &BB(9);
    auto h = &BB(10);
    auto i = &BB(11);
    auto exit = GetGraph()->GetEndBlock();

    GraphChecker(GetGraph()).Check();

    // Check if DomTree is valid after building Dom tree
    GetGraph()->GetAnalysis<DominatorsTree>().SetValid(false);
    GetGraph()->RunPass<DominatorsTree>();
    EXPECT_TRUE(GetGraph()->IsAnalysisValid<DominatorsTree>());

    CheckImmediateDominators(GetGraph()->GetStartBlock(), {BB(2).GetLoop()->GetPreHeader()});
    CheckImmediateDominators(&BB(2), {&BB(3), BB(4).GetLoop()->GetPreHeader()});
    CheckImmediateDominatorsIdSet(3, {});
    CheckImmediateDominatorsIdSet(4, {5});
    CheckImmediateDominatorsIdSet(5, {6});
    CheckImmediateDominatorsIdSet(6, {8});
    CheckImmediateDominatorsIdSet(8, {9});
    CheckImmediateDominatorsIdSet(9, {10, 11});
    CheckImmediateDominatorsIdSet(10, {});
    CheckImmediateDominatorsIdSet(11, {IrConstructor::ID_EXIT_BB});

    CheckListDominators<true>(entry, {entry, k, a, b, c, d, f, g, h, i, exit});
    CheckListDominators<true>(k, {k, a, b, c, d, f, g, h, i, exit});
    CheckListDominators<true>(b, {b, c, d, f, g, h, i, exit});
    CheckListDominators<true>(c, {c, d, f, g, h, i, exit});
    CheckListDominators<true>(f, {f, g, h, i, exit});
    CheckListDominators<true>(g, {h, i, exit});

    CheckListDominators<false>(a, {entry, b, c, d, f, g, h, i, exit});
    CheckListDominators<false>(d, {entry, a, b, c});
    CheckListDominators<false>(h, {entry, a, b, c, d, f, g, i, exit});
    CheckListDominators<false>(i, {entry, a, b, c, d, f, g, h});
}
// NOLINTEND(readability-magic-numbers)

}  // namespace panda::compiler
