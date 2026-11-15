/**
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#include "core/index/file_range_locator.h"

#include <algorithm>
#include <set>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

struct Candidate {
    uint64_t begin {0U};
    uint64_t end {0U};
    FileRangeMatch match;
};

struct Event {
    uint64_t offset {0U};
    size_t candidateIndex {0U};
    bool isStart {false};
};

bool CandidateRanksBefore(const Candidate &lhs, const Candidate &rhs)
{
    if (lhs.match.range.size != rhs.match.range.size) {
        return lhs.match.range.size < rhs.match.range.size;
    }
    if (lhs.match.source != rhs.match.source) {
        return lhs.match.source == FileRangeSource::PRIMARY;
    }
    if (lhs.match.entityId != rhs.match.entityId) {
        return lhs.match.entityId < rhs.match.entityId;
    }
    return lhs.match.backingIndex.value_or(0U) < rhs.match.backingIndex.value_or(0U);
}

bool MatchesEqual(const FileRangeMatch &lhs, const FileRangeMatch &rhs)
{
    return lhs.entityId == rhs.entityId && lhs.source == rhs.source && lhs.range.offset == rhs.range.offset &&
           lhs.range.size == rhs.range.size && lhs.backingIndex == rhs.backingIndex;
}

void AddCandidate(std::vector<Candidate> &candidates, EntityId entityId, FileRangeSource source,
                  const FileRange &range, std::optional<size_t> backingIndex, uint64_t fileSize)
{
    if (range.size == 0U || !range.IsValidFor(fileSize)) {
        return;
    }
    candidates.push_back({range.offset, range.offset + range.size, {entityId, source, range, backingIndex}});
}

// Collects the primary and backing ranges of every structure node as sweep candidates.
std::vector<Candidate> CollectCandidates(const StructureIndex &structure, uint64_t fileSize)
{
    std::vector<Candidate> candidates;
    for (const auto *node : structure.NodesInIdOrder()) {
        if (node->primaryRange.has_value()) {
            AddCandidate(candidates, node->id, FileRangeSource::PRIMARY, *node->primaryRange, std::nullopt, fileSize);
        }
        for (size_t index = 0U; index < node->backing.size(); ++index) {
            AddCandidate(candidates, node->id, FileRangeSource::BACKING, node->backing[index].range, index, fileSize);
        }
    }
    return candidates;
}

// Builds the sorted boundary-event list of the candidate intervals.
std::vector<Event> BuildEvents(const std::vector<Candidate> &candidates)
{
    std::vector<Event> events;
    events.reserve(candidates.size() * 2U);
    for (size_t index = 0U; index < candidates.size(); ++index) {
        events.push_back({candidates[index].begin, index, true});
        events.push_back({candidates[index].end, index, false});
    }
    std::sort(events.begin(), events.end(), [](const Event &lhs, const Event &rhs) {
        if (lhs.offset != rhs.offset) {
            return lhs.offset < rhs.offset;
        }
        if (lhs.isStart != rhs.isStart) {
            return !lhs.isStart;
        }
        return lhs.candidateIndex < rhs.candidateIndex;
    });
    return events;
}

// Extends the last segment when it borders the new match, or appends a new segment.
template <class SegmentVector>
void AppendSegment(SegmentVector *segments, uint64_t begin, uint64_t end, const FileRangeMatch &match)
{
    if (!segments->empty() && segments->back().end == begin && MatchesEqual(segments->back().match, match)) {
        segments->back().end = end;
        return;
    }
    segments->push_back({begin, end, match});
}

}  // namespace

FileRangeLocator::FileRangeLocator(ConstructorToken, std::vector<Segment> segments) : segments_(std::move(segments)) {}

std::shared_ptr<const FileRangeLocator> FileRangeLocator::Build(const StructureIndex &structure, uint64_t fileSize)
{
    const auto candidates = CollectCandidates(structure, fileSize);
    const auto events = BuildEvents(candidates);
    const auto activeLess = [&candidates](size_t lhs, size_t rhs) {
        if (CandidateRanksBefore(candidates[lhs], candidates[rhs])) {
            return true;
        }
        if (CandidateRanksBefore(candidates[rhs], candidates[lhs])) {
            return false;
        }
        return lhs < rhs;
    };
    std::set<size_t, decltype(activeLess)> active(activeLess);
    std::vector<Segment> segments;
    uint64_t previousOffset = 0U;
    size_t eventIndex = 0U;
    while (eventIndex < events.size()) {
        const uint64_t eventOffset = events[eventIndex].offset;
        if (previousOffset < eventOffset && !active.empty()) {
            AppendSegment(&segments, previousOffset, eventOffset, candidates[*active.begin()].match);
        }

        size_t nextEventIndex = eventIndex;
        while (nextEventIndex < events.size() && events[nextEventIndex].offset == eventOffset) {
            const auto &event = events[nextEventIndex];
            if (event.isStart) {
                active.insert(event.candidateIndex);
            } else {
                active.erase(event.candidateIndex);
            }
            ++nextEventIndex;
        }
        previousOffset = eventOffset;
        eventIndex = nextEventIndex;
    }

    return std::make_shared<FileRangeLocator>(ConstructorToken {}, std::move(segments));
}

std::optional<FileRangeMatch> FileRangeLocator::FindMostSpecific(uint64_t offset) const
{
    auto segment = std::upper_bound(segments_.begin(), segments_.end(), offset,
                                    [](uint64_t value, const Segment &candidate) { return value < candidate.begin; });
    if (segment == segments_.begin()) {
        return std::nullopt;
    }
    --segment;
    if (segment->begin <= offset && offset < segment->end) {
        return segment->match;
    }
    return std::nullopt;
}

}  // namespace panda::bytecode_explorer
