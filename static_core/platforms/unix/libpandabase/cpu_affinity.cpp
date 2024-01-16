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

#include "libpandabase/os/cpu_affinity.h"
#include "libpandabase/utils/logger.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <thread>

namespace ark::os {

CpuSet::CpuSet()  // NOLINT(cppcoreguidelines-pro-type-member-init)
{
    CPU_ZERO(&cpuset_);
}

void CpuSet::Set(int cpu)
{
    CPU_SET(cpu, &cpuset_);
}

void CpuSet::Clear()
{
    CPU_ZERO(&cpuset_);
}

void CpuSet::Remove(int cpu)
{
    CPU_CLR(cpu, &cpuset_);
}

size_t CpuSet::Count() const
{
    return CPU_COUNT(&cpuset_);
}

bool CpuSet::IsSet(int cpu) const
{
    return CPU_ISSET(cpu, &cpuset_);
}

bool CpuSet::IsEmpty() const
{
    return Count() == 0U;
}

CpuSetType *CpuSet::GetData()
{
    return &cpuset_;
}

const CpuSetType *CpuSet::GetData() const
{
    return &cpuset_;
}

bool CpuSet::operator==(const CpuSet &other) const
{
    return CPU_EQUAL(&cpuset_, other.GetData());
}

bool CpuSet::operator!=(const CpuSet &other) const
{
    return !(*this == other);
}

/* static */
void CpuSet::And(CpuSet &result, const CpuSet &lhs, const CpuSet &rhs)
{
    CPU_AND(result.GetData(), lhs.GetData(), rhs.GetData());
}

/* static */
void CpuSet::Or(CpuSet &result, const CpuSet &lhs, const CpuSet &rhs)
{
    CPU_OR(result.GetData(), lhs.GetData(), rhs.GetData());
}

/* static */
void CpuSet::Xor(CpuSet &result, const CpuSet &lhs, const CpuSet &rhs)
{
    CPU_XOR(result.GetData(), lhs.GetData(), rhs.GetData());
}

/* static */
void CpuSet::Copy(CpuSet &destination, const CpuSet &source)
{
    memcpy(destination.GetData(), source.GetData(), sizeof(CpuSetType));
    ASSERT(destination == source);
}

CpuSet &CpuSet::operator&=(const CpuSet &other)
{
    CPU_AND(&cpuset_, &cpuset_, other.GetData());
    return *this;
}

CpuSet &CpuSet::operator|=(const CpuSet &other)
{
    CPU_OR(&cpuset_, &cpuset_, other.GetData());
    return *this;
}

CpuSet &CpuSet::operator^=(const CpuSet &other)
{
    CPU_XOR(&cpuset_, &cpuset_, other.GetData());
    return *this;
}

size_t CpuAffinityManager::cpuCount_ = 0U;
CpuSet CpuAffinityManager::bestCpuSet_;           // NOLINT(fuchsia-statically-constructed-objects)
CpuSet CpuAffinityManager::middleCpuSet_;         // NOLINT(fuchsia-statically-constructed-objects)
CpuSet CpuAffinityManager::bestAndMiddleCpuSet_;  // NOLINT(fuchsia-statically-constructed-objects)
CpuSet CpuAffinityManager::weakCpuSet_;           // NOLINT(fuchsia-statically-constructed-objects)

/* static */
void CpuAffinityManager::Initialize()
{
    ASSERT(!IsCpuAffinityEnabled());
    ASSERT(bestCpuSet_.IsEmpty());
    ASSERT(middleCpuSet_.IsEmpty());
    ASSERT(bestAndMiddleCpuSet_.IsEmpty());
    ASSERT(weakCpuSet_.IsEmpty());
    cpuCount_ = std::thread::hardware_concurrency();
    LoadCpuFreq();
}

/* static */
bool CpuAffinityManager::IsCpuAffinityEnabled()
{
    return cpuCount_ != 0U;
}

/* static */
void CpuAffinityManager::Finalize()
{
    cpuCount_ = 0U;
    bestCpuSet_.Clear();
    middleCpuSet_.Clear();
    bestAndMiddleCpuSet_.Clear();
    weakCpuSet_.Clear();
}

/* static */
void CpuAffinityManager::LoadCpuFreq()
{
    if (!IsCpuAffinityEnabled()) {
        return;
    }
    std::vector<CpuInfo> cpu;
    cpu.reserve(cpuCount_);
    for (size_t cpuNumber = 0; cpuNumber < cpuCount_; ++cpuNumber) {
        std::string cpuFreqPath =
            "/sys/devices/system/cpu/cpu" + std::to_string(cpuNumber) + "/cpufreq/cpuinfo_max_freq";
        std::ifstream cpuFreqFile(cpuFreqPath.c_str());
        uint64_t freq = 0U;
        if (cpuFreqFile.is_open()) {
            if (!(cpuFreqFile >> freq)) {
                freq = 0U;
            } else {
                cpu.push_back({cpuNumber, freq});
            }
            cpuFreqFile.close();
        }
        if (freq == 0U) {
            cpuCount_ = 0;
            return;
        }
    }
    // Sort by cpu frequency from best to weakest
    std::sort(cpu.begin(), cpu.end(), [](const CpuInfo &lhs, const CpuInfo &rhs) { return lhs.freq > rhs.freq; });
    auto cpuInfo = cpu.front();
    bestCpuSet_.Set(cpuInfo.number);
    for (auto it = cpu.begin() + 1U; it != cpu.end(); ++it) {
        if (it->freq == cpuInfo.freq) {
            bestCpuSet_.Set(it->number);
        } else {
            break;
        }
    }
    cpuInfo = cpu.back();
    weakCpuSet_.Set(cpuInfo.number);
    for (auto it = cpu.rbegin() + 1U; it != cpu.rend(); ++it) {
        if (it->freq == cpuInfo.freq) {
            weakCpuSet_.Set(it->number);
        } else {
            break;
        }
    }
    if (bestCpuSet_.Count() + weakCpuSet_.Count() >= cpuCount_) {
        CpuSet::Copy(middleCpuSet_, weakCpuSet_);
    } else {
        for (auto it = cpu.begin() + bestCpuSet_.Count(); it != cpu.end() - weakCpuSet_.Count(); ++it) {
            middleCpuSet_.Set(it->number);
        }
        ASSERT(bestCpuSet_.Count() + middleCpuSet_.Count() + weakCpuSet_.Count() == cpuCount_);
    }
    ASSERT(!bestCpuSet_.IsEmpty());
    ASSERT(!middleCpuSet_.IsEmpty());
    ASSERT(!weakCpuSet_.IsEmpty());
    CpuSet::Or(bestAndMiddleCpuSet_, bestCpuSet_, middleCpuSet_);
    ASSERT(!bestAndMiddleCpuSet_.IsEmpty());
    for (auto it : cpu) {
        LOG(DEBUG, COMMON) << "CPU number: " << it.number << ", freq: " << it.freq;
    }
}

/* static */
bool CpuAffinityManager::GetThreadAffinity(int tid, CpuSet &cpuset)
{
    bool success = sched_getaffinity(tid, sizeof(CpuSetType), cpuset.GetData()) == 0;
    LOG_IF(!success, DEBUG, COMMON) << "Couldn't get affinity for thread with tid = "
                                    << (tid != 0 ? tid : thread::GetCurrentThreadId())
                                    << ", error: " << std::strerror(errno);
    return success;
}

/* static */
bool CpuAffinityManager::GetCurrentThreadAffinity(CpuSet &cpuset)
{
    // 0 is value for current thread
    return GetThreadAffinity(0, cpuset);
}

/* static */
bool CpuAffinityManager::SetAffinityForThread(int tid, const CpuSet &cpuset)
{
    if (!IsCpuAffinityEnabled()) {
        return false;
    }
    bool success = sched_setaffinity(tid, sizeof(CpuSetType), cpuset.GetData()) == 0;
    LOG_IF(!success, DEBUG, COMMON) << "Couldn't set affinity with mask " << CpuSetToString(cpuset)
                                    << " for thread with tid = " << (tid != 0 ? tid : thread::GetCurrentThreadId())
                                    << ", error: " << std::strerror(errno);
    return success;
}

/* static */
bool CpuAffinityManager::SetAffinityForThread(int tid, uint8_t powerFlags)
{
    if (powerFlags == CpuPower::ANY) {
        return true;
    }
    CpuSet cpuset;
    if ((powerFlags & CpuPower::BEST) != 0) {
        cpuset |= bestCpuSet_;
    }
    if ((powerFlags & CpuPower::MIDDLE) != 0) {
        cpuset |= middleCpuSet_;
    }
    if ((powerFlags & CpuPower::WEAK) != 0) {
        cpuset |= weakCpuSet_;
    }
    return SetAffinityForThread(tid, cpuset);
}

/* static */
bool CpuAffinityManager::SetAffinityForCurrentThread(uint8_t powerFlags)
{
    // 0 is value for current thread
    return SetAffinityForThread(0, powerFlags);
}

/* static */
bool CpuAffinityManager::SetAffinityForCurrentThread(const CpuSet &cpuset)
{
    // 0 is value for current thread
    return SetAffinityForThread(0, cpuset);
}

}  // namespace ark::os
