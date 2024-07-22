// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  D-Bus helper library
 *
 *  Copyright (C) 2004-2011  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include <dbus/dbus-hash.h>
#include <dbus/dbus-list.h>
#include <dbus/dbus-string.h>
#include <dbus/dbus.h>

#include "gdbus.h"

#define METHOD_CALL_TIMEOUT (300 * 1000)

#ifndef DBUS_INTERFACE_OBJECT_MANAGER
#define DBUS_INTERFACE_OBJECT_MANAGER DBUS_INTERFACE_DBUS ".ObjectManager"
#endif

struct ptr_array {
    void** data;
    size_t len;
    size_t alloc_len;
};

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
};

struct prop_entry {
    char* name;
    int type;
    DBusMessage* msg;
};

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

static gboolean modify_match(DBusConnection* conn, const char* member,
    const char* rule)
{
    DBusMessage* msg;
    DBusPendingCall* call;

    msg = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
        DBUS_INTERFACE_DBUS, member);
    if (msg == NULL)
        return FALSE;

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &rule,
        DBUS_TYPE_INVALID);

    if (dbus_send_message_with_reply(conn, msg, &call, -1) == FALSE) {
        dbus_message_unref(msg);
        return FALSE;
    }

    if (dbus_pending_call_get_completed(call)) {
        modify_match_reply(call, NULL);
    } else {
        dbus_pending_call_set_notify(call, modify_match_reply, NULL, NULL);
    }

    dbus_pending_call_unref(call);

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

    dbus_message_iter_init(reply, &iter);

    update_properties(proxy, &iter, FALSE, TRUE);

done:
    if (_dbus_hash_table_get_n_entries(proxy->prop_list) != 0)
        proxy_added(client, proxy);

    dbus_message_unref(reply);

    dbus_pending_call_unref(proxy->get_all_call);
    proxy->get_all_call = NULL;

    dbus_client_unref(client);
}

