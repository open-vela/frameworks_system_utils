/*
 * Copyright (c) 2024 Xiaomi Technologies Co., Ltd.
 * All rights reserved.
 *
 * This file is part of the Xiaomi project.
 *
 * This source code is licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <uv_ext.h>

#include <dbus/dbus.h>
#include <dbus/dbus-hash.h>

#include "syslog.h"

#include "gdbus.h"

#define error(fmt, ...)                                       \
    do {                                                      \
        syslog(LOG_ERR, fmt, ##__VA_ARGS__); \
    } while (0)
#define warning(fmt, ...)                                           \
    do {                                                          \
        syslog(LOG_WARNING, fmt, ##__VA_ARGS__); \
    } while (0)
#define info(fmt, ...)                                        \
    do {                                                       \
        syslog(LOG_INFO, fmt, ##__VA_ARGS__); \
    } while (0)
#define debug(fmt, ...)                                        \
    do {                                                       \
        syslog(LOG_DEBUG, fmt, ##__VA_ARGS__); \
    } while (0)

struct GDBusClient {
    int ref_count;
    DBusConnection* dbus_conn;
    char* service_name;
    char* base_path;
    char* root_path;
    guint watch;
    guint added_watch;
    guint removed_watch;
    struct ptr_array* match_rules;
    DBusPendingCall* pending_call;
    DBusPendingCall* get_objects_call;
    GDBusWatchFunction connect_func;
    void* connect_data;
    GDBusWatchFunction disconn_func;
    gboolean connected;
    void* disconn_data;
    GDBusMessageFunction signal_func;
    void* signal_data;
    GDBusProxyFunction proxy_added;
    GDBusProxyFunction proxy_removed;
    GDBusProxyPropertyFilterFunction proxy_property_filter;
    GDBusProxyFilterFunction proxy_filter;
    GDBusClientFunction ready;
    void* ready_data;
    gboolean ready_called;
    GDBusPropertyFunction property_changed;
    void* user_data;
    DBusList* proxy_list;
    gboolean standard;
    gboolean getting_object_call;
    uv_async_queue_t async_queue;
    uv_thread_t main_thread;
    GDBusWatch *watcher;
};

struct GDBusProxy {
    int ref_count;
    GDBusClient* client;
    char* obj_path;
    char* interface;
    DBusHashTable* prop_list;
    guint watch;
    guint watch_non_standard;
    GDBusPropertyFunction prop_func;
    void* prop_data;
    GDBusProxyFunction removed_func;
    void* removed_data;
    DBusPendingCall* get_all_call;
    gboolean pending;
    gboolean filter_first;
    gboolean getting_all_prop;
};

GDBusWatch* new_dbus_watch(DBusConnection* connection);
void free_dbus_watch(GDBusWatch* watcher);
void dbus_watch_set_connection_state(GDBusWatch* watcher, gboolean closed);

int dbus_polkit_check_authorization(DBusConnection* conn,
    const char* action, gboolean allow_interaction,
    void (*callback)(dbus_bool_t, void*),
    void* user_data, int timeout_ms);
