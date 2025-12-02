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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include <dbus/dbus-list.h>
#include <dbus/dbus-string.h>
#include <dbus/dbus.h>

#include "gdbus-internal.h"

/** method call timeout in milliseconds:
 * -1 (or #DBUS_TIMEOUT_USE_DEFAULT) for default timer(25s)
 * ((int) 0x7fffffff)(or #DBUS_TIMEOUT_INFINITE) for no timeout
 */
#define METHOD_CALL_TIMEOUT (300 * 1000)

#ifndef DBUS_INTERFACE_OBJECT_MANAGER
#define DBUS_INTERFACE_OBJECT_MANAGER DBUS_INTERFACE_DBUS ".ObjectManager"
#endif

struct ptr_array {
    void** data;
    size_t len;
    size_t alloc_len;
};

struct prop_entry {
    char* name;
    int type;
    DBusMessage* msg;
};

enum client_async_handler_type {
    ASYNC_HDL_REPLY_ASYNC = 1,
    ASYNC_HDL_GET_PROP,
};

typedef struct client_async_handler {
    int handle_type;
    void* data;
} client_async_handler;

struct get_prop_handler {
    GDBusProxy* proxy;
    const char* name;
    void* prop_value;
    GDBusPropIterFunction prop_iter_cb;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    int result;
};

struct pending_call_async {
    DBusConnection* conn;
    DBusMessage* msg;
    DBusPendingCall** call;
    int timeout;
    DBusPendingCallNotifyFunction pending_reply;
    void* user_data;
    DBusFreeFunction destroy;
};

static client_async_handler* new_client_async_handler(int handle_type, void* data)
{
    client_async_handler* async_hdl = calloc(1, sizeof(client_async_handler));
    if (async_hdl == NULL)
        return NULL;
    async_hdl->handle_type = handle_type;
    async_hdl->data = data;

    return async_hdl;
}

static gboolean dbus_send_msg_reply_pendingcall(DBusConnection* conn, DBusMessage* msg,
    DBusPendingCall** call, int timeout, DBusPendingCallNotifyFunction pending_reply,
    void* user_data, DBusFreeFunction destroy)
{
    DBusPendingCall* pending_call = NULL;
    DBusPendingCall** pending_call_pp = NULL;

    if (call == NULL) {
        pending_call_pp = &pending_call;
    } else {
        pending_call_pp = call;
    }

    if (dbus_send_message_with_reply(conn, msg, pending_call_pp, timeout) == FALSE) {
        return FALSE;
    }

    dbus_pending_call_set_notify(*pending_call_pp, pending_reply, user_data, destroy);

    /* call is not NULL, it will unref in pendingcall notify */
    if (call == NULL) {
        dbus_pending_call_unref(*pending_call_pp);
    }

    return TRUE;
}

static void client_async_handler_reply(struct pending_call_async* handler)
{
    gboolean ret = dbus_send_msg_reply_pendingcall(handler->conn, handler->msg, handler->call,
        handler->timeout, handler->pending_reply, handler->user_data, handler->destroy);
    if (ret == FALSE) {
        if (handler->destroy != NULL)
            handler->destroy(handler->user_data);
    }

    dbus_message_unref(handler->msg);
    dbus_connection_unref(handler->conn);
    free(handler);
}

static void client_async_handler_get_prop(struct get_prop_handler* handler)
{
    DBusMessageIter iter;

    handler->result = dbus_proxy_get_property(handler->proxy, handler->name, &iter);
    if (handler->result == TRUE)
        handler->prop_iter_cb(&iter, handler->prop_value);

    pthread_mutex_lock(&handler->mutex);
    pthread_cond_signal(&handler->cond);
    pthread_mutex_unlock(&handler->mutex);
}

static void client_uv_async_queue_cb(uv_async_queue_t* async_queue, void* data)
{
    client_async_handler* async_hdl = (client_async_handler*)data;

    switch (async_hdl->handle_type) {
    case ASYNC_HDL_REPLY_ASYNC:
        client_async_handler_reply(async_hdl->data);
        break;
    case ASYNC_HDL_GET_PROP:
        client_async_handler_get_prop(async_hdl->data);
        break;
    }

    free(async_hdl);
}

static gboolean dbus_send_msg_reply_async(GDBusClient* client, DBusMessage* msg,
    DBusPendingCall** call, int timeout, DBusPendingCallNotifyFunction pending_reply,
    void* user_data, DBusFreeFunction destroy)
{
    struct pending_call_async* handler;
    uv_thread_t self_tid = uv_thread_self();

    if (uv_thread_equal(&self_tid, &client->main_thread) != 0) {
        return dbus_send_msg_reply_pendingcall(client->dbus_conn, msg, call, timeout,
            pending_reply, user_data, destroy);
    }

    handler = calloc(1, sizeof(struct pending_call_async));
    if (handler == NULL) {
        return FALSE;
    }

    handler->conn = client->dbus_conn;
    dbus_connection_ref(client->dbus_conn);
    handler->msg = msg;
    dbus_message_ref(msg);
    handler->call = call;
    handler->timeout = timeout;
    handler->pending_reply = pending_reply;
    handler->user_data = user_data;
    handler->destroy = destroy;

    client_async_handler* async_hdl = new_client_async_handler(ASYNC_HDL_REPLY_ASYNC, handler);
    if (uv_async_queue_send(&client->async_queue, async_hdl) != 0) {
        dbus_connection_unref(client->dbus_conn);
        dbus_message_unref(msg);
        free(handler);
        free(async_hdl);
        return FALSE;
    }

    return TRUE;
}

static char* strdup0(const char* str)
{
    if (str)
        return strdup(str);

    return NULL;
}

static struct ptr_array* ptr_array_sized_new(size_t size)
{
    struct ptr_array* parray;

    parray = malloc(sizeof(struct ptr_array) + size * sizeof(void*));
    if (parray == NULL)
        return NULL;

    parray->data = (void**)(parray + 1);
    parray->alloc_len = size;
    parray->len = 0;
    return parray;
}

static void ptr_array_add(struct ptr_array** array, void* data)
{
    struct ptr_array* tmp;

    if ((*array)->len >= (*array)->alloc_len) {
        tmp = realloc(*array, sizeof(*tmp) + (*array)->alloc_len * 2 * sizeof(void*));
        if (tmp == NULL)
            return;

        *array = tmp;
        (*array)->alloc_len *= 2;
        (*array)->data = (void**)(tmp + 1);
    }

    (*array)->data[(*array)->len++] = data;
}

static void ptr_array_free(struct ptr_array* array)
{
    int i;
    for (i = 0; i < array->len; i++) {
        free(array->data[i]);
    }

    free(array);
}

