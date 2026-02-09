/*
 * Copyright (C) 2025 Xiaomi Corporation
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

#include <execinfo.h>
#include <stdio.h>
#include <string.h>

#include <dbus/dbus-list.h>
#include <dbus/dbus.h>
#include <uv.h>

#include "gdbus-internal.h"

static DBusHandlerResult message_filter(DBusConnection* connection,
    DBusMessage* message, void* user_data);

struct GDBusWatch {
    DBusConnection* conn;
    guint serial;
    DBusList* list;
    gboolean conn_closed;
};

struct service_data {
    DBusConnection* conn;
    DBusPendingCall* call;
    char* name;
    const char* owner;
    struct filter_callback* callback;
    uv_idle_t handle;
};

struct filter_callback {
    GDBusWatchFunction conn_func;
    GDBusWatchFunction disc_func;
    GDBusSignalFunction signal_func;
    GDBusDestroyFunction destroy_func;
    struct service_data* data;
    void* user_data;
    guint id;
};

struct filter_data {
    DBusConnection* connection;
    DBusHandleMessageFunction handle_func;
    char* name;
    char* owner;
    char* path;
    char* interface;
    char* member;
    char* argument;
    DBusList* callbacks;
    DBusList* processed;
    guint name_watch;
    gboolean lock;
    gboolean registered;
    GDBusWatch* watcher;
};

static int strcmp0(const char* str1, const char* str2)
{
    if (str1 == NULL)
        return -(str1 != str2);

    if (str2 == NULL)
        return -(str1 != str2);

    return strcmp(str1, str2);
}

static char* strdup0(const char* str)
{
    if (str)
        return strdup(str);

    return NULL;
}

static int get_watch_serial(GDBusWatch* watcher)
{
    return ++watcher->serial;
}

static struct filter_data* filter_data_find_match(DBusConnection* connection,
    DBusList* listener_list, const char* name, const char* owner,
    const char* path, const char* interface, const char* member, const char* argument)
{
    DBusList* current;

    for (current = _dbus_list_get_first_link(&listener_list); current != NULL;
         current = _dbus_list_get_next_link(&listener_list, current)) {
        struct filter_data* data = current->data;

        if (connection != data->connection)
            continue;

        if (strcmp0(name, data->name) != 0)
            continue;

        if (strcmp0(owner, data->owner) != 0)
            continue;

        if (strcmp0(path, data->path) != 0)
            continue;

        if (strcmp0(interface, data->interface) != 0)
            continue;

        if (strcmp0(member, data->member) != 0)
            continue;

        if (strcmp0(argument, data->argument) != 0)
            continue;

        return data;
    }

    return NULL;
}

static struct filter_data* filter_data_find(DBusConnection* connection, DBusList* listener_list)
{
    DBusList* current;

    for (current = _dbus_list_get_first_link(&listener_list); current != NULL;
         current = _dbus_list_get_next_link(&listener_list, current)) {
        struct filter_data* data = current->data;

        if (connection != data->connection)
            continue;

        return data;
    }

    return NULL;
}

static void format_rule(struct filter_data* data, char* rule, size_t size)
{
    const char* sender;
    int offset;

    offset = snprintf(rule, size, "type='signal'");
    sender = data->name ?: data->owner;

    if (sender)
        offset += snprintf(rule + offset, size - offset,
            ",sender='%s'", sender);
    if (data->path)
        offset += snprintf(rule + offset, size - offset,
            ",path='%s'", data->path);
    if (data->interface)
        offset += snprintf(rule + offset, size - offset,
            ",interface='%s'", data->interface);
    if (data->member)
        offset += snprintf(rule + offset, size - offset,
            ",member='%s'", data->member);
    if (data->argument)
        snprintf(rule + offset, size - offset,
            ",arg0='%s'", data->argument);
}

static gboolean add_match(struct filter_data* data,
    DBusHandleMessageFunction filter)
{
    DBusError err;
    char rule[DBUS_MAXIMUM_MATCH_RULE_LENGTH];

    format_rule(data, rule, sizeof(rule));
    dbus_error_init(&err);

    dbus_bus_add_match(data->connection, rule, &err);
    if (dbus_error_is_set(&err)) {
        error("Adding match rule \"%s\" failed: %s", rule,
            err.message);
        dbus_error_free(&err);
        return FALSE;
    }

    data->handle_func = filter;
    data->registered = TRUE;

    return TRUE;
}

static gboolean remove_match(struct filter_data* data)
{
    DBusError err;
    char rule[DBUS_MAXIMUM_MATCH_RULE_LENGTH];

    if (data->watcher->conn_closed) {
        /* If the connection is disconnected, we don't need to remove the match */
        return TRUE;
    }

    format_rule(data, rule, sizeof(rule));

    dbus_error_init(&err);

    dbus_bus_remove_match(data->connection, rule, &err);
    if (dbus_error_is_set(&err)) {
        error("GDBUS Removing owner match rule for %s failed: %s",
            rule, err.message);
        dump_stack();
        dbus_error_free(&err);
        return FALSE;
    }

    return TRUE;
}

