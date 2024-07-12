// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  D-Bus helper library
 *
 *  Copyright (C) 2004-2011  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 */

#include <stdio.h>
#include <string.h>

#include <dbus/dbus-list.h>
#include <dbus/dbus.h>
#include <uv.h>

#include "gdbus.h"

#define info(fmt...)
#define error(fmt...)
#define debug(fmt...)

static DBusHandlerResult message_filter(DBusConnection* connection,
    DBusMessage* message, void* user_data);

static guint listener_id = 0;
static DBusList* listeners = NULL;

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

static struct filter_data* filter_data_find_match(DBusConnection* connection,
    const char* name,
    const char* owner,
    const char* path,
    const char* interface,
    const char* member,
    const char* argument)
{
    DBusList* current;

    for (current = _dbus_list_get_first_link(&listeners); current != NULL;
         current = _dbus_list_get_next_link(&listeners, current)) {
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

static struct filter_data* filter_data_find(DBusConnection* connection)
{
    DBusList* current;

    for (current = _dbus_list_get_first_link(&listeners); current != NULL;
         current = _dbus_list_get_next_link(&listeners, current)) {
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

    format_rule(data, rule, sizeof(rule));

    dbus_error_init(&err);

    dbus_bus_remove_match(data->connection, rule, &err);
    if (dbus_error_is_set(&err)) {
        error("Removing owner match rule for %s failed: %s",
            rule, err.message);
        dbus_error_free(&err);
        return FALSE;
    }

    return TRUE;
}

static void filter_data_free(struct filter_data* data)
{
    DBusList* l;

    /* Remove filter if there are no listeners left for the connection */
    if (filter_data_find(data->connection) == NULL)
        dbus_connection_remove_filter(data->connection, message_filter,
            NULL);

    for (l = _dbus_list_get_first_link(&data->callbacks); l != NULL;
         l = _dbus_list_get_next_link(&data->callbacks, l))
        free(l->data);

    _dbus_list_clear(&data->callbacks);
    dbus_remove_watch(data->connection, data->name_watch);
    free(data->name);
    free(data->owner);
    free(data->path);
    free(data->interface);
    free(data->member);
    free(data->argument);
    dbus_connection_unref(data->connection);
    free(data);
}

static struct filter_data* filter_data_get(DBusConnection* connection,
    DBusHandleMessageFunction filter,
    const char* sender,
    const char* path,
    const char* interface,
    const char* member,
    const char* argument)
{
    struct filter_data* data;
    const char *name = NULL, *owner = NULL;

    if (filter_data_find(connection) == NULL) {
        if (!dbus_connection_add_filter(connection,
                message_filter, NULL, NULL)) {
            error("dbus_connection_add_filter() failed");
            return NULL;
        }
    }

    if (sender == NULL)
        goto proceed;

    if (sender[0] == ':')
        owner = sender;
    else
        name = sender;

proceed:
    data = filter_data_find_match(connection, name, owner, path,
        interface, member, argument);
    if (data)
        return data;

    data = calloc(1, sizeof(struct filter_data));

    data->connection = dbus_connection_ref(connection);
    data->name = strdup0(name);
    data->owner = strdup0(owner);
    data->path = strdup0(path);
    data->interface = strdup0(interface);
    data->member = strdup0(member);
    data->argument = strdup0(argument);

    if (!add_match(data, filter)) {
        filter_data_free(data);
        return NULL;
    }

    _dbus_list_append(&listeners, data);

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

static void filter_data_call_and_free(struct filter_data* data)
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

    filter_data_free(data);
}

static struct filter_callback* filter_data_add_callback(
    struct filter_data* data,
    GDBusWatchFunction connect,
    GDBusWatchFunction disconnect,
    GDBusSignalFunction signal,
    GDBusDestroyFunction destroy,
    void* user_data)
{
    struct filter_callback* cb = NULL;

    cb = calloc(1, sizeof(struct filter_callback));

    cb->conn_func = connect;
    cb->disc_func = disconnect;
    cb->signal_func = signal;
    cb->destroy_func = destroy;
    cb->user_data = user_data;
    cb->id = ++listener_id;

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

    _dbus_list_remove(&listeners, data);
    filter_data_free(data);

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

static void update_name_cache(const char* name, const char* owner)
{
    DBusList* l;

    for (l = _dbus_list_get_first_link(&listeners); l != NULL;
         l = _dbus_list_get_next_link(&listeners, l)) {
        struct filter_data* data = l->data;

        if (strcmp0(data->name, name) != 0)
            continue;

        free(data->owner);
        data->owner = strdup0(owner);
    }
}

static const char* check_name_cache(const char* name)
{
    DBusList* l;

    for (l = _dbus_list_get_first_link(&listeners); l != NULL;
         l = _dbus_list_get_next_link(&listeners, l)) {
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

    update_name_cache(name, new);

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

static DBusHandlerResult message_filter(DBusConnection* connection,
    DBusMessage* message, void* user_data)
{
    struct filter_data* data;
    const char *sender, *path, *iface, *member, *arg = NULL;
    DBusList *current, *delete_listener = NULL;

    /* Only filter signals */
    if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_SIGNAL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    sender = dbus_message_get_sender(message);
    path = dbus_message_get_path(message);
    iface = dbus_message_get_interface(message);
    member = dbus_message_get_member(message);
    dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID);

    /* If sender != NULL it is always the owner */

    for (current = _dbus_list_get_first_link(&listeners); current != NULL;
         current = _dbus_list_get_next_link(&listeners, current)) {
        data = current->data;

        if (connection != data->connection)
            continue;

        if (!sender && data->owner)
            continue;

        if (data->owner && strcmp(sender, data->owner) != 0)
            continue;

        if (data->path && (!path || strcmp(path, data->path) != 0))
            continue;

        if (data->interface && (!iface || strcmp(iface, data->interface) != 0))
            continue;

        if (data->member && (!member || strcmp(member, data->member) != 0))
            continue;

        if (data->argument && (!arg || strcmp(arg, data->argument) != 0))
            continue;

        if (data->handle_func) {
            data->lock = TRUE;

            data->handle_func(connection, message, data);

            data->callbacks = data->processed;
            data->processed = NULL;
            data->lock = FALSE;
        }

        if (!data->callbacks)
            _dbus_list_prepend(&delete_listener, current);
    }

    if (delete_listener == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    for (current = _dbus_list_get_first_link(&delete_listener); current != NULL;
         current = _dbus_list_get_next_link(&delete_listener, current)) {
        DBusList* l = current->data;

        data = l->data;

        /* Has any other callback added callbacks back to this data? */
        if (data->callbacks != NULL)
            continue;

        remove_match(data);
        _dbus_list_remove_link(&listeners, l);

        filter_data_free(data);
    }

    _dbus_list_clear(&delete_listener);

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
    error("%s", err.message);
    dbus_error_free(&err);
    service_data_free(data);
done:
    dbus_message_unref(reply);
}

static void check_service(DBusConnection* connection,
    const char* name,
    struct filter_callback* callback)
{
    DBusMessage* message;
    struct service_data* data;

    data = calloc(1, sizeof(*data));
    if (data == NULL) {
        error("Can't allocate data structure");
        return;
    }

    data->conn = dbus_connection_ref(connection);
    data->name = strdup0(name);
    data->callback = callback;
    callback->data = data;

    if (uv_idle_init(uv_default_loop(), &data->handle) != 0) {
        dbus_connection_unref(connection);
        free(data);
        return;
    }

    data->handle.data = data;
    data->owner = check_name_cache(name);
    if (data->owner != NULL) {
        if (uv_idle_start(&data->handle, update_service) != 0) {
            uv_close((uv_handle_t*)&data->handle, NULL);
            dbus_connection_unref(connection);
            free(data);
        }
        return;
    }

    message = dbus_message_new_method_call(DBUS_SERVICE_DBUS,
        DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "GetNameOwner");
    if (message == NULL) {
        error("Can't allocate new message");
        dbus_connection_unref(connection);
        free(data);
        return;
    }

    dbus_message_append_args(message, DBUS_TYPE_STRING, &name,
        DBUS_TYPE_INVALID);

    if (dbus_connection_send_with_reply(connection, message, &data->call, -1)
        == FALSE) {
        error("Failed to execute method call");
        dbus_connection_unref(connection);
        free(data);
        goto done;
    }

    if (data->call == NULL) {
        error("D-Bus connection not available");
        dbus_connection_unref(connection);
        free(data);
        goto done;
    }

    if (dbus_pending_call_get_completed(data->call)) {
        service_reply(data->call, data);
    } else {
        dbus_pending_call_set_notify(data->call, service_reply, data, NULL);
    }

done:
    dbus_message_unref(message);
}

guint dbus_add_service_watch(DBusConnection* connection, const char* name,
    GDBusWatchFunction connect,
    GDBusWatchFunction disconnect,
    void* user_data, GDBusDestroyFunction destroy)
{
    struct filter_data* data;
    struct filter_callback* cb;

    if (name == NULL)
        return 0;

    data = filter_data_get(connection, service_filter,
        DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
        DBUS_INTERFACE_DBUS, "NameOwnerChanged",
        name);
    if (data == NULL)
        return 0;

    cb = filter_data_add_callback(data, connect, disconnect, NULL, destroy,
        user_data);
    if (cb == NULL)
        return 0;

    if (connect)
        check_service(connection, name, cb);

    return cb->id;
}

guint dbus_add_disconnect_watch(DBusConnection* connection, const char* name,
    GDBusWatchFunction func,
    void* user_data, GDBusDestroyFunction destroy)
{
    return dbus_add_service_watch(connection, name, NULL, func,
        user_data, destroy);
}

guint dbus_add_signal_watch(DBusConnection* connection,
    const char* sender, const char* path,
    const char* interface, const char* member,
    GDBusSignalFunction function, void* user_data,
    GDBusDestroyFunction destroy)
{
    struct filter_data* data;
    struct filter_callback* cb;

    data = filter_data_get(connection, signal_filter, sender, path,
        interface, member, NULL);
    if (data == NULL)
        return 0;

    cb = filter_data_add_callback(data, NULL, NULL, function, destroy,
        user_data);
    if (cb == NULL)
        return 0;

    if (data->name != NULL && data->name_watch == 0)
        data->name_watch = dbus_add_service_watch(connection,
            data->name, NULL,
            NULL, NULL, NULL);

    return cb->id;
}

guint dbus_add_properties_watch(DBusConnection* connection,
    const char* sender, const char* path,
    const char* interface,
    GDBusSignalFunction function, void* user_data,
    GDBusDestroyFunction destroy)
{
    struct filter_data* data;
    struct filter_callback* cb;

    data = filter_data_get(connection, signal_filter, sender, path,
        DBUS_INTERFACE_PROPERTIES, "PropertiesChanged",
        interface);
    if (data == NULL)
        return 0;

    cb = filter_data_add_callback(data, NULL, NULL, function, destroy,
        user_data);
    if (cb == NULL)
        return 0;

    if (data->name != NULL && data->name_watch == 0)
        data->name_watch = dbus_add_service_watch(connection,
            data->name, NULL,
            NULL, NULL, NULL);

    return cb->id;
}

gboolean dbus_remove_watch(DBusConnection* connection, guint id)
{
    struct filter_data* data;
    struct filter_callback* cb;
    DBusList* ldata;

    if (id == 0)
        return FALSE;

    for (ldata = _dbus_list_get_first_link(&listeners); ldata != NULL;
         ldata = _dbus_list_get_next_link(&listeners, ldata)) {
        data = ldata->data;

        cb = filter_data_find_callback(data, id);
        if (cb) {
            filter_data_remove_callback(data, cb);
            return TRUE;
        }
    }

    return FALSE;
}

void dbus_remove_all_watches(DBusConnection* connection)
{
    struct filter_data* data;

    while ((data = filter_data_find(connection))) {
        _dbus_list_remove(&listeners, data);
        filter_data_call_and_free(data);
    }
}