static void modify_match_reply(DBusPendingCall* call, void* user_data)
{
    DBusMessage* reply = dbus_pending_call_steal_reply(call);
    DBusError error;

    dbus_error_init(&error);

    if (dbus_set_error_from_message(&error, reply) == TRUE)
        dbus_error_free(&error);

    dbus_message_unref(reply);
}

static gboolean modify_match(GDBusClient* client, const char* member,
    const char* rule)
{
    DBusMessage* msg;

    msg = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
        DBUS_INTERFACE_DBUS, member);
    if (msg == NULL)
        return FALSE;

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &rule,
        DBUS_TYPE_INVALID);

    if (dbus_send_msg_reply_async(client, msg, NULL, -1, modify_match_reply, NULL, NULL)
        == FALSE) {
        dbus_message_unref(msg);
        return FALSE;
    }

    dbus_message_unref(msg);

    return TRUE;
}

static void append_variant(DBusMessageIter* iter, int type, const void* val)
{
    DBusMessageIter value;
    char sig[2] = { type, '\0' };

    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, sig, &value);

    dbus_message_iter_append_basic(&value, type, val);

    dbus_message_iter_close_container(iter, &value);
}

static void append_array_variant(DBusMessageIter* iter, int type, void* val,
    int n_elements)
{
    DBusMessageIter variant, array;
    char type_sig[2] = { type, '\0' };
    char array_sig[3] = { DBUS_TYPE_ARRAY, type, '\0' };

    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
        array_sig, &variant);

    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY,
        type_sig, &array);

    if (dbus_type_is_fixed(type) == TRUE) {
        dbus_message_iter_append_fixed_array(&array, type, val,
            n_elements);
    } else if (type == DBUS_TYPE_STRING || type == DBUS_TYPE_OBJECT_PATH) {
        const char*** str_array = val;
        int i;

        for (i = 0; i < n_elements; i++)
            dbus_message_iter_append_basic(&array, type,
                &((*str_array)[i]));
    }

    dbus_message_iter_close_container(&variant, &array);

    dbus_message_iter_close_container(iter, &variant);
}

static void dict_append_basic(DBusMessageIter* dict, int key_type,
    const void* key, int type, void* val)
{
    DBusMessageIter entry;

    if (type == DBUS_TYPE_STRING) {
        const char* str = *((const char**)val);
        if (str == NULL)
            return;
    }

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY,
        NULL, &entry);

    dbus_message_iter_append_basic(&entry, key_type, key);

    append_variant(&entry, type, val);

    dbus_message_iter_close_container(dict, &entry);
}

void dbus_dict_append_entry(DBusMessageIter* dict,
    const char* key, int type, void* val)
{
    dict_append_basic(dict, DBUS_TYPE_STRING, &key, type, val);
}

void dbus_dict_append_basic_array(DBusMessageIter* dict, int key_type,
    const void* key, int type, void* val,
    int n_elements)
{
    DBusMessageIter entry;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY,
        NULL, &entry);

    dbus_message_iter_append_basic(&entry, key_type, key);

    append_array_variant(&entry, type, val, n_elements);

    dbus_message_iter_close_container(dict, &entry);
}

void dbus_dict_append_array(DBusMessageIter* dict,
    const char* key, int type, void* val,
    int n_elements)
{
    dbus_dict_append_basic_array(dict, DBUS_TYPE_STRING, &key, type, val,
        n_elements);
}

static void iter_append_iter(DBusMessageIter* base, DBusMessageIter* iter)
{
    int type;

    type = dbus_message_iter_get_arg_type(iter);

    if (dbus_type_is_basic(type)) {
        DBusBasicValue value;

        dbus_message_iter_get_basic(iter, &value);
        dbus_message_iter_append_basic(base, type, &value);
    } else if (dbus_type_is_container(type)) {
        DBusMessageIter iter_sub, base_sub;
        char* sig;

        dbus_message_iter_recurse(iter, &iter_sub);

        switch (type) {
        case DBUS_TYPE_ARRAY:
        case DBUS_TYPE_VARIANT:
            sig = dbus_message_iter_get_signature(&iter_sub);
            break;
        default:
            sig = NULL;
            break;
        }

        dbus_message_iter_open_container(base, type, sig, &base_sub);

        if (sig != NULL)
            dbus_free(sig);

        while (dbus_message_iter_get_arg_type(&iter_sub) != DBUS_TYPE_INVALID) {
            iter_append_iter(&base_sub, &iter_sub);
            dbus_message_iter_next(&iter_sub);
        }

        dbus_message_iter_close_container(base, &base_sub);
    }
}

static void prop_entry_update(struct prop_entry* prop, DBusMessageIter* iter)
{
    DBusMessage* msg;
    DBusMessageIter base;

    msg = dbus_message_new(DBUS_MESSAGE_TYPE_METHOD_RETURN);
    if (msg == NULL)
        return;

    dbus_message_iter_init_append(msg, &base);
    iter_append_iter(&base, iter);

    if (prop->msg != NULL)
        dbus_message_unref(prop->msg);

    prop->msg = dbus_message_copy(msg);
    dbus_message_unref(msg);
}

static struct prop_entry* prop_entry_new(const char* name,
    DBusMessageIter* iter)
{
    struct prop_entry* prop;

    prop = calloc(1, sizeof(struct prop_entry));
    if (prop == NULL)
        return NULL;

    prop->name = strdup0(name);
    prop->type = dbus_message_iter_get_arg_type(iter);

    prop_entry_update(prop, iter);

    return prop;
}

static void prop_entry_free(gpointer data)
{
    struct prop_entry* prop = data;

    if (prop == NULL)
        return;

    if (prop->msg != NULL)
        dbus_message_unref(prop->msg);

    free(prop->name);

    free(prop);
}

static void add_property(GDBusProxy* proxy, const char* name,
    DBusMessageIter* iter, gboolean send_changed, gboolean standard)
{
    GDBusClient* client = proxy->client;
    DBusMessageIter value;
    struct prop_entry* prop;

    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_VARIANT)
        return;

    dbus_message_iter_recurse(iter, &value);

    client->standard = standard;
    prop = _dbus_hash_table_lookup_string(proxy->prop_list, name);
    if (prop != NULL) {
        prop_entry_update(prop, &value);
        goto done;
    }

    prop = prop_entry_new(name, &value);
    if (prop == NULL)
        return;

    _dbus_hash_table_insert_string(proxy->prop_list, prop->name, prop);