static void get_all_properties(GDBusProxy* proxy)
{
    GDBusClient* client = proxy->client;
    const char* service_name = client->service_name;
    DBusMessage* msg;

    if (proxy->get_all_call)
        return;

    msg = dbus_message_new_method_call(service_name, proxy->obj_path,
        DBUS_INTERFACE_PROPERTIES, "GetAll");
    if (msg == NULL)
        return;

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &proxy->interface,
        DBUS_TYPE_INVALID);

    if (dbus_send_message_with_reply(client->dbus_conn, msg,
            &proxy->get_all_call, -1)
        == FALSE) {
        dbus_message_unref(msg);
        return;
    }

    if (dbus_pending_call_get_completed(proxy->get_all_call)) {
        get_all_properties_reply(proxy->get_all_call, proxy);
    } else {
        dbus_pending_call_set_notify(proxy->get_all_call,
            get_all_properties_reply, proxy, NULL);
    }

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

    if (client->proxy_filter && client->proxy_filter(client, path, interface)) {
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
    proxy->watch = dbus_add_properties_watch(client->dbus_conn,
        client->service_name,
        proxy->obj_path,
        proxy->interface,
        properties_changed,
        proxy, NULL);

    proxy->watch_non_standard = dbus_add_signal_watch(client->dbus_conn,
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

        if (proxy->get_all_call != NULL) {
            dbus_pending_call_cancel(proxy->get_all_call);
            dbus_pending_call_unref(proxy->get_all_call);
            proxy->get_all_call = NULL;
        }

        if (client->proxy_removed)
            client->proxy_removed(proxy, client->user_data);

        dbus_remove_watch(client->dbus_conn, proxy->watch);
        dbus_remove_watch(client->dbus_conn, proxy->watch_non_standard);

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

    if (!client->get_objects_call)
        get_all_properties(proxy);

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
    if (prop == NULL)
        return FALSE;

    if (prop->msg == NULL)
        return FALSE;

    if (dbus_message_iter_init(prop->msg, iter) == FALSE)
        return FALSE;

    return TRUE;
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

        dbus_message_iter_init(reply, &iter);

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
    DBusPendingCall* call;

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

    if (dbus_send_message_with_reply(client->dbus_conn, msg, &call, -1)
        == FALSE) {
        dbus_message_unref(msg);
        refresh_property_free(data);
        return FALSE;
    }

    if (dbus_pending_call_get_completed(call)) {
        refresh_property_reply(call, data);
        refresh_property_free(data);
    } else {
        dbus_pending_call_set_notify(call, refresh_property_reply,
            data, refresh_property_free);
    }

    dbus_pending_call_unref(call);

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
    DBusPendingCall* call;

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

    if (dbus_send_message_with_reply(client->dbus_conn, msg, &call, -1)
        == FALSE) {
        dbus_message_unref(msg);
        free(data);
        return FALSE;
    }

    if (dbus_pending_call_get_completed(call)) {
        set_property_reply(call, data);
        free(data);
    } else {
        dbus_pending_call_set_notify(call, set_property_reply, data, free);
    }

    dbus_pending_call_unref(call);

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
    DBusPendingCall* call;

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

    if (dbus_send_message_with_reply(client->dbus_conn, msg, &call, -1)
        == FALSE) {
        dbus_message_unref(msg);
        free(data);
        return FALSE;
    }

    if (dbus_pending_call_get_completed(call)) {
        set_property_reply(call, data);
        free(data);
    } else {
        dbus_pending_call_set_notify(call, set_property_reply, data, free);
    }

    dbus_pending_call_unref(call);

    dbus_message_unref(msg);

    return TRUE;
}

struct method_call_data {
    GDBusReturnFunction function;
    void* user_data;
    GDBusDestroyFunction destroy;
    bool reply_handled;
    int ref_count;
};

static void method_call_reply(DBusPendingCall* call, void* user_data)
{
    struct method_call_data* data = user_data;
    if (!data->reply_handled && dbus_pending_call_get_completed(call)) {
        data->reply_handled = true;
        DBusMessage* reply = dbus_pending_call_steal_reply(call);
        if (reply) {
            if (data->function)
                data->function(reply, data->user_data);

            if (data->destroy)
                data->destroy(data->user_data);

            dbus_message_unref(reply);
            dbus_pending_call_unref(call);
        }
    }

    data->ref_count--;
    if (data->ref_count == 0)
        free(data);
}

gboolean dbus_proxy_method_call(GDBusProxy* proxy, const char* method,
    GDBusSetupFunction setup,
    GDBusReturnFunction function, void* user_data,
    GDBusDestroyFunction destroy)
{
    struct method_call_data* data;
    GDBusClient* client;
    DBusMessage* msg;
    DBusPendingCall* call;

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
    data->reply_handled = false;
    data->ref_count = 1;

    if (dbus_send_message_with_reply(client->dbus_conn, msg,
            &call, METHOD_CALL_TIMEOUT)
        == FALSE) {
        dbus_message_unref(msg);
        free(data);
        return FALSE;
    }

    if (dbus_pending_call_get_completed(call)) {
        method_call_reply(call, data);
    } else {
        data->ref_count++;
        dbus_pending_call_set_notify(call, method_call_reply, data, NULL);
        if (dbus_pending_call_get_completed(call)) {
            dbus_pending_call_cancel(call);
            method_call_reply(call, data);
        }
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
    DBusMessageIter* iter)
{
    DBusMessageIter dict;

    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
        return;

    dbus_message_iter_recurse(iter, &dict);

    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        const char* interface;

        dbus_message_iter_recurse(&dict, &entry);

        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING)
            break;

        dbus_message_iter_get_basic(&entry, &interface);
        dbus_message_iter_next(&entry);

        parse_properties(client, path, interface, &entry);

        dbus_message_iter_next(&dict);
    }
}

