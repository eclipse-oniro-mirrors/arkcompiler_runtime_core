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

#ifndef COMPILER_OPTIMIZER_ANALYSIS_DOMINATORS_TREE_H_
#define COMPILER_OPTIMIZER_ANALYSIS_DOMINATORS_TREE_H_

#include "optimizer/pass.h"
#include "utils/arena_containers.h"

namespace panda::compiler {
class BasicBlock;
class Graph;

/// This class builds dominators tree, using Lengauer-Tarjan algorithm
class DominatorsTree : public Analysis {
public:
    using BlocksVector = ArenaVector<BasicBlock *>;

    explicit DominatorsTree(Graph *graph);

    NO_MOVE_SEMANTIC(DominatorsTree);
    NO_COPY_SEMANTIC(DominatorsTree);
    ~DominatorsTree() override = default;

    bool RunImpl() override;

    const char *GetPassName() const override
    {
        return "DominatorTree";
    }

    static void SetDomPair(BasicBlock *dominator, BasicBlock *block);

    void UpdateAfterResolverInsertion(BasicBlock *predecessor, BasicBlock *successor, BasicBlock *resolver);

private:
    /*
     * The ancestor of 'block', nullptr for the tree root
     */
    void SetAncestor(BasicBlock *dest, BasicBlock *block)
    {
        (*ancestors_)[GetBlockId(dest)] = block;
    }
    BasicBlock *GetAncestor(BasicBlock *block) const
    {
        return (*ancestors_)[GetBlockId(block)];
    }

    /*
     * A set of blocks whose semidominator is 'block'
     */
    BlocksVector &GetBucket(BasicBlock *block)
    {
        return (*buckets_)[GetBlockId(block)];
    }

    /*
     * The immediate dominator of 'block'
     */
    void SetIdom(BasicBlock *dest, BasicBlock *block)
    {
        (*idoms_)[GetBlockId(dest)] = block;
    }
    BasicBlock *GetIdom(BasicBlock *block) const
    {
        return (*idoms_)[GetBlockId(block)];
    }

    /*
     * The block in the ancestors chain with the minimal semidominator DFS-number for 'block'
     */
    void SetLabel(BasicBlock *dest, BasicBlock *block)
    {
        (*labels_)[GetBlockId(dest)] = block;
    }
    BasicBlock *GetLabel(BasicBlock *block) const
    {
        return (*labels_)[GetBlockId(block)];
    }

    /*
     * The parent of 'block' in the spanning tree generated by DFS
     */
    void SetParent(BasicBlock *dest, BasicBlock *block)
    {
        (*parents_)[GetBlockId(dest)] = block;
    }
    BasicBlock *GetParent(BasicBlock *block) const
    {
        return (*parents_)[GetBlockId(block)];
    }

    /*
     * The DFS-number of the semidominator of 'block'
     */
    void SetSemi(BasicBlock *dest, int32_t value)
    {
        (*semi_)[GetBlockId(dest)] = value;
    }
    int32_t GetSemi(BasicBlock *block) const
    {
        return (*semi_)[GetBlockId(block)];
    }

    /*
     * The block whose DFS-number is 'index'
     */
    void SetVertex(size_t index, BasicBlock *block)
    {
        (*vertices_)[index] = block;
    }
    BasicBlock *GetVertex(size_t index) const
    {
        return (*vertices_)[index];
    }

    void AdjustImmediateDominators(BasicBlock *block);
    void ComputeImmediateDominators(BasicBlock *block);
    void Compress(BasicBlock *block);
    void DfsNumbering(BasicBlock *block);
    BasicBlock *Eval(BasicBlock *block);
    void Init(size_t blocks_count);
    void Link(BasicBlock *parent, BasicBlock *block)
    {
        SetAncestor(block, parent);
    }
    static uint32_t GetBlockId(BasicBlock *block);

private:
    static constexpr int32_t DEFAULT_DFS_VAL = -1;
    // number of the block according to the order it is reached during the DFS
    int32_t dfs_num_ {DEFAULT_DFS_VAL};
    BlocksVector *ancestors_ {nullptr};
    ArenaVector<BlocksVector> *buckets_ {nullptr};
    BlocksVector *idoms_ {nullptr};
    BlocksVector *labels_ {nullptr};
    BlocksVector *parents_ {nullptr};
    ArenaVector<int32_t> *semi_ {nullptr};
    BlocksVector *vertices_ {nullptr};
};
}  // namespace panda::compiler

#endif  // COMPILER_OPTIMIZER_ANALYSIS_DOMINATORS_TREE_H_