done:
    if (proxy->prop_func)
        proxy->prop_func(proxy, name, &value, proxy->prop_data);

    if (send_changed == FALSE)
        return;

    if (client->property_changed)
        client->property_changed(proxy, name, &value,
            client->user_data);
}

static void update_properties(GDBusProxy* proxy, DBusMessageIter* iter,
    gboolean send_changed, gboolean standard)
{
    DBusMessageIter dict;

    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
        return;

    dbus_message_iter_recurse(iter, &dict);

    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        const char* name;

        dbus_message_iter_recurse(&dict, &entry);

        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING)
            break;

        dbus_message_iter_get_basic(&entry, &name);
        dbus_message_iter_next(&entry);

        add_property(proxy, name, &entry, send_changed, standard);

        dbus_message_iter_next(&dict);
    }
}

static void proxy_added(GDBusClient* client, GDBusProxy* proxy)
{
    if (!proxy->pending)
        return;

    if (client->proxy_added)
        client->proxy_added(proxy, client->user_data);

    proxy->pending = FALSE;
    proxy->filter_first = TRUE;
}

static void get_all_properties_reply(DBusPendingCall* call, void* user_data)
{
    GDBusProxy* proxy = user_data;
    GDBusClient* client = proxy->client;
    DBusMessage* reply = dbus_pending_call_steal_reply(call);
    DBusMessageIter iter;
    DBusError error;

    dbus_client_ref(client);

    dbus_error_init(&error);

    if (dbus_set_error_from_message(&error, reply) == TRUE) {
        dbus_error_free(&error);
        goto done;
    }

    if (!dbus_message_iter_init(reply, &iter))
        goto done;

    update_properties(proxy, &iter, FALSE, TRUE);

done:
    if (_dbus_hash_table_get_n_entries(proxy->prop_list) != 0)
        proxy_added(client, proxy);

    dbus_message_unref(reply);

    proxy->getting_all_prop = FALSE;
    dbus_pending_call_unref(proxy->get_all_call);
    proxy->get_all_call = NULL;

    dbus_client_unref(client);
}

static void get_all_properties(GDBusProxy* proxy)
{
    GDBusClient* client = proxy->client;
    const char* service_name = client->service_name;
    DBusMessage* msg;

    if (proxy->getting_all_prop)
        return;

    msg = dbus_message_new_method_call(service_name, proxy->obj_path,
        DBUS_INTERFACE_PROPERTIES, "GetAll");
    if (msg == NULL)
        return;

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &proxy->interface,
        DBUS_TYPE_INVALID);

    if (dbus_send_msg_reply_async(client, msg, &proxy->get_all_call,
            -1, get_all_properties_reply, proxy, NULL)
        == FALSE) {
        dbus_message_unref(msg);
        return;
    }
    proxy->getting_all_prop = TRUE;
    dbus_message_unref(msg);
}

GDBusProxy* dbus_proxy_lookup(void* list, int* index, const char* path,
    const char* interface)
{
    DBusList* list_ = list;
    int n = index ? *index : 0;
    DBusList* l;

    if (!interface)
        return NULL;

    for (l = _dbus_list_get_first_link(&list_); l;
         l = _dbus_list_get_next_link(&list_, l)) {
        if (n-- > 0) {
            continue;
        }

        GDBusProxy* proxy = l->data;
        const char* proxy_iface = dbus_proxy_get_interface(proxy);
        const char* proxy_path = dbus_proxy_get_path(proxy);

        if (index)
            (*index)++;

        if (strcmp(proxy_iface, interface) == 0 && strcmp(proxy_path, path) == 0)
            return proxy;
    }

    return NULL;
}

char* dbus_proxy_path_lookup(void* list, int* index, const char* path)
{
    DBusList* list_ = list;
    int len = strlen(path);
    int n = index ? *index : 0;
    DBusList* l;

    for (l = _dbus_list_get_first_link(&list_); l;
         l = _dbus_list_get_next_link(&list_, l)) {
        if (n-- > 0) {
            continue;
        }

        GDBusProxy* proxy = l->data;
        const char* proxy_path = dbus_proxy_get_path(proxy);

        if (index)
            (*index)++;

        if (!strncasecmp(proxy_path, path, len))
            return strdup0(proxy_path);
    }

    return NULL;
}

static gboolean properties_changed(DBusConnection* conn, DBusMessage* msg,
    void* user_data)
{
    GDBusProxy* proxy = user_data;
    GDBusClient* client = proxy->client;
    DBusMessageIter iter, entry;
    const char* interface;

    if (dbus_message_iter_init(msg, &iter) == FALSE)
        return TRUE;

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
        return TRUE;

    dbus_message_iter_get_basic(&iter, &interface);
    dbus_message_iter_next(&iter);

    update_properties(proxy, &iter, TRUE, TRUE);

    dbus_message_iter_next(&iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
        return TRUE;

    dbus_message_iter_recurse(&iter, &entry);

    while (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
        const char* name;

        dbus_message_iter_get_basic(&entry, &name);

        _dbus_hash_table_remove_string(proxy->prop_list, name);

        if (proxy->prop_func)
            proxy->prop_func(proxy, name, NULL, proxy->prop_data);

        if (client->property_changed)
            client->property_changed(proxy, name, NULL,
                client->user_data);

        dbus_message_iter_next(&entry);
    }

    return TRUE;
}

static gboolean properties_changed_non_standard(DBusConnection* conn, DBusMessage* msg,
    void* user_data)
{
    GDBusProxy* proxy = user_data;
    DBusMessageIter iter;
    const char* name;

    if (dbus_message_iter_init(msg, &iter) == FALSE)
        return TRUE;

    dbus_message_iter_get_basic(&iter, &name);

    dbus_message_iter_next(&iter);
    add_property(proxy, name, &iter, TRUE, FALSE);

    return TRUE;
}

static GDBusProxy* proxy_new(GDBusClient* client, const char* path,
    const char* interface)
{
    GDBusProxy* proxy;

    if (client->proxy_filter && client->proxy_filter(path, interface, client->user_data)) {
        return NULL;
    }

    proxy = calloc(1, sizeof(GDBusProxy));
    if (proxy == NULL)
        return NULL;

    proxy->client = client;
    proxy->obj_path = strdup0(path);
    proxy->interface = strdup0(interface);

    proxy->prop_list = _dbus_hash_table_new(DBUS_HASH_STRING,
        NULL, prop_entry_free);
    proxy->watch = dbus_add_properties_watch(client->watcher,
        client->service_name,
        proxy->obj_path,
        proxy->interface,
        properties_changed,
        proxy, NULL);

    proxy->watch_non_standard = dbus_add_signal_watch(client->watcher,
        client->service_name,
        proxy->obj_path,
        proxy->interface,
        "PropertyChanged",
        properties_changed_non_standard,
        proxy, NULL);

    proxy->pending = TRUE;
    proxy->filter_first = FALSE;

    _dbus_list_append(&client->proxy_list, proxy);

    return dbus_proxy_ref(proxy);
}

static void proxy_free(gpointer data)
{
    GDBusProxy* proxy = data;

    if (proxy->client) {
        GDBusClient* client = proxy->client;

        proxy->getting_all_prop = FALSE;
        if (proxy->get_all_call != NULL) {
            dbus_pending_call_cancel(proxy->get_all_call);
            dbus_pending_call_unref(proxy->get_all_call);
            proxy->get_all_call = NULL;
        }

        if (client->proxy_removed)
            client->proxy_removed(proxy, client->user_data);

        dbus_remove_watch(client->watcher, proxy->watch);
        dbus_remove_watch(client->watcher, proxy->watch_non_standard);

        _dbus_hash_table_remove_all(proxy->prop_list);

        proxy->client = NULL;
    }

    if (proxy->removed_func)
        proxy->removed_func(proxy, proxy->removed_data);

    dbus_proxy_unref(proxy);
}

static void proxy_remove(GDBusClient* client, const char* path,
    const char* interface)
{
    DBusList* list;

    for (list = _dbus_list_get_first_link(&client->proxy_list); list;
         list = _dbus_list_get_next_link(&client->proxy_list, list)) {
        GDBusProxy* proxy = list->data;

        if (strcmp(proxy->interface, interface) == 0 && strcmp(proxy->obj_path, path) == 0) {
            _dbus_list_remove_link(&client->proxy_list, list);
            proxy_free(proxy);
            break;
        }
    }
}

static void start_service(GDBusProxy* proxy)
{
    GDBusClient* client = proxy->client;
    const char* service_name = client->service_name;
    dbus_uint32_t flags = 0;
    DBusMessage* msg;

    msg = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
        DBUS_INTERFACE_DBUS,
        "StartServiceByName");
    if (msg == NULL)
        return;

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &service_name,
        DBUS_TYPE_UINT32, &flags,
        DBUS_TYPE_INVALID);

    dbus_send_message(client->dbus_conn, msg);
    return;
}

