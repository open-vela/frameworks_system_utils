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

#include <cutils/properties.h>
#include <sys/types.h>

#define PROP_MSG_MAX (3 + PROP_NAME_MAX + PROP_VALUE_MAX) /* +3 = +1(opcode) +2(len) */

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief Store Key-Values to database.
 * @param[in] key entry key string
 * @param[in] value entry value string
 * @note Key starting with "persist." will be stored permanently after commit,
 *   others will be lost after reboot.
 * @return On success returns 0, -errno otherwise.
 */
int property_set_oneway(const char* key, const char* value);

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

/**
 * @brief Wait the monitored key until its value updated or key deleted
 * @param[in] key the monitored key string, support fnmatch pattern
 * @param[in] the length of the newvalue
 * @param[in] timeout the wait timeout time (in milliseconds)
 * @param[out] newkey pointer to a string buffer to receive the key of
 *                    the updated/deleted value
 * @param[out] newvalue pointer to a string buffer to receive the updated
 *                      value or deleted value
 * @return On success returns the length of the value.
 */
ssize_t property_wait(const char* key, char* newkey, void* newvalue, size_t val_len, int timeout);

/**
 * @brief Open a key monitor channel
 * @param[in] key the monitored key string, support fnmatch pattern
 * @return On success returns a file descriptor, -errno otherwise.
 */
int property_monitor_open(const char* key);

/**
 * @brief Read the monitored key and value
 * @param[in] fd file descriptor returned by property_monitor_open()
 * @param[in] the length of the newvalue
 * @param[out] newkey pointer to a string buffer to receive the key of
 *                    the updated/deleted value
 * @param[out] newvalue pointer to a string buffer to receive the updated
 *                      value or deleted value
 * @return On success returns the length of the value.
 */
ssize_t property_monitor_read(int fd, char* newkey, void* newvalue, size_t val_len);

/**
 * @brief Close a key monitor channel
 * @param[in] fd file descriptor returned by property_monitor_open()
 * @return On success returns 0, -errno otherwise.
 */
int property_monitor_close(int fd);

/**
 * @brief Saves a boolean to database.
 * @param[in] key entry key string
 * @param[in] value entry boolean value
 * @return On success returns 0, -errno otherwise.
 */
int property_set_bool(const char* key, int8_t value);
int property_set_bool_oneway(const char* key, int8_t value);

/**
 * @brief Saves an 32-bit integer to database.
 * @param[in] key entry key string
 * @param[in] value entry value
 * @return On success returns 0, -errno otherwise.
 */
int property_set_int32(const char* key, int32_t value);
int property_set_int32_oneway(const char* key, int32_t value);

/**
 * @brief Saves an 64-bit integer to database.
 * @param[in] key entry key string
 * @param[in] value entry value
 * @return On success returns 0, -errno otherwise.
 */
int property_set_int64(const char* key, int64_t value);
int property_set_int64_oneway(const char* key, int64_t value);

/**
 * @brief Saves a binary buffer to database.
 * @param[in] key entry key string
 * @param[in] value buffer value
 * @param[in] size entry size
 * @return On success returns 0, -errno otherwise.
 */
int property_set_buffer(const char* key, const void* value, size_t size);
int property_set_buffer_oneway(const char* key, const void* value, size_t size);

/**
 * @brief Retrieve a Key-Value from database and interpret as binary buffer.
 * @param[in] key entry key string
 * @param[in] value buffer value
 * @param[in] size buffer size
 * @return On success returns the array length, -errno otherwise.
 */
ssize_t property_get_buffer(const char* key, void* value, size_t size);

/**
 * @brief Saves a the specified length buffer to database.
 * @param[in] key entry key string
 * @param[in] value buffer value
 * @param[in] value buffer size
 * @return On success returns the array length, -errno otherwise.
 */
int property_set_binary(const char* key, const void* value, size_t val_len, bool oneway);
ssize_t property_get_binary(const char* key, void* value, size_t val_len);

/**
 * @List all KVs in every database and calls callback function.
 * @param[in] callback function
 * @param[in] cookie data to pass to callback function
 * @Returns 0 on success, <0 if all databases failed to open.
 */
int property_list_binary(void (*propfn)(const char* key, const void* value, size_t val_len, void* cookie), void* cookie);
#if defined(__cplusplus)
}
#endif

#endif