static void filter_data_free(struct filter_data* data, DBusList* listener_list)
{
    DBusList* l;

    /* Remove filter if there are no listeners left for the connection */
    if (filter_data_find(data->connection, listener_list) == NULL) {
        dbus_connection_remove_filter(data->connection, message_filter,
            data->watcher);
    }

    for (l = _dbus_list_get_first_link(&data->callbacks); l != NULL;
         l = _dbus_list_get_next_link(&data->callbacks, l))
        free(l->data);

    _dbus_list_clear(&data->callbacks);
    dbus_remove_watch(data->watcher, data->name_watch);
    free(data->name);
    free(data->owner);
    free(data->path);
    free(data->interface);
    free(data->member);
    free(data->argument);

    /* no listeners left for the connection */
    if (_dbus_list_get_last(&listener_list) == NULL) {
        free_dbus_watch(data->watcher);
    }
    dbus_connection_unref(data->connection);
    free(data);
}

static struct filter_data* filter_data_get(GDBusWatch* watcher,
    DBusHandleMessageFunction filter,
    const char* sender, const char* path, const char* interface,
    const char* member, const char* argument)
{
    struct filter_data* data;
    const char *name = NULL, *owner = NULL;

    if (filter_data_find(watcher->conn, watcher->list) == NULL) {
        if (!dbus_connection_add_filter(watcher->conn,
                message_filter, watcher, NULL)) {
            error("dbus_connection_add_filter() failed");
            return NULL;
        }
    }

    if (sender != NULL) {
        if (sender[0] == ':')
            owner = sender;
        else
            name = sender;
    }

    data = filter_data_find_match(watcher->conn, watcher->list, name, owner, path,
        interface, member, argument);
    if (data)
        return data;

    data = calloc(1, sizeof(struct filter_data));

    data->connection = dbus_connection_ref(watcher->conn);
    data->name = strdup0(name);
    data->owner = strdup0(owner);
    data->path = strdup0(path);
    data->interface = strdup0(interface);
    data->member = strdup0(member);
    data->argument = strdup0(argument);
    data->watcher = watcher;

    if (!add_match(data, filter)) {
        dbus_connection_unref(data->connection);
        free(data->name);
        free(data->owner);
        free(data->path);
        free(data->interface);
        free(data->member);
        free(data->argument);
        free(data);
        return NULL;
    }

    _dbus_list_append(&watcher->list, data);
    return data;
}

static struct filter_callback* filter_data_find_callback(
    struct filter_data* data,
    guint id)
{
    DBusList* l;

    for (l = _dbus_list_get_first_link(&data->callbacks); l != NULL;
         l = _dbus_list_get_next_link(&data->callbacks, l)) {
        struct filter_callback* cb = l->data;
        if (cb->id == id)
            return cb;
    }
    for (l = _dbus_list_get_first_link(&data->processed); l != NULL;
         l = _dbus_list_get_next_link(&data->processed, l)) {
        struct filter_callback* cb = l->data;
        if (cb->id == id)
            return cb;
    }

    return NULL;
}

static void filter_data_call_and_free(struct filter_data* data, DBusList* listener_list)
{
    DBusList* l;

    for (l = _dbus_list_get_first_link(&data->callbacks); l != NULL;
         l = _dbus_list_get_next_link(&data->callbacks, l)) {
        struct filter_callback* cb = l->data;
        if (cb->disc_func)
            cb->disc_func(data->connection, cb->user_data);
        if (cb->destroy_func)
            cb->destroy_func(cb->user_data);
    }

    filter_data_free(data, listener_list);
}