static void proxy_get_properties_reply(DBusPendingCall* call, void* user_data)
{
    GDBusProxy* proxy = user_data;
    GDBusClient* client = proxy->client;
    DBusMessage* message = dbus_pending_call_steal_reply(call);
    DBusMessageIter array;
    DBusError error;

    if (!proxy->getting_all_prop) {
        /**
         * not in getting prop process, do nothing for reply.
         * maybe proxy already freed.
         */
        return;
    }

    dbus_error_init(&error);
    if (dbus_set_error_from_message(&error, message)) {
        goto out;
    }

    if (!dbus_message_iter_init(message, &array)) {
        goto out;
    }

    update_properties(proxy, &array, FALSE, FALSE);

out:
    proxy_added(client, proxy);

    dbus_error_free(&error);
    dbus_message_unref(message);
    proxy->getting_all_prop = FALSE;
    dbus_pending_call_unref(proxy->get_all_call);
    proxy->get_all_call = NULL;
}

static gboolean proxy_get_properties(GDBusProxy* proxy)
{
    DBusMessage* msg;
    GDBusClient* client;

    if (proxy->getting_all_prop)
        return FALSE;

    client = proxy->client;

    if (client->proxy_property_filter
        && !client->proxy_property_filter(proxy, client->user_data)) {
        msg = dbus_message_new_method_call(client->service_name,
            proxy->obj_path, proxy->interface, "GetProperties");
        if (msg == NULL)
            return FALSE;

        if (dbus_send_msg_reply_async(client, msg, &proxy->get_all_call, -1,
                proxy_get_properties_reply, proxy, NULL)
            == FALSE) {
            dbus_message_unref(msg);
            return FALSE;
        }
        dbus_message_unref(msg);
        proxy->getting_all_prop = TRUE;
    }

    return TRUE;
}

GDBusProxy* dbus_proxy_new(GDBusClient* client, const char* path,
    const char* interface)
{
    GDBusProxy* proxy;

    if (client == NULL)
        return NULL;

    proxy = dbus_proxy_lookup(client->proxy_list, NULL,
        path, interface);
    if (proxy)
        return dbus_proxy_ref(proxy);

    proxy = proxy_new(client, path, interface);
    if (proxy == NULL)
        return NULL;

    if (!client->connected) {
        /* Force service to start */
        start_service(proxy);
        return dbus_proxy_ref(proxy);
    }

    if (!client->getting_object_call) {
        get_all_properties(proxy);
        proxy_get_properties(proxy);
    }

    return dbus_proxy_ref(proxy);
}

GDBusProxy* dbus_proxy_ref(GDBusProxy* proxy)
{
    if (proxy == NULL)
        return NULL;

    __sync_fetch_and_add(&proxy->ref_count, 1);

    return proxy;
}

void dbus_proxy_unref(GDBusProxy* proxy)
{
    if (proxy == NULL)
        return;

    if (__sync_sub_and_fetch(&proxy->ref_count, 1) > 0)
        return;

    proxy->getting_all_prop = FALSE;
    if (proxy->get_all_call != NULL) {
        dbus_pending_call_cancel(proxy->get_all_call);
        dbus_pending_call_unref(proxy->get_all_call);
    }

    _dbus_hash_table_unref(proxy->prop_list);

    free(proxy->obj_path);
    free(proxy->interface);

    free(proxy);
}

const char* dbus_proxy_get_path(const GDBusProxy* proxy)
{
    if (proxy == NULL)
        return NULL;

    return proxy->obj_path;
}

const char* dbus_proxy_get_interface(GDBusProxy* proxy)
{
    if (proxy == NULL)
        return NULL;

    return proxy->interface;
}

gboolean dbus_proxy_get_property(GDBusProxy* proxy, const char* name,
    DBusMessageIter* iter)
{
    struct prop_entry* prop;

    if (proxy == NULL || name == NULL)
        return FALSE;

    prop = _dbus_hash_table_lookup_string(proxy->prop_list, name);
    if (prop != NULL
        && prop->msg != NULL
        && dbus_message_iter_init(prop->msg, iter) == TRUE)
        return TRUE;

    return FALSE;
}

gboolean dbus_proxy_get_property_basic(GDBusProxy* proxy, const char* name,
    void* value)
{
    return dbus_proxy_get_property_iter_cb(proxy, name, value,
        dbus_message_iter_get_basic);
}