static void get_properties_reply_not_standard(DBusPendingCall* call, void* user_data)
{
    GDBusProxy* proxy = user_data;
    GDBusClient* client = proxy->client;
    DBusMessage* message = dbus_pending_call_steal_reply(call);
    DBusMessageIter array;
    DBusError error;

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
        if (proxy->get_all_call)
            continue;

        client = proxy->client;
        if (client->proxy_property_filter && client->proxy_property_filter(proxy)) {
            if (!proxy->filter_first)
                proxy_added(client, proxy);
            continue;
        }

        msg = dbus_message_new_method_call(client->service_name,
            proxy->obj_path, proxy->interface, "GetProperties");
        if (msg == NULL)
            return FALSE;

        if (dbus_send_message_with_reply(client->dbus_conn, msg,
                &proxy->get_all_call, -1)
            == FALSE) {
            dbus_message_unref(msg);
            return FALSE;
        }

        if (dbus_pending_call_get_completed(proxy->get_all_call)) {
            get_properties_reply_not_standard(proxy->get_all_call, proxy);
        } else {
            dbus_pending_call_set_notify(proxy->get_all_call,
                get_properties_reply_not_standard, proxy, NULL);
        }

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

    parse_interfaces(client, path, &iter);
    get_properties_non_standard(client);

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

        parse_interfaces(client, path, &entry);

        dbus_message_iter_next(&dict);
    }

    get_properties_non_standard(client);
}

static void get_managed_objects_reply(DBusPendingCall* call, void* user_data)
{
    GDBusClient* client = user_data;
    DBusMessage* reply = dbus_pending_call_steal_reply(call);
    DBusError error;

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

    if (client->get_objects_call != NULL)
        return;

    msg = dbus_message_new_method_call(client->service_name,
        client->root_path,
        DBUS_INTERFACE_OBJECT_MANAGER,
        "GetManagedObjects");
    if (msg == NULL)
        return;

    dbus_message_append_args(msg, DBUS_TYPE_INVALID);

    if (dbus_send_message_with_reply(client->dbus_conn, msg,
            &client->get_objects_call, -1)
        == FALSE) {
        dbus_message_unref(msg);
        return;
    }

    if (dbus_pending_call_get_completed(client->get_objects_call)) {
        get_managed_objects_reply(client->get_objects_call, client);
    } else {
        dbus_pending_call_set_notify(client->get_objects_call,
            get_managed_objects_reply,
            client, NULL);
    }

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

    client->match_rules = ptr_array_sized_new(1);

    client->watch = dbus_add_service_watch(connection, service,
        service_connect,
        service_disconnect,
        client, NULL);

    if (!root_path)
        return dbus_client_ref(client);

    client->added_watch = dbus_add_signal_watch(connection, service,
        client->root_path,
        DBUS_INTERFACE_OBJECT_MANAGER,
        "InterfacesAdded",
        interfaces_added,
        client, NULL);
    client->removed_watch = dbus_add_signal_watch(connection, service,
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
        modify_match(client->dbus_conn, "AddMatch",
            client->match_rules->data[i]);
    }

    return dbus_client_ref(client);
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

    if (client->get_objects_call != NULL) {
        dbus_pending_call_cancel(client->get_objects_call);
        dbus_pending_call_unref(client->get_objects_call);
    }

    for (i = 0; i < client->match_rules->len; i++) {
        modify_match(client->dbus_conn, "RemoveMatch",
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

    dbus_remove_watch(client->dbus_conn, client->watch);
    dbus_remove_watch(client->dbus_conn, client->added_watch);
    dbus_remove_watch(client->dbus_conn, client->removed_watch);

    dbus_connection_unref(client->dbus_conn);

    free(client->service_name);
    free(client->base_path);
    free(client->root_path);

    free(client);
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
    GDBusProxyFilterFunction proxy_filter,
    void* user_data)
{
    if (client == NULL)
        return FALSE;

    client->proxy_filter = proxy_filter;

    return TRUE;
}
