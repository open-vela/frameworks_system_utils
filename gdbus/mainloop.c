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

#include <dbus/dbus.h>
#include <poll.h>
#include <stdlib.h>
#include <uv.h>

#include "gdbus-internal.h"

struct idle_handler {
    uv_idle_t handle;
    DBusConnection* conn;
};

struct timeout_handler {
    uv_timer_t handle;
    DBusTimeout* timeout;
};

struct dbus_watch_info {
    uv_poll_t* handle;
    DBusWatch* read_watch;
    DBusWatch* write_watch;
    DBusConnection* conn;
};

static void close_cb(uv_handle_t* handle)
{
    free(handle->data);
}

static void message_dispatch(uv_idle_t* handle)
{
    struct idle_handler* handler = handle->data;
    DBusConnection* conn = handler->conn;

    /* Dispatch messages */
    while (dbus_connection_dispatch(conn) == DBUS_DISPATCH_DATA_REMAINS)
        ;

    dbus_connection_unref(conn);

    uv_close((uv_handle_t*)handle, close_cb);
}

static inline void queue_dispatch(DBusConnection* conn,
    DBusDispatchStatus status)
{
    if (status == DBUS_DISPATCH_DATA_REMAINS) {
        struct idle_handler* handler;

        handler = calloc(1, sizeof(struct idle_handler));
        if (handler == NULL) {
            return;
        }

        if (uv_idle_init(uv_default_loop(), &handler->handle) != 0) {
            free(handler);
            return;
        }

        handler->conn = dbus_connection_ref(conn);
        handler->handle.data = handler;
        if (uv_idle_start(&handler->handle, message_dispatch) != 0) {
            dbus_connection_unref(conn);
            uv_close((uv_handle_t*)&handler->handle, close_cb);
            free(handler);
        }
    }
}

static void watch_func(uv_poll_t* handle, int state, int events)
{
    struct dbus_watch_info* info = handle->data;
    unsigned int flags = 0;
    DBusDispatchStatus status;
    DBusConnection* conn;

    if (events & UV_READABLE)
        flags |= DBUS_WATCH_READABLE;
    if (events & UV_WRITABLE)
        flags |= DBUS_WATCH_WRITABLE;
    if (events & UV_DISCONNECT)
        flags |= DBUS_WATCH_HANGUP;
    if (events & POLLERR)
        flags |= DBUS_WATCH_ERROR;

    /* Protect connection from being destroyed by dbus_watch_handle */
    conn = dbus_connection_ref(info->conn);

    if (flags & DBUS_WATCH_READABLE && info->read_watch != NULL)
        dbus_watch_handle(info->read_watch, flags);
    if (flags & DBUS_WATCH_WRITABLE && info->write_watch != NULL)
        dbus_watch_handle(info->write_watch, flags);

    status = dbus_connection_get_dispatch_status(conn);
    queue_dispatch(conn, status);

    dbus_connection_unref(conn);
}

static void close_watch_info_handler_cb(uv_handle_t* handle)
{
    struct dbus_watch_info* info = handle->data;

    if (info == NULL)
        return;

    if (info->read_watch) {
        dbus_watch_set_data(info->read_watch, NULL, NULL);
        info->read_watch = NULL;
    }
    if (info->write_watch) {
        dbus_watch_set_data(info->write_watch, NULL, NULL);
        info->write_watch = NULL;
    }

    free(info->handle);
    info->handle = NULL;
}

static void watch_info_free_read(void* data)
{
    struct dbus_watch_info* info = data;

    if (info != NULL && info->read_watch != NULL) {
        info->read_watch = NULL;

        /**
         * libdbus maybe call read watch free cb, not from remove watch.
         * need close uv hander when read and watch both null.
         */
        if (info->write_watch == NULL && info->handle != NULL)
            uv_close((uv_handle_t*)info->handle, close_watch_info_handler_cb);
    }
}

static void watch_info_free_write(void* data)
{
    struct dbus_watch_info* info = data;

    if (info != NULL && info->write_watch != NULL) {
        info->write_watch = NULL;

        /**
         * libdbus maybe call write watch free cb, not from remove watch.
         * need close uv hander when read and watch both null.
         */
        if (info->read_watch == NULL && info->handle != NULL)
            uv_close((uv_handle_t*)info->handle, close_watch_info_handler_cb);
    }
}