gboolean dbus_proxy_get_property_iter_cb(GDBusProxy* proxy, const char* name,
    void* value, GDBusPropIterFunction iter_cb)
{
    DBusMessageIter iter;
    uv_thread_t self_tid = uv_thread_self();
    GDBusClient* client = NULL;
    gboolean ret = FALSE;

    if (proxy == NULL || name == NULL)
        return FALSE;

    client = proxy->client;
    if (client == NULL)
        return FALSE;

    if (uv_thread_equal(&self_tid, &client->main_thread) != 0) {
        if (dbus_proxy_get_property(proxy, name, &iter) == FALSE)
            return FALSE;

        iter_cb(&iter, value);
        return TRUE;
    }

    struct get_prop_handler* prop_hdl = calloc(1, sizeof(struct get_prop_handler));
    if (prop_hdl == NULL)
        return FALSE;
    prop_hdl->proxy = proxy;
    prop_hdl->name = name;
    pthread_mutex_init(&prop_hdl->mutex, NULL);
    pthread_cond_init(&prop_hdl->cond, NULL);
    prop_hdl->prop_value = value;
    prop_hdl->prop_iter_cb = iter_cb;
    prop_hdl->result = -1;

    client_async_handler* async_hdl = new_client_async_handler(ASYNC_HDL_GET_PROP, prop_hdl);
    pthread_mutex_lock(&prop_hdl->mutex);
    if (uv_async_queue_send(&client->async_queue, async_hdl) != 0) {
        free(prop_hdl);
        free(async_hdl);
        return FALSE;
    }

    while (prop_hdl->result == -1)
        pthread_cond_wait(&prop_hdl->cond, &prop_hdl->mutex);

    if (prop_hdl->result == TRUE)
        ret = TRUE;

    pthread_mutex_unlock(&prop_hdl->mutex);
    free(prop_hdl);
    return ret;
}

struct refresh_property_data {
    GDBusProxy* proxy;
    char* name;
};

static void refresh_property_free(gpointer user_data)
{
    struct refresh_property_data* data = user_data;

    free(data->name);
    free(data);
}

static void refresh_property_reply(DBusPendingCall* call, void* user_data)
{
    struct refresh_property_data* data = user_data;
    DBusMessage* reply = dbus_pending_call_steal_reply(call);
    DBusError error;

    dbus_error_init(&error);

    if (dbus_set_error_from_message(&error, reply) == FALSE) {
        DBusMessageIter iter;

        if (!dbus_message_iter_init(reply, &iter))
            return;

        add_property(data->proxy, data->name, &iter, TRUE, TRUE);
    } else
        dbus_error_free(&error);

    dbus_message_unref(reply);
}

static void refresh_properties_reply_not_standard(DBusMessage* message,
    void* user_data)
{
    GDBusProxy* proxy = user_data;
    DBusMessageIter array;
    DBusError error;

    dbus_proxy_ref(proxy);
    dbus_error_init(&error);

    if (dbus_set_error_from_message(&error, message)) {
        dbus_error_free(&error);
        dbus_proxy_unref(proxy);
        return;
    }

    if (!dbus_message_iter_init(message, &array)) {
        dbus_proxy_unref(proxy);
        return;
    }

    update_properties(proxy, &array, TRUE, FALSE);

    dbus_proxy_unref(proxy);
}

gboolean dbus_proxy_refresh_property(GDBusProxy* proxy, const char* name)
{
    struct refresh_property_data* data;
    GDBusClient* client;
    DBusMessage* msg;
    DBusMessageIter iter;

    if (proxy == NULL || name == NULL)
        return FALSE;

    client = proxy->client;
    if (client == NULL)
        return FALSE;

    if (!client->standard) {
        return dbus_proxy_method_call(proxy, "GetProperties",
            NULL, refresh_properties_reply_not_standard,
            proxy, NULL);
    }

    data = calloc(1, sizeof(struct refresh_property_data));
    if (data == NULL)
        return FALSE;

    data->proxy = proxy;
    data->name = strdup0(name);

    msg = dbus_message_new_method_call(client->service_name,
        proxy->obj_path, DBUS_INTERFACE_PROPERTIES, "Get");

    if (msg == NULL) {
        refresh_property_free(data);
        return FALSE;
    }

    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING,
        &proxy->interface);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &name);

    if (dbus_send_msg_reply_async(client, msg, NULL, -1, refresh_property_reply,
            data, refresh_property_free)
        == FALSE) {
        dbus_message_unref(msg);
        refresh_property_free(data);
        return FALSE;
    }

    dbus_message_unref(msg);

    return TRUE;
}

struct set_property_data {
    GDBusResultFunction function;
    void* user_data;
    GDBusDestroyFunction destroy;
};

static void set_property_reply(DBusPendingCall* call, void* user_data)
{
    struct set_property_data* data = user_data;
    DBusMessage* reply = dbus_pending_call_steal_reply(call);
    DBusError error;

    dbus_error_init(&error);

    dbus_set_error_from_message(&error, reply);

    if (data->function)
        data->function(&error, data->user_data);

    if (data->destroy)
        data->destroy(data->user_data);

    dbus_error_free(&error);

    dbus_message_unref(reply);
}

gboolean dbus_proxy_set_property_basic(GDBusProxy* proxy,
    const char* name, int type, const void* value,
    GDBusResultFunction function, void* user_data,
    GDBusDestroyFunction destroy)
{
    struct set_property_data* data;
    GDBusClient* client;
    DBusMessage* msg;
    DBusMessageIter iter;

    if (proxy == NULL || name == NULL || value == NULL)
        return FALSE;

    if (dbus_type_is_basic(type) == FALSE)
        return FALSE;

    client = proxy->client;
    if (client == NULL)
        return FALSE;

    data = calloc(1, sizeof(struct set_property_data));
    if (data == NULL)
        return FALSE;

    data->function = function;
    data->user_data = user_data;
    data->destroy = destroy;

    if (client->standard) {
        msg = dbus_message_new_method_call(client->service_name,
            proxy->obj_path, DBUS_INTERFACE_PROPERTIES, "Set");
    } else {
        msg = dbus_message_new_method_call(client->service_name,
            proxy->obj_path, proxy->interface, "SetProperty");
    }

    if (msg == NULL) {
        free(data);
        return FALSE;
    }

    dbus_message_iter_init_append(msg, &iter);

    if (client->standard)
        dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING,
            &proxy->interface);

    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &name);

    append_variant(&iter, type, value);

    if (dbus_send_msg_reply_async(client, msg, NULL, -1, set_property_reply, data, free)
        == FALSE) {
        dbus_message_unref(msg);
        free(data);
        return FALSE;
    }

    dbus_message_unref(msg);

    return TRUE;
}

