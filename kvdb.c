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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <debug.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kvdb.h"
#include "unqlite.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct callback_data {
    void (*user_callback)(const char* key, const char* value, void* cookie);
    void* cookie;
    const char* key;
    unqlite_kv_cursor *cur;
} callback_data;

static const struct database {
    const char* prefix;
    const char* path;
    const char* defaults;
} databases[] = {
    { "ram.", CONFIG_KVDB_RAM_PATH, CONFIG_KVDB_RAM_SRC },
    { "persist.", CONFIG_KVDB_PST_PATH, CONFIG_KVDB_PST_SRC },
    { NULL, CONFIG_KVDB_DAT_PATH, CONFIG_KVDB_DAT_SRC }
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int database_open(unqlite** db, const char* key);
static int database_key_callback(const void* value, size_t len, void* usr);
static int database_data_callback(const void* value, size_t len, void* usr);
static int property_load(const char* filename, unqlite* db);

/****************************************************************************
 * Name: database_open
 *
 * Description:
 *   This function calls unqlite_open() to open the specified database.
 *   Default values stored in corresponding default.properties are loaded
 *   if database doesn't exist.
 *
 * Input Parameters:
 *   unqlite *db: a secondary pointer to unqlite database
 *
 * Returned Value:
 *   0 is returned on success.
 *   Any other return value indicates failure such as:
 *   UNQLITE_MEM Out of memory (Unlikely scenario).
 *
 ****************************************************************************/

static int database_open(unqlite** db, const char* key)
{
    bool load_defaults = false;
    int type = 0;
    for (; databases[type].prefix; type++) {
        int prefix_len = strlen(databases[type].prefix);
        if (key && strncmp(key, databases[type].prefix, prefix_len) == 0)
            break;
    }
    if (access(databases[type].path, F_OK) < 0)
        load_defaults = true;
    int rc = unqlite_open(db, databases[type].path, UNQLITE_OPEN_CREATE);
    if (rc < 0) {
        _err("Failed to open database %d\n", rc);
        return rc;
    }
    if (load_defaults && access(databases[type].defaults, F_OK) == 0)
        return property_load(databases[type].defaults, *db);
    return 0;
}

/****************************************************************************
 * Name: database_key_callback
 *
 * Description:
 *   The callback function for key extraction
 *
 * Input Parameters:
 *   void *value: pointer to data of current entry
 *   size_t len: length of data of current entry (currently not used)
 *   void *usr: pointer to callback_data struct
 *
 * Returned Value:
 *   UNQLITE_OK(0)
 *
 * Limitations:
 *   This function should not be called directly. It is supposed to be called
 *   by unqlite_kv_cursor_data_callback().
 *
 ****************************************************************************/

static int database_key_callback(const void* value, size_t len, void* usr)
{
    callback_data* tmp = usr;
    tmp->key = value;
    unqlite_kv_cursor_data_callback(tmp->cur, database_data_callback, tmp);
    return UNQLITE_OK;
}

/****************************************************************************
 * Name: database_data_callback
 *
 * Description:
 *   The callback function for data extraction
 *
 * Input Parameters:
 *   void *value: pointer to data of current entry
 *   size_t len: length of data of current entry (currently not used)
 *   void *usr: pointer to callback_data struct
 *
 * Returned Value:
 *   UNQLITE_OK(0)
 *
 * Limitations:
 *   This function should not be called directly. It is supposed to be called
 *   by unqlite_kv_cursor_data_callback().
 *
 ****************************************************************************/

static int database_data_callback(const void* value, size_t len, void* usr)
{
    callback_data* tmp = usr;
    tmp->user_callback(tmp->key, value, tmp->cookie);
    return UNQLITE_OK;
}

/****************************************************************************
 * Name: property_load
 *
 * Description:
 *   Read default KVs from a text file, parse and write the KVs to database
 *
 * Input Parameters:
 *   const char* filename: name of the file to be loaded
 *   unqlite *db: pointer to the target database
 *
 * Returned Value:
 *   On success return 0.
 *   Returns <0 if file open failed or store failed.
 *
 * Limitations:
 *   The format of the text file should be "KEY1=VALUE1\nKEY2=VALUE2\n..."
 *
 ****************************************************************************/

static int property_load(const char* filename, unqlite* db)
{
    FILE* f = fopen(filename, "r");
    if (!f) {
        _err("Error opening file '%s'\n", filename);
        return -errno;
    }
    size_t buflen = PROP_VALUE_MAX << 1; //Maybe not enough
    char* buf = malloc(buflen);

    while (fgets(buf, buflen, f)) { //fgets() will truncate string if too long
        char *saveptr;
        char *key = strtok_r(buf, "=", &saveptr);
        char *value = strtok_r(NULL, "\n", &saveptr);
        if (value != NULL) {
            int rc = unqlite_kv_store(db, key, strlen(key) + 1,
                                          value, strlen(value) + 1);
            if (rc < 0)
                _err("Load value failed %d\n", rc);
        }
    }
    fclose(f);
    free(buf);
    return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: property_set
 *
 * Description:
 *   Store Key-Values to database.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   const char* value: entry value string
 *
 * Returned Value:
 *         0: success
 *        <0: failure during execution
 *
 ****************************************************************************/

int property_set(const char* key, const char* value)
{
    if (value == NULL)
        return property_delete(key);
    int value_len = strlen(value);
    if (value_len > PROP_VALUE_MAX - 1) {
        _err("Value too long\n");
        return -E2BIG;
    }
    unqlite* db;
    int rc = database_open(&db, key);
    if (rc < 0)
        return rc;
    rc = unqlite_kv_store(db, key, strlen(key) + 1, value, value_len + 1);
    //saves terminating '\0'
    if (rc < 0)
        _err("Store value failed %d\n", rc);
    unqlite_close(db);
    return rc;
}

/****************************************************************************
 * Name: property_get
 *
 * Description:
 *   Retrieve Key-Values from database.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   char* value: pointer to string buffer
 *   const char* default_value: the value to return on failure
 *
 * Returned Value:
 *   On success returns the length of the value which will never be greater
 *   than PROP_VALUE_MAX - 1 and will always be zero terminated.
 *   (the length does not include the terminating zero).
 *   On failure returns length of default_value.
 *
 ****************************************************************************/

int property_get(const char* key, char* value, const char* default_value)
{
    if (!key)
        goto err_out;
    unqlite* db;
    int rc = database_open(&db, key);
    if (rc < 0)
        goto err_out;
    int64_t buflen = PROP_VALUE_MAX;
    rc = unqlite_kv_fetch(db, key, strlen(key) + 1, value, &buflen);
    unqlite_close(db);
    if (rc < 0) {
        _err("Get value failed %d\n", rc);
        goto err_out;
    }
    return buflen - 1;
err_out:
    strcpy(value, default_value);
    return strlen(default_value);
}

/****************************************************************************
 * Name: property_delete
 *
 * Description:
 *   Delete a KV pair by key
 *
 * Input Parameters:
 *   const char* key: entry key string (without db prefix)
 *
 * Returned Value:
 *         0: success
 *        <0: failure during execution
 *
 ****************************************************************************/

int property_delete(const char* key)
{
    unqlite* db;
    int rc = database_open(&db, key);
    if (rc < 0)
        return rc;
    rc = unqlite_kv_delete(db, key, strlen(key) + 1);
    unqlite_close(db);
    return rc;
}

/****************************************************************************
 * Name: property_list
 *
 * Description:
 *   List all KVs in every database and calls callback function.
 *
 * Input Parameters:
 *   void (*fn)(const char* name, const char* value, void* cookie):
 *       callback function
 *   void* cookie: cookie data to pass to callback function
 *
 * Returned Value:
 *   Returns 0 on success, <0 if all databases failed to open.
 *
 ****************************************************************************/

int property_list(
    void (*fn)(const char* name, const char* value, void* cookie),
    void* cookie)
{
    callback_data data = { fn, cookie, 0, 0 };
    int fail_count = 0;
    for (int type = 0; type < sizeof(databases) / sizeof(struct database);
         type++) {
        unqlite* db;
        unqlite_kv_cursor* cur;
        int rc = database_open(&db, databases[type].prefix);
        if (rc < 0)
          {
            fail_count++;
            continue;
          }

        /* Allocate a new cursor instance */

        rc = unqlite_kv_cursor_init(db, &cur);
        if (rc < 0) {
            _err("Out of memory %d\n", rc);
            fail_count++;
            goto release_db;
        }

        /* Point to the first record */

        rc = unqlite_kv_cursor_first_entry(cur);
        if (rc < 0)
          {
            fail_count++;
            goto release_all;
          }

        /* Iterate over the entries */

        while (unqlite_kv_cursor_valid_entry(cur)) {
            data.cur = cur;

            /* Consume the key and data */

            unqlite_kv_cursor_key_callback(cur, database_key_callback, &data);

            /* Point to the next entry */

            unqlite_kv_cursor_next_entry(cur);
        }

        /* Finally, Release our cursor */
release_all:
        unqlite_kv_cursor_release(db, cur);
release_db:
        unqlite_close(db);
    }
    return fail_count < sizeof(databases) / sizeof(struct database) ?
           0 : -ENOSPC;
}

/****************************************************************************
 * Name: property_set_bool
 *
 * Description:
 *   Saves a boolean to database.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   int8_t value: entry value
 *
 * Returned Value:
 *         0: success
 *        <0: failure during execution
 *
 ****************************************************************************/

int property_set_bool(const char* key, int8_t value)
{
    return property_set(key, value ? "true" : "false");
}

/****************************************************************************
 * Name: property_get_bool
 *
 * Description:
 *   Retrieve a Key-Value from database and interpret the value as boolean.
 *   This is taken from Android libcutils:
 *   https://android.googlesource.com/platform/system/core/+/master/libcutils/
 *   properties.cpp
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   int8_t default_value: the value to return on failure
 *
 * Returned Value:
 *   On success returns a boolean.
 *   On failure returns default_value.
 *
 ****************************************************************************/

int8_t property_get_bool(const char* key, int8_t default_value)
{
    if (!key)
        return default_value;
    int8_t result = default_value;
    char buf[PROP_VALUE_MAX];
    int len = property_get(key, buf, "");
    if (len == 1) {
        char ch = buf[0];
        if (ch == '0' || ch == 'n') {
            result = false;
        } else if (ch == '1' || ch == 'y') {
            result = true;
        }
    } else if (len > 1) {
        if (!strcmp(buf, "no") || !strcmp(buf, "false") ||
            !strcmp(buf, "off")) {
            result = false;
        } else if (!strcmp(buf, "yes") || !strcmp(buf, "true") ||
            !strcmp(buf, "on")) {
            result = true;
        }
    }
    return result;
}

/****************************************************************************
 * Name: property_set_int32
 *
 * Description:
 *   Saves an 32-bit integer to database.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   int32_t value: entry value
 *
 * Returned Value:
 *         0: success
 *        <0: failure during execution
 *
 ****************************************************************************/

int property_set_int32(const char* key, int32_t value)
{
    char buf[12];
    snprintf(buf, 12, "%"PRId32, value);
    return property_set(key, buf);
}

/****************************************************************************
 * Name: property_get_int32
 *
 * Description:
 *   Retrieve a Key-Value from database and interpret the value as int32_t.
 *   This is modified from Android libcutils:
 *   https://android.googlesource.com/platform/system/core/+/master/libcutils/
 *   properties.cpp
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   int32_t default_value: the value to return on failure
 *
 * Returned Value:
 *   On success returns a boolean.
 *   On failure returns default_value.
 *
 ****************************************************************************/

int32_t property_get_int32(const char* key, int32_t default_value)
{
    if (!key)
        return default_value;
    char value[PROP_VALUE_MAX];
    if (property_get(key, value, "") < 1)
        return default_value;
    int32_t result = default_value;
    int saved_errno = errno;
    errno = 0;
    char* end = NULL;
    int32_t v = strtol(value, &end, 0);
    if (errno != ERANGE && end != value)
        result = v;
    errno = saved_errno;
    return result;
}

/****************************************************************************
 * Name: property_set_int64
 *
 * Description:
 *   Saves an 64-bit integer to database.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   int64_t value: entry value
 *
 * Returned Value:
 *         0: success
 *        <0: failure during execution
 *
 ****************************************************************************/

int property_set_int64(const char* key, int64_t value)
{
    char buf[21];
    snprintf(buf, 21, "%"PRId64, value);
    return property_set(key, buf);
}

/****************************************************************************
 * Name: property_get_int64
 *
 * Description:
 *   Retrieve a Key-Value from database and interpret the value as int64_t.
 *   This is modified from Android libcutils:
 *   https://android.googlesource.com/platform/system/core/+/master/libcutils/
 *   properties.cpp
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   int64_t default_value: the value to return on failure
 *
 * Returned Value:
 *   On success returns a boolean.
 *   On failure returns default_value.
 *
 ****************************************************************************/

int64_t property_get_int64(const char* key, int64_t default_value)
{
    if (!key)
        return default_value;
    char value[PROP_VALUE_MAX];
    if (property_get(key, value, "") < 1)
        return default_value;
    int64_t result = default_value;
    int saved_errno = errno;
    errno = 0;
    char* end = NULL;
    int64_t v = strtoll(value, &end, 0);
    if (errno != ERANGE && end != value)
        result = v;
    errno = saved_errno;
    return result;
}