static dbus_bool_t add_watch(DBusWatch* watch, void* data)
{
    struct dbus_watch_info* watch_info = data;
    int cond = UV_DISCONNECT;
    int flags = 0;

    if (!dbus_watch_get_enabled(watch))
        return TRUE;

    flags = dbus_watch_get_flags(watch);
    if (flags & DBUS_WATCH_READABLE) {
        dbus_watch_set_data(watch, watch_info, watch_info_free_read);
        watch_info->read_watch = watch;
    }
    if (flags & DBUS_WATCH_WRITABLE) {
        dbus_watch_set_data(watch, watch_info, watch_info_free_write);
        watch_info->write_watch = watch;
    }

    if (watch_info->read_watch != NULL && watch != watch_info->read_watch)
        flags |= dbus_watch_get_flags(watch_info->read_watch);
    if (watch_info->write_watch != NULL && watch != watch_info->write_watch)
        flags |= dbus_watch_get_flags(watch_info->write_watch);

    if (flags & DBUS_WATCH_READABLE)
        cond |= UV_READABLE;
    if (flags & DBUS_WATCH_WRITABLE)
        cond |= UV_WRITABLE;

    if (!watch_info->handle) {
        watch_info->handle = calloc(1, sizeof(uv_poll_t));
        if (!watch_info->handle)
            return FALSE;

        int fd = dbus_watch_get_unix_fd(watch);
        if (uv_poll_init(uv_default_loop(), watch_info->handle, fd) != 0) {
            free(watch_info->handle);
            watch_info->handle = NULL;
            dbus_watch_set_data(watch, NULL, NULL);
            return FALSE;
        }

        watch_info->handle->data = watch_info;
    }

    if (uv_poll_start(watch_info->handle, cond, watch_func) != 0) {
        uv_close((uv_handle_t*)watch_info->handle, close_watch_info_handler_cb);
        return FALSE;
    }

    return TRUE;
}

static void remove_watch(DBusWatch* watch, void* data)
{
    int flags = 0;
    int cond = 0;
    struct dbus_watch_info* info = data;

    /* If the watch is still enabled, we treat this as a toggle */
    if (dbus_watch_get_enabled(watch))
        return;

    if (info->read_watch == watch && info->write_watch != NULL) {
        /* remove watch is read, keep write flag if write watch is valid */
        flags = dbus_watch_get_flags(info->write_watch);
    } else if (info->write_watch == watch && info->read_watch != NULL) {
        /* remove watch is write, keep read flag if read watch is valid */
        flags = dbus_watch_get_flags(info->read_watch);
    }

    if (flags & DBUS_WATCH_READABLE)
        cond |= UV_READABLE;
    if (flags & DBUS_WATCH_WRITABLE)
        cond |= UV_WRITABLE;

    if (cond != 0) {
        cond |= UV_DISCONNECT;
        if (uv_poll_start(info->handle, cond, watch_func) != 0) {
            uv_close((uv_handle_t*)info->handle, close_watch_info_handler_cb);
            return;
        }
    }

    /* will trigger watch_info_free_read/write() */
    dbus_watch_set_data(watch, NULL, NULL);
}

static void watch_toggled(DBusWatch* watch, void* data)
{
    /* Because we just exit on OOM, enable/disable is
     * no different from add/remove */
    if (dbus_watch_get_enabled(watch))
        add_watch(watch, data);
    else
        remove_watch(watch, data);
}

static void timeout_handler_dispatch(uv_timer_t* handle)
{
    struct timeout_handler* handler = handle->data;

    /* if not enabled should not be polled by the main loop */
    if (dbus_timeout_get_enabled(handler->timeout))
        dbus_timeout_handle(handler->timeout);
}

static void timeout_handler_free(void* data)
{
    struct timeout_handler* handler = data;

    uv_close((uv_handle_t*)&handler->handle, close_cb);
}

static dbus_bool_t add_timeout(DBusTimeout* timeout, void* data)
{
    int interval = dbus_timeout_get_interval(timeout);
    struct timeout_handler* handler;

    if (!dbus_timeout_get_enabled(timeout))
        return TRUE;

    handler = calloc(1, sizeof(struct timeout_handler));
    if (handler == NULL)
        return FALSE;

    handler->timeout = timeout;

    dbus_timeout_set_data(timeout, handler, timeout_handler_free);

    if (uv_timer_init(uv_default_loop(), &handler->handle) != 0) {
        goto errout;
    }

    handler->handle.data = handler;
    if (uv_timer_start(&handler->handle, timeout_handler_dispatch, interval, 0) != 0) {
        uv_close((uv_handle_t*)&handler->handle, close_cb);
        goto errout;
    }

    return TRUE;
errout:
    free(handler);
    dbus_timeout_set_data(timeout, NULL, NULL);
    return FALSE;
}

static void remove_timeout(DBusTimeout* timeout, void* data)
{
    /* will trigger timeout_handler_free() */
    dbus_timeout_set_data(timeout, NULL, NULL);
}