gboolean dbus_proxy_set_property_array(GDBusProxy* proxy,
    const char* name, int type, const void* value,
    size_t size, GDBusResultFunction function,
    void* user_data, GDBusDestroyFunction destroy)
{
    struct set_property_data* data;
    GDBusClient* client;
    DBusMessage* msg;
    DBusMessageIter iter;

    if (!proxy || !name || !value)
        return FALSE;

    if (!dbus_type_is_basic(type))
        return FALSE;

    client = proxy->client;
    if (!client || !client->standard)
        return FALSE;

    data = calloc(1, sizeof(struct set_property_data));
    if (!data)
        return FALSE;

    data->function = function;
    data->user_data = user_data;
    data->destroy = destroy;

    msg = dbus_message_new_method_call(client->service_name,
        proxy->obj_path,
        DBUS_INTERFACE_PROPERTIES,
        "Set");
    if (!msg) {
        free(data);
        return FALSE;
    }

    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING,
        &proxy->interface);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &name);

    append_array_variant(&iter, type, &value, size);

    if (dbus_send_msg_reply_async(client, msg, NULL, -1, set_property_reply, data, free)
        == FALSE) {
        dbus_message_unref(msg);
        free(data);
        return FALSE;
    }

    dbus_message_unref(msg);

    return TRUE;
}

struct method_call_data {
    GDBusReturnFunction function;
    void* user_data;
    GDBusDestroyFunction destroy;
};

static void method_call_reply(DBusPendingCall* call, void* user_data)
{
    struct method_call_data* data = user_data;
    DBusMessage* reply = dbus_pending_call_steal_reply(call);

    if (data->function)
        data->function(reply, data->user_data);

    if (data->destroy)
        data->destroy(data->user_data);

    dbus_message_unref(reply);
}

gboolean dbus_proxy_method_call(GDBusProxy* proxy, const char* method,
    GDBusSetupFunction setup,
    GDBusReturnFunction function, void* user_data,
    GDBusDestroyFunction destroy)
{
    struct method_call_data* data;
    GDBusClient* client;
    DBusMessage* msg;

    if (proxy == NULL || method == NULL)
        return FALSE;

    client = proxy->client;
    if (client == NULL)
        return FALSE;

    msg = dbus_message_new_method_call(client->service_name,
        proxy->obj_path, proxy->interface, method);
    if (msg == NULL)
        return FALSE;

    if (setup) {
        DBusMessageIter iter;

        dbus_message_iter_init_append(msg, &iter);
        setup(&iter, user_data);
    }

    if (!function)
        return dbus_send_message(client->dbus_conn, msg);

    data = calloc(1, sizeof(struct method_call_data));
    if (data == NULL)
        return FALSE;

    data->function = function;
    data->user_data = user_data;
    data->destroy = destroy;

    if (dbus_send_msg_reply_async(client, msg, NULL, METHOD_CALL_TIMEOUT,
            method_call_reply, data, free)
        == FALSE) {
        dbus_message_unref(msg);
        free(data);
        return FALSE;
    }

    dbus_message_unref(msg);

    return TRUE;
}

gboolean dbus_proxy_set_property_watch(GDBusProxy* proxy,
    GDBusPropertyFunction function, void* user_data)
{
    if (proxy == NULL)
        return FALSE;

    proxy->prop_func = function;
    proxy->prop_data = user_data;

    return TRUE;
}

gboolean dbus_proxy_remove_property_watch(GDBusProxy* proxy,
    GDBusDestroyFunction destroy)
{
    if (proxy == NULL)
        return FALSE;

    proxy->prop_func = NULL;
    if (destroy)
        destroy(proxy->prop_data);
    proxy->prop_data = NULL;

    return TRUE;
}

gboolean dbus_proxy_set_removed_watch(GDBusProxy* proxy,
    GDBusProxyFunction function, void* user_data)
{
    if (proxy == NULL)
        return FALSE;

    proxy->removed_func = function;
    proxy->removed_data = user_data;

    return TRUE;
}

static void refresh_properties(DBusList* list)
{
    DBusList* l;

    for (l = _dbus_list_get_first_link(&list); l;
         l = _dbus_list_get_next_link(&list, l)) {
        GDBusProxy* proxy = l->data;

        if (proxy->pending)
            get_all_properties(proxy);
    }
}

static void parse_properties(GDBusClient* client, const char* path,
    const char* interface, DBusMessageIter* iter)
{
    GDBusProxy* proxy;

    if (strcmp(interface, DBUS_INTERFACE_INTROSPECTABLE) == 0)
        return;

    if (strcmp(interface, DBUS_INTERFACE_PROPERTIES) == 0)
        return;

    proxy = dbus_proxy_lookup(client->proxy_list, NULL,
        path, interface);
    if (proxy && !proxy->pending) {
        update_properties(proxy, iter, FALSE, TRUE);
        return;
    }

    if (!proxy) {
        proxy = proxy_new(client, path, interface);
        if (proxy == NULL)
            return;
    }

    update_properties(proxy, iter, FALSE, TRUE);

    if (_dbus_hash_table_get_n_entries(proxy->prop_list) != 0)
        proxy_added(client, proxy);
}

static void parse_interfaces(GDBusClient* client, const char* path,
    DBusMessageIter* iter, gboolean added)
{
    DBusMessageIter dict;

    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
        return;

    dbus_message_iter_recurse(iter, &dict);

    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        const char* interface;
        GDBusProxy* proxy;

        dbus_message_iter_recurse(&dict, &entry);

        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING)
            break;

        dbus_message_iter_get_basic(&entry, &interface);
        dbus_message_iter_next(&entry);

        parse_properties(client, path, interface, &entry);

        dbus_message_iter_next(&dict);

        if (added) {
            proxy = dbus_proxy_lookup(client->proxy_list, NULL, path, interface);
            if (proxy)
                proxy_get_properties(proxy);
        }
    }
}