static struct filter_callback* filter_data_add_callback(
    struct filter_data* data,
    GDBusWatchFunction connect,
    GDBusWatchFunction disconnect,
    GDBusSignalFunction signal,
    GDBusDestroyFunction destroy,
    guint watch_id,
    void* user_data)
{
    struct filter_callback* cb = NULL;

    cb = calloc(1, sizeof(struct filter_callback));

    cb->conn_func = connect;
    cb->disc_func = disconnect;
    cb->signal_func = signal;
    cb->destroy_func = destroy;
    cb->user_data = user_data;
    cb->id = watch_id;

    if (data->lock)
        _dbus_list_append(&data->processed, cb);
    else
        _dbus_list_append(&data->callbacks, cb);

    return cb;
}

static void close_cb(uv_handle_t* handle)
{
    free(handle->data);
}

static void service_data_free(struct service_data* data)
{
    struct filter_callback* callback = data->callback;

    dbus_connection_unref(data->conn);

    if (data->call)
        dbus_pending_call_unref(data->call);

    free(data->name);
    callback->data = NULL;

    uv_close((uv_handle_t*)&data->handle, close_cb);
}

/* Returns TRUE if data is freed */
static gboolean filter_data_remove_callback(struct filter_data* data,
    struct filter_callback* cb)
{
    _dbus_list_remove(&data->callbacks, cb);
    _dbus_list_remove(&data->processed, cb);

    /* Cancel pending operations */
    if (cb->data) {
        if (cb->data->call)
            dbus_pending_call_cancel(cb->data->call);
        service_data_free(cb->data);
    }

    if (cb->destroy_func)
        cb->destroy_func(cb->user_data);

    free(cb);

    /* Don't remove the filter if other callbacks exist or data is lock
     * processing callbacks */
    if (data->callbacks || data->lock)
        return FALSE;

    if (data->registered && !remove_match(data))
        return FALSE;

    _dbus_list_remove(&data->watcher->list, data);
    filter_data_free(data, data->watcher->list);

    return TRUE;
}

