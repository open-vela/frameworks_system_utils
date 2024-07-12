// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  D-Bus helper library
 *
 *  Copyright (C) 2004-2011  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 */

#include <dbus/dbus.h>
#include <poll.h>
#include <stdlib.h>
#include <uv.h>

#include "gdbus.h"

#define info(fmt...)
#define error(fmt...)
#define debug(fmt...)

struct idle_handler {
    uv_idle_t handle;
    DBusConnection* conn;
};

struct timeout_handler {
    uv_timer_t handle;
    DBusTimeout* timeout;
};

struct watch_info {
    uv_poll_t handle;
    DBusWatch* watch;
    DBusConnection* conn;
};

struct disconnect_data {
    GDBusWatchFunction function;
    void* user_data;
};

static gboolean disconnected_signal(DBusConnection* conn,
    DBusMessage* msg, void* data)
{
    struct disconnect_data* dc_data = data;

    error("Got disconnected from the system message bus");

    dc_data->function(conn, dc_data->user_data);

    dbus_connection_unref(conn);

    return TRUE;
}

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
    struct watch_info* info = handle->data;
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

    dbus_watch_handle(info->watch, flags);

    status = dbus_connection_get_dispatch_status(conn);
    queue_dispatch(conn, status);

    dbus_connection_unref(conn);
}

static void watch_info_free(void* data)
{
    struct watch_info* info = data;

    dbus_connection_unref(info->conn);
    uv_close((uv_handle_t*)&info->handle, close_cb);
}

static dbus_bool_t add_watch(DBusWatch* watch, void* data)
{
    DBusConnection* conn = data;
    int cond = UV_DISCONNECT;
    struct watch_info* info;
    unsigned int flags;
    int fd;

    if (!dbus_watch_get_enabled(watch))
        return TRUE;

    info = calloc(1, sizeof(struct watch_info));
    if (info == NULL)
        return FALSE;

    fd = dbus_watch_get_unix_fd(watch);

    info->watch = watch;
    info->conn = dbus_connection_ref(conn);

    dbus_watch_set_data(watch, info, watch_info_free);

    flags = dbus_watch_get_flags(watch);

    if (flags & DBUS_WATCH_READABLE)
        cond |= UV_READABLE;
    if (flags & DBUS_WATCH_WRITABLE)
        cond |= UV_WRITABLE;

    if (uv_poll_init(uv_default_loop(), &info->handle, fd) != 0) {
        free(info);
        goto errout;
    }

    info->handle.data = info;
    if (uv_poll_start(&info->handle, cond, watch_func) != 0) {
        uv_close((uv_handle_t*)&info->handle, close_cb);
        goto errout;
    }

    return TRUE;
errout:
    dbus_connection_unref(conn);
    return FALSE;
}

static void remove_watch(DBusWatch* watch, void* data)
{
    if (dbus_watch_get_enabled(watch))
        return;

    /* will trigger watch_info_free() */
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

static inline void setup_dbus_with_main_loop(DBusConnection* conn)
{
    dbus_connection_set_watch_functions(conn, add_watch, remove_watch,
        watch_toggled, conn, NULL);

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

gboolean dbus_set_disconnect_function(DBusConnection* connection,
    GDBusWatchFunction function,
    void* user_data, DBusFreeFunction destroy)
{
    struct disconnect_data* dc_data;

    dc_data = calloc(1, sizeof(struct disconnect_data));

    dc_data->function = function;
    dc_data->user_data = user_data;

    dbus_connection_set_exit_on_disconnect(connection, FALSE);

    if (dbus_add_signal_watch(connection, NULL, NULL,
            DBUS_INTERFACE_LOCAL, "Disconnected",
            disconnected_signal, dc_data, free)
        == 0) {
        error("Failed to add watch for D-Bus Disconnected signal");
        free(dc_data);
        return FALSE;
    }

    return TRUE;
}