static void get_properties_reply_not_standard(DBusPendingCall* call, void* user_data)
{
    GDBusProxy* proxy = user_data;
    GDBusClient* client = proxy->client;
    DBusMessage* message = dbus_pending_call_steal_reply(call);
    DBusMessageIter array;
    DBusError error;

    if (!proxy->getting_all_prop) {
        /**
         * not in getting prop process, do nothing for reply.
         * maybe proxy already freed.
         */
        return;
    }

    dbus_error_init(&error);

    if (dbus_set_error_from_message(&error, message)) {
        goto out;
    }

    if (!dbus_message_iter_init(message, &array)) {
        goto out;
    }

    update_properties(proxy, &array, FALSE, FALSE);

out:
    proxy_added(client, proxy);

    if (client->ready_called == FALSE
        && proxy == _dbus_list_get_last(&client->proxy_list)
        && client->ready && !client->standard) {
        client->ready_called = TRUE;
        client->ready(client, client->ready_data);
    }

    dbus_error_free(&error);
    dbus_message_unref(message);
    proxy->getting_all_prop = FALSE;
    dbus_pending_call_unref(proxy->get_all_call);
    proxy->get_all_call = NULL;
}

static gboolean get_properties_non_standard(GDBusClient* client)
{
    DBusList* list;

    for (list = _dbus_list_get_first_link(&client->proxy_list); list;
         list = _dbus_list_get_next_link(&client->proxy_list, list)) {
        GDBusProxy* proxy;
        DBusMessage* msg;

        proxy = list->data;
        if (proxy->getting_all_prop)
            continue;

        client = proxy->client;
        if (client->proxy_property_filter && client->proxy_property_filter(proxy, client->user_data)) {
            if (!proxy->filter_first)
                proxy_added(client, proxy);
            continue;
        }

        msg = dbus_message_new_method_call(client->service_name,
            proxy->obj_path, proxy->interface, "GetProperties");
        if (msg == NULL)
            return FALSE;

        if (dbus_send_msg_reply_async(client, msg, &proxy->get_all_call, -1,
                get_properties_reply_not_standard, proxy, NULL)
            == FALSE) {
            dbus_message_unref(msg);
            return FALSE;
        }
        proxy->getting_all_prop = TRUE;
        dbus_message_unref(msg);
    }

    return TRUE;
}

static gboolean interfaces_added(DBusConnection* conn, DBusMessage* msg,
    void* user_data)
{
    GDBusClient* client = user_data;
    DBusMessageIter iter;
    const char* path;

    if (dbus_message_iter_init(msg, &iter) == FALSE)
        return TRUE;

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_OBJECT_PATH)
        return TRUE;

    dbus_message_iter_get_basic(&iter, &path);
    dbus_message_iter_next(&iter);

    dbus_client_ref(client);

    parse_interfaces(client, path, &iter, TRUE);

    dbus_client_unref(client);

    return TRUE;
}

static gboolean interfaces_removed(DBusConnection* conn, DBusMessage* msg,
    void* user_data)
{
    GDBusClient* client = user_data;
    DBusMessageIter iter, entry;
    const char* path;

    if (dbus_message_iter_init(msg, &iter) == FALSE)
        return TRUE;

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_OBJECT_PATH)
        return TRUE;

    dbus_message_iter_get_basic(&iter, &path);
    dbus_message_iter_next(&iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
        return TRUE;

    dbus_message_iter_recurse(&iter, &entry);

    dbus_client_ref(client);

    while (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
        const char* interface;

        dbus_message_iter_get_basic(&entry, &interface);
        proxy_remove(client, path, interface);
        dbus_message_iter_next(&entry);
    }

    dbus_client_unref(client);

    return TRUE;
}

static void parse_managed_objects(GDBusClient* client, DBusMessage* msg)
{
    DBusMessageIter iter, dict;

    if (dbus_message_iter_init(msg, &iter) == FALSE)
        return;

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
        return;

    dbus_message_iter_recurse(&iter, &dict);

    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        const char* path;

        dbus_message_iter_recurse(&dict, &entry);

        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_OBJECT_PATH)
            break;

        dbus_message_iter_get_basic(&entry, &path);
        dbus_message_iter_next(&entry);

        parse_interfaces(client, path, &entry, FALSE);

        dbus_message_iter_next(&dict);
    }

    get_properties_non_standard(client);
}

static void get_managed_objects_reply(DBusPendingCall* call, void* user_data)
{
    GDBusClient* client = user_data;
    DBusMessage* reply = dbus_pending_call_steal_reply(call);
    DBusError error;

    if (!client->getting_object_call) {
        /**
         * not in getting object process, do nothing for reply.
         * maybe client already freed.
         */
        return;
    }

    dbus_client_ref(client);

    dbus_error_init(&error);

    if (dbus_set_error_from_message(&error, reply) == TRUE) {
        dbus_error_free(&error);
        goto done;
    }

    parse_managed_objects(client, reply);

done:
    if (client->ready && client->standard)
        client->ready(client, client->ready_data);

    dbus_message_unref(reply);

    client->getting_object_call = FALSE;
    dbus_pending_call_unref(client->get_objects_call);
    client->get_objects_call = NULL;

    refresh_properties(client->proxy_list);

    dbus_client_unref(client);
}

static void get_managed_objects(GDBusClient* client)
{
    DBusMessage* msg;

    if (!client->connected)
        return;

    if ((!client->proxy_added && !client->proxy_removed) || !client->root_path) {
        refresh_properties(client->proxy_list);
        return;
    }

    if (client->getting_object_call)
        return;

    msg = dbus_message_new_method_call(client->service_name,
        client->root_path,
        DBUS_INTERFACE_OBJECT_MANAGER,
        "GetManagedObjects");
    if (msg == NULL)
        return;

    dbus_message_append_args(msg, DBUS_TYPE_INVALID);

    if (dbus_send_msg_reply_async(client, msg, &client->get_objects_call,
            -1, get_managed_objects_reply, client, NULL)
        == FALSE) {
        dbus_message_unref(msg);
        return;
    }
    client->getting_object_call = TRUE;
    dbus_message_unref(msg);
}

static void service_connect(DBusConnection* conn, void* user_data)
{
    GDBusClient* client = user_data;

    dbus_client_ref(client);

    client->connected = TRUE;

    get_managed_objects(client);

    if (client->connect_func)
        client->connect_func(conn, client->connect_data);

    dbus_client_unref(client);
}

static void service_disconnect(DBusConnection* conn, void* user_data)
{
    GDBusClient* client = user_data;

    client->connected = FALSE;

    _dbus_list_clear_full(&client->proxy_list, proxy_free);
    client->proxy_list = NULL;

    if (client->disconn_func)
        client->disconn_func(conn, client->disconn_data);
}