static DBusHandlerResult signal_filter(DBusConnection* connection,
    DBusMessage* message, void* user_data)
{
    struct filter_data* data = user_data;
    struct filter_callback* cb;

    while (data->callbacks) {
        cb = data->callbacks->data;

        if (cb->signal_func && !cb->signal_func(connection, message, cb->user_data)) {
            if (filter_data_remove_callback(data, cb))
                break;

            continue;
        }

        /* Check if the watch was removed/freed by the callback
         * function */
        if (_dbus_list_find_last(&data->callbacks, cb) == NULL)
            continue;

        _dbus_list_remove(&data->callbacks, cb);
        _dbus_list_append(&data->processed, cb);
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void update_name_cache(DBusList* listener_list, const char* name, const char* owner)
{
    DBusList* l;

    for (l = _dbus_list_get_first_link(&listener_list); l != NULL;
         l = _dbus_list_get_next_link(&listener_list, l)) {
        struct filter_data* data = l->data;

        if (strcmp0(data->name, name) != 0)
            continue;

        free(data->owner);
        data->owner = strdup0(owner);
    }
}

static const char* check_name_cache(DBusList* listener_list, const char* name)
{
    DBusList* l;

    for (l = _dbus_list_get_first_link(&listener_list); l != NULL;
         l = _dbus_list_get_next_link(&listener_list, l)) {
        struct filter_data* data = l->data;

        if (strcmp0(data->name, name) != 0)
            continue;

        return data->owner;
    }

    return NULL;
}

static DBusHandlerResult service_filter(DBusConnection* connection,
    DBusMessage* message, void* user_data)
{
    struct filter_data* data = user_data;
    struct filter_callback* cb;
    char *name, *old, *new;

    if (!dbus_message_get_args(message, NULL,
            DBUS_TYPE_STRING, &name,
            DBUS_TYPE_STRING, &old,
            DBUS_TYPE_STRING, &new,
            DBUS_TYPE_INVALID)) {
        error("Invalid arguments for NameOwnerChanged signal");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    update_name_cache(data->watcher->list, name, new);

    while (data->callbacks) {
        cb = data->callbacks->data;

        if (*new == '\0') {
            if (cb->disc_func)
                cb->disc_func(connection, cb->user_data);
        } else {
            if (cb->conn_func)
                cb->conn_func(connection, cb->user_data);
        }

        /* Check if the watch was removed/freed by the callback
         * function */
        if (_dbus_list_find_last(&data->callbacks, cb) == NULL)
            continue;

        /* Only auto remove if it is a bus name watch */
        if (data->argument[0] == ':' && (cb->conn_func == NULL || cb->disc_func == NULL)) {
            if (filter_data_remove_callback(data, cb))
                break;

            continue;
        }

        _dbus_list_remove(&data->callbacks, cb);
        _dbus_list_append(&data->processed, cb);
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static bool process_signal_message(DBusMessage* message,
    const char** sender, const char** path,
    const char** iface, const char** member,
    const char** arg)
{
    if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_SIGNAL)
        return false;

    *sender = dbus_message_get_sender(message);
    *path = dbus_message_get_path(message);
    *iface = dbus_message_get_interface(message);
    *member = dbus_message_get_member(message);
    if (!dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, arg, DBUS_TYPE_INVALID))
        *arg = NULL;

    return true;
}
static int filter_data_match(const char* str1, const char* data_param)
{
    if (data_param == NULL)
        return 0;

    if (str1 == NULL)
        return -1;

    return strcmp(str1, data_param);
}

static gboolean match_listener_data(struct filter_data* data, DBusConnection* connection,
    const char* sender, const char* path,
    const char* iface, const char* member, const char* arg)
{
    if (connection != data->connection)
        return FALSE;

    if (filter_data_match(sender, data->owner) != 0)
        return FALSE;

    if (filter_data_match(path, data->path) != 0)
        return FALSE;

    if (filter_data_match(iface, data->interface) != 0)
        return FALSE;

    if (filter_data_match(member, data->member) != 0)
        return FALSE;

    if (filter_data_match(arg, data->argument) != 0)
        return FALSE;

    return TRUE;
}

static void process_listener_callbacks(struct filter_data* data, DBusConnection* connection,
    DBusMessage* message)
{

    if (!data->handle_func)
        return;

    data->lock = TRUE;
    data->handle_func(connection, message, data);
    data->callbacks = data->processed;
    data->processed = NULL;
    data->lock = FALSE;
}

static void process_listeners(DBusList* listener_list, DBusConnection* connection,
    DBusMessage* message, DBusList** delete_listener, const char* sender,
    const char* path, const char* iface, const char* member, const char* arg)
{
    DBusList* current;
    struct filter_data* data;

    for (current = _dbus_list_get_first_link(&listener_list); current;
         current = _dbus_list_get_next_link(&listener_list, current)) {
        data = current->data;

        if (!match_listener_data(data, connection, sender,
                path, iface, member, arg))
            continue;

        process_listener_callbacks(data, connection, message);

        if (!data->callbacks)
            _dbus_list_prepend(delete_listener, current);
    }
}

static void cleanup_listeners(DBusList* delete_listener, GDBusWatch* watcher)
{
    DBusList* current;
    struct filter_data* data;

    for (current = _dbus_list_get_first_link(&delete_listener); current;
         current = _dbus_list_get_next_link(&delete_listener, current)) {
        DBusList* l = current->data;
        data = l->data;

        /* Has any other callback added callbacks back to this data? */
        if (data->callbacks)
            continue;

        remove_match(data);
        _dbus_list_remove_link(&watcher->list, l);
        filter_data_free(data, watcher->list);
    }
    _dbus_list_clear(&delete_listener);
}

static DBusHandlerResult message_filter(DBusConnection* connection,
    DBusMessage* message, void* user_data)
{
    const char *sender, *path, *iface, *member, *arg = NULL;
    DBusList* delete_listener = NULL;
    GDBusWatch* watcher = user_data;

    if (!process_signal_message(message, &sender, &path,
            &iface, &member, &arg))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    process_listeners(watcher->list, connection, message, &delete_listener,
        sender, path, iface, member, arg);

    if (delete_listener)
        cleanup_listeners(delete_listener, watcher);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void update_service(uv_idle_t* handle)
{
    struct service_data* data = handle->data;
    struct filter_callback* cb = data->callback;
    DBusConnection* conn;

    conn = dbus_connection_ref(data->conn);
    service_data_free(data);

    if (cb->conn_func)
        cb->conn_func(conn, cb->user_data);

    dbus_connection_unref(conn);
}

static void service_reply(DBusPendingCall* call, void* user_data)
{
    struct service_data* data = user_data;
    DBusMessage* reply;
    DBusError err;

    reply = dbus_pending_call_steal_reply(call);
    if (reply == NULL)
        return;

    dbus_error_init(&err);

    if (dbus_set_error_from_message(&err, reply))
        goto fail;

    if (dbus_message_get_args(reply, &err, DBUS_TYPE_STRING,
            &data->owner, DBUS_TYPE_INVALID)
        == FALSE)
        goto fail;

    update_service(&data->handle);

    goto done;

fail:
    error("service_reply fail: %s", err.message);
    dbus_error_free(&err);
    service_data_free(data);
done:
    dbus_message_unref(reply);
}

static struct service_data* create_service_data(DBusConnection* conn,
    const char* name,
    struct filter_callback* cb)
{
    struct service_data* data = calloc(1, sizeof(*data));
    if (!data) {
        error("%s: malloc failed", __func__);
        return NULL;
    }

    data->conn = dbus_connection_ref(conn);
    data->name = strdup0(name);
    data->callback = cb;
    cb->data = data;
    return data;
}

static bool init_uv_handle(struct service_data* data)
{
    if (uv_idle_init(uv_default_loop(), &data->handle) != 0) {
        error("%s: uv_idle_init failed", __func__);
        return false;
    }
    data->handle.data = data;
    return true;
}

static gboolean send_name_owner_request(struct service_data* data, const char* name)
{
    DBusMessage* msg = dbus_message_new_method_call(DBUS_SERVICE_DBUS,
        DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "GetNameOwner");
    if (!msg) {
        error("%s: dbus_message_new_method_call failed", __func__);
        return FALSE;
    }

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
    if (!dbus_connection_send_with_reply(data->conn, msg, &data->call, -1)) {
        error("%s: dbus_connection_send_with_reply failed", __func__);
        dbus_message_unref(msg);
        return FALSE;
    }

    if (!data->call) {
        error("%s: dbus reply failed with empty pendingcall", __func__);
        dbus_message_unref(msg);
        return FALSE;
    }

    if (dbus_pending_call_get_completed(data->call)) {
        service_reply(data->call, data);
    } else {
        dbus_pending_call_set_notify(data->call, service_reply, data, NULL);
    }

    dbus_message_unref(msg);
    return TRUE;
}

static void check_service(DBusConnection* conn, const char* name,
    struct filter_callback* cb, DBusList* listener_list)
{
    struct service_data* data = create_service_data(conn, name, cb);
    if (!data)
        return;

    if (!init_uv_handle(data)) {
        dbus_connection_unref(data->conn);
        free(data->name);
        free(data);
        return;
    }

    data->owner = check_name_cache(listener_list, name);
    if (data->owner) {
        if (uv_idle_start(&data->handle, update_service) != 0) {
            error("%s: uv_idle_start failed:", __func__);
            uv_close((uv_handle_t*)&data->handle, NULL);
            dbus_connection_unref(data->conn);
            free(data->name);
            free(data);
        }
        return;
    }

    if (send_name_owner_request(data, name) == FALSE) {
        uv_close((uv_handle_t*)&data->handle, NULL);
        dbus_connection_unref(data->conn);
        free(data->name);
        free(data);
        return;
    }
}

guint dbus_add_service_watch(GDBusWatch* watcher, const char* name,
    GDBusWatchFunction connect,
    GDBusWatchFunction disconnect,
    void* user_data, GDBusDestroyFunction destroy)
{
    struct filter_data* data;
    struct filter_callback* cb;

    if (name == NULL)
        return 0;

    data = filter_data_get(watcher, service_filter, DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
        DBUS_INTERFACE_DBUS, "NameOwnerChanged", name);
    if (data == NULL)
        return 0;

    cb = filter_data_add_callback(data, connect, disconnect, NULL, destroy,
        get_watch_serial(watcher), user_data);
    if (cb == NULL)
        return 0;

    if (connect)
        check_service(watcher->conn, name, cb, watcher->list);

    return cb->id;
}

guint dbus_add_service_disconnect_watch(GDBusWatch* watcher, const char* name,
    GDBusWatchFunction func, void* user_data, GDBusDestroyFunction destroy)
{
    return dbus_add_service_watch(watcher, name, NULL, func, user_data, destroy);
}

guint dbus_add_signal_watch(GDBusWatch* watcher,
    const char* sender, const char* path,
    const char* interface, const char* member,
    GDBusSignalFunction function, void* user_data,
    GDBusDestroyFunction destroy)
{
    struct filter_data* data;
    struct filter_callback* cb;

    data = filter_data_get(watcher, signal_filter, sender, path,
        interface, member, NULL);
    if (data == NULL)
        return 0;

    cb = filter_data_add_callback(data, NULL, NULL, function, destroy,
        get_watch_serial(watcher), user_data);
    if (cb == NULL)
        return 0;

    if (data->name != NULL && data->name_watch == 0)
        data->name_watch = dbus_add_service_watch(watcher,
            data->name, NULL,
            NULL, NULL, NULL);

    return cb->id;
}

guint dbus_add_properties_watch(GDBusWatch* watcher,
    const char* sender, const char* path,
    const char* interface,
    GDBusSignalFunction function, void* user_data,
    GDBusDestroyFunction destroy)
{
    struct filter_data* data;
    struct filter_callback* cb;

    data = filter_data_get(watcher, signal_filter, sender, path,
        DBUS_INTERFACE_PROPERTIES, "PropertiesChanged", interface);
    if (data == NULL)
        return 0;

    cb = filter_data_add_callback(data, NULL, NULL, function, destroy,
        get_watch_serial(watcher), user_data);
    if (cb == NULL)
        return 0;

    if (data->name != NULL && data->name_watch == 0)
        data->name_watch = dbus_add_service_watch(watcher,
            data->name, NULL,
            NULL, NULL, NULL);

    return cb->id;
}

gboolean dbus_remove_watch(GDBusWatch* watcher, guint id)
{
    struct filter_data* data;
    struct filter_callback* cb;
    DBusList* ldata;

    if (id == 0)
        return FALSE;

    for (ldata = _dbus_list_get_first_link(&watcher->list); ldata != NULL;
         ldata = _dbus_list_get_next_link(&watcher->list, ldata)) {
        data = ldata->data;

        cb = filter_data_find_callback(data, id);
        if (cb) {
            filter_data_remove_callback(data, cb);
            return TRUE;
        }
    }

    return FALSE;
}

void dbus_remove_all_watches(GDBusWatch* watcher)
{
    struct filter_data* data;

    while ((data = filter_data_find(watcher->conn, watcher->list))) {
        _dbus_list_remove(&watcher->list, data);
        filter_data_call_and_free(data, watcher->list);
    }
}

GDBusWatch* new_dbus_watch(DBusConnection* connection)
{
    GDBusWatch* watcher;

    watcher = calloc(1, sizeof(GDBusWatch));
    if (watcher == NULL)
        return NULL;

    watcher->conn = dbus_connection_ref(connection);
    watcher->list = NULL;
    watcher->serial = 0;
    watcher->conn_closed = FALSE;

    return watcher;
}
void free_dbus_watch(GDBusWatch* watcher)
{
    dbus_connection_unref(watcher->conn);
    free(watcher);
}

guint dbus_client_add_service_watch(GDBusClient* client, const char* name,
    GDBusWatchFunction connect, GDBusWatchFunction disconnect,
    void* user_data, GDBusDestroyFunction destroy)
{
    return dbus_add_service_watch(client->watcher, name, connect, disconnect,
        user_data, destroy);
}

guint dbus_client_add_service_disconnect_watch(GDBusClient* client, const char* name,
    GDBusWatchFunction function, void* user_data, GDBusDestroyFunction destroy)
{
    return dbus_add_service_disconnect_watch(client->watcher, name, function, user_data, destroy);
}

guint dbus_client_add_signal_watch(GDBusClient* client, const char* sender,
    const char* path, const char* interface, const char* member,
    GDBusSignalFunction function, void* user_data, GDBusDestroyFunction destroy)
{
    return dbus_add_signal_watch(client->watcher, sender, path, interface,
        member, function, user_data, destroy);
}

guint dbus_client_add_properties_watch(GDBusClient* client,
    const char* sender, const char* path, const char* interface,
    GDBusSignalFunction function, void* user_data, GDBusDestroyFunction destroy)
{
    return dbus_add_properties_watch(client->watcher, sender, path, interface,
        function, user_data, destroy);
}

gboolean dbus_client_remove_watch(GDBusClient* client, guint tag)
{
    return dbus_remove_watch(client->watcher, tag);
}

void dbus_client_remove_all_watches(GDBusClient* client)
{
    dbus_remove_all_watches(client->watcher);
}

void dbus_watch_set_connection_state(GDBusWatch* watcher, gboolean closed)
{
    watcher->conn_closed = closed;
}
