/****************************************************************************
 * internal.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __INTERNAL_H
#define __INTERNAL_H

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct kvdb;

typedef int (*kvdb_consume)(const char* key, size_t key_len, const char* value, size_t val_len, void* cookie);

int kvdb_set(struct kvdb* kvdb, const char* key, size_t key_len, const char* value, size_t val_len, bool force);
int kvdb_get(struct kvdb* kvdb, const char* key, size_t key_len, char* value);
int kvdb_delete(struct kvdb* kvdb, const char* key, size_t key_len);
int kvdb_list(struct kvdb* kvdb, kvdb_consume consume, void* cookie);
int kvdb_commit(struct kvdb* kvdb);
int kvdb_init(struct kvdb** kvdb);
void kvdb_uninit(struct kvdb* kvdb);

#if defined(__cplusplus)
}
#endif

#endif /* __INTERNAL_H */