static DBusHandlerResult message_filter(DBusConnection* connection,
    DBusMessage* message, void* user_data)
{
    GDBusClient* client = user_data;
    const char *sender, *path, *interface;

    if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_SIGNAL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    sender = dbus_message_get_sender(message);
    if (sender == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    path = dbus_message_get_path(message);
    interface = dbus_message_get_interface(message);

    if (path == NULL || client->base_path == NULL
        || strncmp(path, client->base_path, strlen(client->base_path)) != 0)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (strcmp(interface, DBUS_INTERFACE_PROPERTIES) == 0)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (client->signal_func)
        client->signal_func(connection, message, client->signal_data);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

GDBusClient* dbus_client_new(DBusConnection* connection,
    const char* service, const char* path)
{
    return dbus_client_new_full(connection, service, path, "/");
}

GDBusClient* dbus_client_new_full(DBusConnection* connection,
    const char* service,
    const char* path,
    const char* root_path)
{
    GDBusClient* client;
    DBusString str;
    char* rule;
    unsigned int i;

    if (!connection || !service)
        return NULL;

    client = calloc(1, sizeof(GDBusClient));
    if (client == NULL)
        return NULL;

    if (dbus_connection_add_filter(connection, message_filter, client, NULL)
        == FALSE) {
        free(client);
        return NULL;
    }

    client->dbus_conn = dbus_connection_ref(connection);
    client->service_name = strdup0(service);
    client->base_path = strdup0(path);
    client->root_path = strdup0(root_path);
    client->connected = FALSE;
    client->watcher = new_dbus_watch(connection);
    client->match_rules = ptr_array_sized_new(1);

    uv_async_queue_init(uv_default_loop(), &client->async_queue, client_uv_async_queue_cb);
    client->async_queue.data = client;
    client->main_thread = uv_thread_self();

    client->watch = dbus_add_service_watch(client->watcher, service,
        service_connect,
        service_disconnect,
        client, NULL);

    if (!root_path)
        return dbus_client_ref(client);

    client->added_watch = dbus_add_signal_watch(client->watcher, service,
        client->root_path,
        DBUS_INTERFACE_OBJECT_MANAGER,
        "InterfacesAdded",
        interfaces_added,
        client, NULL);
    client->removed_watch = dbus_add_signal_watch(client->watcher, service,
        client->root_path,
        DBUS_INTERFACE_OBJECT_MANAGER,
        "InterfacesRemoved",
        interfaces_removed,
        client, NULL);

    if (_dbus_string_init(&str)) {
        _dbus_string_append_printf(&str, "type='signal', sender='%s',"
                                         "path_namespace='%s'",
            client->service_name, client->base_path);
        _dbus_string_copy_data(&str, &rule);
        _dbus_string_free(&str);
        ptr_array_add(&client->match_rules, rule);
    }

    for (i = 0; i < client->match_rules->len; i++) {
        modify_match(client, "AddMatch",
            client->match_rules->data[i]);
    }

    return dbus_client_ref(client);
}

static void dbus_close_uv_async_cb(uv_handle_t* handle)
{
    uv_async_queue_t* async_queue = (uv_async_queue_t*)handle;
    GDBusClient* client = (GDBusClient*)async_queue->data;

    if (client != NULL) {
        free(client->service_name);
        free(client->base_path);
        free(client->root_path);
        free(client);
    }
}

GDBusClient* dbus_client_ref(GDBusClient* client)
{
    if (client == NULL)
        return NULL;

    __sync_fetch_and_add(&client->ref_count, 1);

    return client;
}

void dbus_client_unref(GDBusClient* client)
{
    unsigned int i;

    if (client == NULL)
        return;

    if (__sync_sub_and_fetch(&client->ref_count, 1) > 0)
        return;

    if (client->pending_call != NULL) {
        dbus_pending_call_cancel(client->pending_call);
        dbus_pending_call_unref(client->pending_call);
    }

    client->getting_object_call = FALSE;
    if (client->get_objects_call != NULL) {
        dbus_pending_call_cancel(client->get_objects_call);
        dbus_pending_call_unref(client->get_objects_call);
    }

    for (i = 0; i < client->match_rules->len; i++) {
        modify_match(client, "RemoveMatch",
            client->match_rules->data[i]);
    }

    ptr_array_free(client->match_rules);

    dbus_connection_remove_filter(client->dbus_conn,
        message_filter, client);

    _dbus_list_clear_full(&client->proxy_list, proxy_free);

    /*
     * Don't call disconn_func twice if disconnection
     * was previously reported.
     */
    if (client->disconn_func && client->connected)
        client->disconn_func(client->dbus_conn, client->disconn_data);

    dbus_remove_watch(client->watcher, client->watch);
    dbus_remove_watch(client->watcher, client->added_watch);
    dbus_remove_watch(client->watcher, client->removed_watch);

    dbus_connection_unref(client->dbus_conn);

    uv_async_queue_close(&client->async_queue, dbus_close_uv_async_cb);
}

gboolean dbus_client_set_connect_watch(GDBusClient* client,
    GDBusWatchFunction function, void* user_data)
{
    if (client == NULL)
        return FALSE;

    client->connect_func = function;
    client->connect_data = user_data;

    return TRUE;
}

gboolean dbus_client_set_disconnect_watch(GDBusClient* client,
    GDBusWatchFunction function, void* user_data)
{
    if (client == NULL)
        return FALSE;

    client->disconn_func = function;
    client->disconn_data = user_data;

    return TRUE;
}

gboolean dbus_client_set_signal_watch(GDBusClient* client,
    GDBusMessageFunction function, void* user_data)
{
    if (client == NULL)
        return FALSE;

    client->signal_func = function;
    client->signal_data = user_data;

    return TRUE;
}

gboolean dbus_client_set_ready_watch(GDBusClient* client,
    GDBusClientFunction ready, void* user_data)
{
    if (client == NULL)
        return FALSE;

    client->ready = ready;
    client->ready_data = user_data;

    return TRUE;
}

gboolean dbus_client_set_proxy_handlers(GDBusClient* client,
    GDBusProxyFunction proxy_added_,
    GDBusProxyFunction proxy_removed,
    GDBusProxyPropertyFilterFunction proxy_property_filter,
    GDBusPropertyFunction property_changed,
    void* user_data)
{
    if (client == NULL)
        return FALSE;

    client->proxy_added = proxy_added_;
    client->proxy_removed = proxy_removed;
    client->proxy_property_filter = proxy_property_filter;
    client->property_changed = property_changed;
    client->user_data = user_data;

    if (proxy_added_ || proxy_removed || property_changed || proxy_property_filter)
        get_managed_objects(client);

    return TRUE;
}

gboolean dbus_client_set_proxy_filter(GDBusClient* client,
    GDBusProxyFilterFunction proxy_filter, void* user_data)
{
    if (client == NULL)
        return FALSE;

    client->proxy_filter = proxy_filter;
    client->user_data = user_data;

    return TRUE;
}
