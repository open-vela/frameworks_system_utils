/*
 * Copyright (C) 2020 Xiaomi Corporation
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

#ifndef __KVDB_H
#define __KVDB_H

#include <stdint.h>

#define PROP_KEY_MAX     100
#define PROP_VALUE_MAX   100
#define PROP_MSG_MAX     (3 + PROP_KEY_MAX + PROP_VALUE_MAX) /* +3 = +1(opcode) +2(len) */
#define PROP_SERVER_PATH "/kvdbd"

int property_set(const char* key, const char* value);
int property_get(const char* key, char* value, const char* default_value);
int property_delete(const char* key);

typedef void (*property_callback)(const char* key, const char* value, void* cookie);
int property_list(property_callback propfn, void* cookie);

int property_set_bool(const char* key, int8_t value);
int8_t property_get_bool(const char* key, int8_t default_value);
int property_set_int32(const char* key, int32_t value);
int32_t property_get_int32(const char* key, int32_t default_value);
int property_set_int64(const char* key, int64_t value);
int64_t property_get_int64(const char* key, int64_t default_value);

#endif
