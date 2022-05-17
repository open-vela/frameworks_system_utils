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

#ifndef __CUTILS_PROPERTIES_H
#define __CUTILS_PROPERTIES_H

#include <stdint.h>

/**
 * @brief Maximum property key string length = 127
 */
#define PROP_NAME_MAX      127

/**
 * @brief Maximum property value string length = 255
 */
#define PROP_VALUE_MAX     255

#define PROPERTY_KEY_MAX   PROP_NAME_MAX
#define PROPERTY_VALUE_MAX PROP_VALUE_MAX

#define PROP_MSG_MAX       (3 + PROP_NAME_MAX + PROP_VALUE_MAX) /* +3 = +1(opcode) +2(len) */
#define PROP_SERVER_PATH   "kvdbd"

#if defined(__cplusplus)
extern "C"
{
#endif

/**
 * @brief Store Key-Values to database.
 * @param[in] key entry key string
 * @param[in] value entry value string
 * @note Key starting with "persist." will be stored permanently after commit,
 *   others will be lost after reboot.
 * @return On success returns 0, -errno otherwise.
 */
int property_set(const char* key, const char* value);

/**
 * @brief Retrieve Key-Values from database.
 * @param[in] key entry key string
 * @param[out] value pointer to string buffer
 * @param[in] default_value the value to return on failure
 * @return On success returns the length of the value which will never be greater
 *   than PROP_NAME_MAX - 1 and will always be zero terminated.
 *   (the length does not include the terminating zero).
 *   On failure returns length of default_value.
 */
int property_get(const char* key, char* value, const char* default_value);

/**
 * @brief Delete a KV pair by key.
 * @param[in] key entry key string
 * @return On success returns 0, -errno otherwise.
 */
int property_delete(const char* key);

/**
 * @brief Actively commit all property changes.
 * @return On success returns 0, -errno otherwise.
 */
int property_commit(void);

/**
 * @brief Reload default property value.
 * @return On success returns 0, -errno otherwise.
 */
int property_reload(void);

typedef void (*property_callback)(const char* key, const char* value, void* cookie);

/**
 * @brief List all KVs in every database and calls callback function.
 * @param[in] property_callback propfn: callback function
 * @param[in] cookie: cookie data to pass to callback function
 * @return On success returns 0, -errno if all databases fail to open.
 */
int property_list(property_callback propfn, void* cookie);

/**
 * @brief Saves a boolean to database.
 * @param[in] key entry key string
 * @param[in] value entry boolean value
 * @return On success returns 0, -errno otherwise.
 */
int property_set_bool(const char* key, int8_t value);

/**
 * @brief Retrieve a Key-Value from database and interpret the value as boolean.
 *   This is taken from Android libcutils:
 *   https://android.googlesource.com/platform/system/core/+/master/libcutils/
 *   properties.cpp
 * @param[in] key entry key string
 * @param[in] value the value to return on failure
 * @return On success returns a boolean, otherwise returns default_value.
 */
int8_t property_get_bool(const char* key, int8_t default_value);

/**
 * @brief Saves an 32-bit integer to database.
 * @param[in] key entry key string
 * @param[in] value entry value
 * @return On success returns 0, -errno otherwise.
 */
int property_set_int32(const char* key, int32_t value);

/**
 * @brief Retrieve a Key-Value from database and interpret the value as int32_t.
 *   This is taken from Android libcutils:
 *   https://android.googlesource.com/platform/system/core/+/master/libcutils/
 *   properties.cpp
 * @param[in] key entry key string
 * @param[in] value the value to return on failure
 * @return On success returns a int32_t, otherwise returns default_value.
 */
int32_t property_get_int32(const char* key, int32_t default_value);

/**
 * @brief Saves an 64-bit integer to database.
 * @param[in] key entry key string
 * @param[in] value entry value
 * @return On success returns 0, -errno otherwise.
 */
int property_set_int64(const char* key, int64_t value);

/**
 * @brief Retrieve a Key-Value from database and interpret the value as int64_t.
 *   This is taken from Android libcutils:
 *   https://android.googlesource.com/platform/system/core/+/master/libcutils/
 *   properties.cpp
 * @param[in] key entry key string
 * @param[in] value the value to return on failure
 * @return On success returns a int64_t, otherwise returns default_value.
 */
int64_t property_get_int64(const char* key, int64_t default_value);

#if defined(__cplusplus)
}
#endif

#endif
