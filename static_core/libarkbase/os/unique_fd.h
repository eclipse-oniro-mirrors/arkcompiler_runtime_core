/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

#ifndef PANDA_LIBPANDABASE_OS_UNIQUE_FD_H_
#define PANDA_LIBPANDABASE_OS_UNIQUE_FD_H_

#ifdef PANDA_TARGET_UNIX
#include "platforms/unix/libarkbase//unique_fd.h"
#elif PANDA_TARGET_WINDOWS
#include "platforms/windows/libarkbase//unique_fd.h"
#else
#error "Unsupported platform"
#endif  // PANDA_TARGET_UNIX

#include "libarkbase/os/failure_retry.h"
#include "libarkbase/utils/logger.h"

#include <cerrno>
#include <cstdint>
#if defined(PANDA_TARGET_OHOS) && defined(PANDA_BUILD_IN_OHOS_TREE)
#include <cstdio>
#endif
#include <unistd.h>

namespace ark::os::unique_fd {

class UniqueFd {
public:
    explicit UniqueFd(int fd = -1) noexcept
    {
        Reset(fd);
    }

    UniqueFd(const UniqueFd &otherFd) = delete;
    UniqueFd &operator=(const UniqueFd &otherFd) = delete;

    UniqueFd(UniqueFd &&otherFd) noexcept
    {
        Reset(otherFd.Release());
    }

    UniqueFd &operator=(UniqueFd &&otherFd) noexcept
    {
        Reset(otherFd.Release());
        return *this;
    }

    ~UniqueFd()
    {
        Reset();
    }

    int Release() noexcept
    {
        int fd = fd_;
#if defined(PANDA_TARGET_OHOS) && defined(PANDA_BUILD_IN_OHOS_TREE)
        if (fd != -1) {
            fdsan_exchange_owner_tag(
                fd, fdsan_create_owner_tag(FDSAN_OWNER_TYPE_UNIQUE_FD, reinterpret_cast<uintptr_t>(this)), 0);
        }
#endif
        fd_ = -1;
        return fd;
    }

    void Reset(int newFd = -1)
    {
        if (fd_ != -1) {
            ASSERT(newFd != fd_);
            DefaultCloser(fd_);
        }
        fd_ = newFd;
#if defined(PANDA_TARGET_OHOS) && defined(PANDA_BUILD_IN_OHOS_TREE)
        if (fd_ != -1) {
            fdsan_exchange_owner_tag(
                fd_, 0, fdsan_create_owner_tag(FDSAN_OWNER_TYPE_UNIQUE_FD, reinterpret_cast<uintptr_t>(this)));
        }
#endif
    }

    int Get() const noexcept
    {
        return fd_;
    }

    bool IsValid() const noexcept
    {
        return fd_ != -1;
    }

private:
    void DefaultCloser(int fd)
    {
#if defined(PANDA_TARGET_OHOS) && defined(PANDA_BUILD_IN_OHOS_TREE)
        uint64_t tag = fdsan_create_owner_tag(FDSAN_OWNER_TYPE_UNIQUE_FD, reinterpret_cast<uintptr_t>(this));
        const int result = fdsan_close_with_tag(fd, tag);
        const int savedErrno = errno;
        // Match OHOS close(): treat EINTR as success without retrying a possibly reused fd.
        LOG_IF(result != 0 && !(result == -1 && savedErrno == EINTR), FATAL, COMMON) << "Incorrect fd: " << fd;
#else
        LOG_IF(PANDA_FAILURE_RETRY(::close(fd)) != 0, FATAL, COMMON) << "Incorrect fd: " << fd;
#endif
    }

    int fd_ = -1;
};

}  // namespace ark::os::unique_fd

#endif  // PANDA_LIBPANDABASE_OS_UNIQUE_FD_H_
