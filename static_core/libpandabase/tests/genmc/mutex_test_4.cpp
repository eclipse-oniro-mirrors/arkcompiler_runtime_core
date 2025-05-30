/*
 * Copyright (c) 2021-2024 Huawei Device Co., Ltd.
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

#include <pthread.h>
#include <cstdlib>
#define MC_ON
#include "platforms/unix/libpandabase/futex/fmutex.cpp"

// The tests checks work with two mutexes

// Copy of mutex storage, after complete implementation should totally replace mutex::current_tid
thread_local pthread_t current_tid;

static struct fmutex g_x;
static struct fmutex g_y;
static int g_shared;

static void *Thread1(void *arg)
{
    intptr_t index = reinterpret_cast<intptr_t>(arg);

    MutexLock(&g_x, false);
    MutexLock(&g_y, false);
    g_shared = index;
    int r = g_shared;
    ASSERT(r == index);
    MutexUnlock(&g_y);
    MutexUnlock(&g_x);
    return nullptr;
}

static void *Thread2(void *arg)
{
    intptr_t index = reinterpret_cast<intptr_t>(arg);

    MutexLock(&g_x, false);
    g_shared = index;
    int r = g_shared;
    ASSERT(r == index);
    MutexUnlock(&g_x);
    return nullptr;
}

static void *Thread3(void *arg)
{
    intptr_t index = reinterpret_cast<intptr_t>(arg);

    MutexLock(&g_y, false);
    g_shared = index;
    int r = g_shared;
    ASSERT(r == index);
    MutexUnlock(&g_y);
    return nullptr;
}

int main()
{
    constexpr int N = 3;
    MutexInit(&g_x);
    MutexInit(&g_y);
    pthread_t t[N];

    pthread_create(&t[1U], nullptr, Thread2, reinterpret_cast<void *>(1U));
    pthread_create(&t[0U], nullptr, Thread1, reinterpret_cast<void *>(0U));
    pthread_join(t[1U], nullptr);
    pthread_create(&t[2U], nullptr, Thread3, reinterpret_cast<void *>(2U));

    pthread_join(t[0U], nullptr);
    pthread_join(t[2U], nullptr);

    MutexDestroy(&g_x);
    MutexDestroy(&g_y);
    return 0;
}
