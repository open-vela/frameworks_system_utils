/*
 * Copyright (C) 2023 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __INTERNAL_H
#define __INTERNAL_H

#include <stddef.h>
#include <syslog.h>

#define KVLOG(level, fmt, ...) \
    syslog(level, "[kvdb] [%s:%d] " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#if defined(CONFIG_KVDB_LOG_INFO)
#define KVINFO(fmt, ...) KVLOG(LOG_INFO, fmt, ##__VA_ARGS__)
#define KVWARN(fmt, ...) KVLOG(LOG_WARNING, fmt, ##__VA_ARGS__)
#define KVERR(fmt, ...) KVLOG(LOG_ERR, fmt, ##__VA_ARGS__)
#elif defined(CONFIG_KVDB_LOG_WARN)
#define KVINFO(fmt, ...)
#define KVWARN(fmt, ...) KVLOG(LOG_WARNING, fmt, ##__VA_ARGS__)
#define KVERR(fmt, ...) KVLOG(LOG_ERR, fmt, ##__VA_ARGS__)
#elif defined(CONFIG_KVDB_LOG_ERR)
#define KVINFO(fmt, ...)
#define KVWARN(fmt, ...)
#define KVERR(fmt, ...) KVLOG(LOG_ERR, fmt, ##__VA_ARGS__)
#else
#define KVINFO(fmt, ...)
#define KVWARN(fmt, ...)
#define KVERR(fmt, ...)
#endif

#define PROP_SERVER_PATH "kvdbd"

#if defined(__cplusplus)
extern "C" {
#endif

#define PERSIST_LABEL "persist."
#define PERSIST_LABEL_LEN 8

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

enum {
    KVDB_PERSIST, /* save key-value pairs in flash */
#ifdef CONFIG_KVDB_TEMPORARY_STORAGE
    KVDB_MEM, /* save key-value pairs in memory */
#endif
    KVDB_COUNT
};

struct kvdb;

typedef void (*kvdb_consume)(const char* key, const void* value, size_t val_len, void* cookie);

int kvdb_set(struct kvdb* kvdb, const char* key, size_t key_len, const void* value, size_t val_len, bool force);
ssize_t kvdb_get(struct kvdb* kvdb, const char* key, size_t key_len, void* value, size_t val_len);
int kvdb_delete(struct kvdb* kvdb, const char* key, size_t key_len);
int kvdb_list(struct kvdb* kvdb, kvdb_consume consume, void* cookie);
int kvdb_commit(struct kvdb* kvdb);
int kvdb_init(struct kvdb** kvdb);
void kvdb_uninit(struct kvdb* kvdb);

int kvdb_get_index(const char* key);

#if defined(__cplusplus)
}
#endif

#endif /* __INTERNAL_H */