static void timeout_toggled(DBusTimeout* timeout, void* data)
{
    if (dbus_timeout_get_enabled(timeout))
        add_timeout(timeout, data);
    else
        remove_timeout(timeout, data);
}

static void dispatch_status(DBusConnection* conn,
    DBusDispatchStatus status, void* data)
{
    if (!dbus_connection_get_is_connected(conn))
        return;

    queue_dispatch(conn, status);
}

static void dbus_watch_info_free(void* data)
{
    struct dbus_watch_info* info = data;

    if (info != NULL) {
        dbus_connection_unref(info->conn);
        free(info);
    }
}

static inline void setup_dbus_with_main_loop(DBusConnection* conn)
{
    struct dbus_watch_info* info = calloc(1, sizeof(struct dbus_watch_info));
    if (info == NULL)
        return;
    info->conn = dbus_connection_ref(conn);

    dbus_connection_set_watch_functions(conn, add_watch, remove_watch,
        watch_toggled, info, dbus_watch_info_free);

    dbus_connection_set_timeout_functions(conn, add_timeout, remove_timeout,
        timeout_toggled, NULL, NULL);

    dbus_connection_set_dispatch_status_function(conn, dispatch_status,
        NULL, NULL);
}

static gboolean setup_bus(DBusConnection* conn, const char* name,
    DBusError* error)
{
    gboolean result;
    DBusDispatchStatus status;

    if (name != NULL) {
        result = dbus_request_name(conn, name, error);

        if (error != NULL) {
            if (dbus_error_is_set(error) == TRUE)
                return FALSE;
        }

        if (result == FALSE)
            return FALSE;
    }

    setup_dbus_with_main_loop(conn);

    status = dbus_connection_get_dispatch_status(conn);
    queue_dispatch(conn, status);

    return TRUE;
}

DBusConnection* dbus_setup_bus(DBusBusType type, const char* name,
    DBusError* error)
{
    DBusConnection* conn;

    conn = dbus_bus_get(type, error);

    if (error != NULL) {
        if (dbus_error_is_set(error) == TRUE)
            return NULL;
    }

    if (conn == NULL)
        return NULL;

    if (setup_bus(conn, name, error) == FALSE) {
        dbus_connection_unref(conn);
        return NULL;
    }

    return conn;
}

DBusConnection* dbus_setup_private(DBusBusType type, const char* name,
    DBusError* error)
{
    DBusConnection* conn;

    conn = dbus_bus_get_private(type, error);

    if (error != NULL) {
        if (dbus_error_is_set(error) == TRUE)
            return NULL;
    }

    if (conn == NULL)
        return NULL;

    if (setup_bus(conn, name, error) == FALSE) {
        dbus_connection_close(conn);
        dbus_connection_unref(conn);
        return NULL;
    }

    return conn;
}

gboolean dbus_request_name(DBusConnection* connection, const char* name,
    DBusError* error)
{
    int result;

    result = dbus_bus_request_name(connection, name,
        DBUS_NAME_FLAG_DO_NOT_QUEUE, error);

    if (error != NULL) {
        if (dbus_error_is_set(error) == TRUE)
            return FALSE;
    }

    if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        if (error != NULL)
            dbus_set_error(error, name, "Name already in use");

        return FALSE;
    }

    return TRUE;
}

struct disconnect_data {
    GDBusWatchFunction function;
    void* user_data;
    GDBusWatch* watcher;
};

static gboolean disconnected_signal(DBusConnection* conn,
    DBusMessage* msg, void* data)
{
    struct disconnect_data* dc_data = data;

    info("Got disconnected from the system message bus");

    dc_data->function(conn, dc_data->user_data);

    dbus_connection_unref(conn);

    dbus_watch_set_connection_state(dc_data->watcher, TRUE);

    return FALSE;
}

gboolean dbus_client_add_disconnect_watch(GDBusClient* client,
    GDBusWatchFunction function, void* user_data, DBusFreeFunction destroy)
{
    struct disconnect_data* dc_data;

    dbus_connection_set_exit_on_disconnect(client->dbus_conn, FALSE);

    dc_data = calloc(1, sizeof(struct disconnect_data));
    if (dc_data == NULL)
        return FALSE;

    dc_data->function = function;
    dc_data->user_data = user_data;
    dc_data->watcher = client->watcher;

    if (dbus_add_signal_watch(client->watcher, NULL, NULL,
            DBUS_INTERFACE_LOCAL, "Disconnected",
            disconnected_signal, dc_data, free)
        == 0) {
        error("Failed to add watch for D-Bus Disconnected signal");
        free(dc_data);
        return FALSE;
    }

    dbus_connection_ref(client->dbus_conn);
    return TRUE;
}
