/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#ifndef LIBABCKIT_H
#define LIBABCKIT_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

#include "statuses.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AbckitModifyApi;
struct AbckitInspectApi;
struct AbckitGraphApi;
struct AbckitFile;
struct AbckitGraph;

#ifndef __cplusplus
typedef struct AbckitModifyApi AbckitModifyApi;
typedef struct AbckitInspectApi AbckitInspectApi;
typedef struct AbckitGraphApi AbckitGraphApi;
typedef struct AbckitFile AbckitFile;
typedef struct AbckitGraph AbckitGraph;
#endif

enum AbckitApiVersion {
    ABCKIT_VERSION_RELEASE_1_0_0,
};

/**
 * @brief Struct that holds the pointers to the top-level Abckit API.
 */
struct AbckitApi {
    /**
     * @brief Version of the Abckit API. Used for versioning the support for certain binary file features.
     */
    int apiVersion;

    /**
     * @brief Returns status of last abckit API call.
     * @return `AbckitStatus`.
     */
    enum AbckitStatus (*getLastError)(void);  // NOLINT(modernize-redundant-void-arg)

    /**
     * @brief Opens abc file from given `path`.
     * @return Pointer to the `AbckitFile`.
     * @param [ in ] path - Path to abc file.
     * @note Set `ABCKIT_STATUS_BAD_ARGUMENT` error if `path` is NULL.
     * @note Set `ABCKIT_STATUS_BAD_ARGUMENT` error if `path` doesn't point to a valid abc file.
     * @note Allocates
     */
    AbckitFile *(*openAbc)(const char *path);

    /**
     * @brief Writes `file` to the specified `path`.
     * @return None.
     * @param [ in ] file - File to write.
     * @param [ in ] path - Path where file will be written.
     * @note Set `ABCKIT_STATUS_BAD_ARGUMENT` error if `file` is NULL.
     * @note Set `ABCKIT_STATUS_BAD_ARGUMENT` error if `path` is NULL.
     * @note Set `ABCKIT_STATUS_BAD_ARGUMENT` error if `path` is not valid.
     * @note Allocates
     */
    void (*writeAbc)(AbckitFile *file, const char *path);

    /**
     * @brief Closes file, frees resources.
     * @return None.
     * @param [ in ] file - File to close.
     * @note Set `ABCKIT_STATUS_BAD_ARGUMENT` error if `file` is NULL.
     */
    void (*closeFile)(AbckitFile *file);

    /**
     * @brief Destroys graph, frees resources.
     * @return None.
     * @param [ in ] graph - Graph to destroy.
     * @note Set `ABCKIT_STATUS_BAD_ARGUMENT` error if `graph` is NULL.
     */
    void (*destroyGraph)(AbckitGraph *graph);
};

/**
 * @brief Instantiates Abckit API.
 * @return Instance of the `AbckitApi` struct with valid function pointers.
 * @param [ in ] version - Version of the API to instantiate.
 * @note Set `ABCKIT_STATUS_UNKNOWN_API_VERSION` error if `version` value is not in the `AbckitApiVersion` enum.
 */
struct AbckitApi const *AbckitGetApiImpl(enum AbckitApiVersion version);

#ifdef __cplusplus
}
#endif

#endif /* LIBABCKIT_H */