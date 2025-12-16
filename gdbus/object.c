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

#include <stdio.h>
#include <string.h>

#include <dbus/dbus-list.h>
#include <dbus/dbus-string.h>
#include <dbus/dbus.h>
#include <uv.h>

#ifdef __NuttX__
#include <nuttx/tls_task.h>
#include <pthread.h>
#endif

#include "gdbus-internal.h"

#define DBUS_INTERFACE_OBJECT_MANAGER "org.freedesktop.DBus.ObjectManager"

#ifndef DBUS_ERROR_UNKNOWN_PROPERTY
#define DBUS_ERROR_UNKNOWN_PROPERTY "org.freedesktop.DBus.Error.UnknownProperty"
#endif

#ifndef DBUS_ERROR_PROPERTY_READ_ONLY
#define DBUS_ERROR_PROPERTY_READ_ONLY "org.freedesktop.DBus.Error.PropertyReadOnly"
#endif

#define DBUS_ERROR_OOM "No memory"

#define G_DBUS_ANNOTATE(name_, value_)                     \
    "<annotation name=\"org.freedesktop.DBus." name_ "\" " \
    "value=\"" value_ "\"/>"

#define G_DBUS_ANNOTATE_DEPRECATED \
    G_DBUS_ANNOTATE("Deprecated", "true")

#define G_DBUS_ANNOTATE_NOREPLY \
    G_DBUS_ANNOTATE("Method.NoReply", "true")

struct generic_data {
    unsigned int refcount;
    DBusConnection* conn;
    char* path;
    DBusList* interfaces;
    DBusList* objects;
    DBusList* added;
    DBusList* removed;
    gboolean pending_prop;
    char* introspect;
    struct generic_data* parent;
    uv_idle_t handle;
};

struct interface_data {
    char* name;
    const GDBusMethodTable* methods;
    const GDBusSignalTable* signals;
    const GDBusPropertyTable* properties;
    DBusList* pending_prop;
    void* user_data;
    GDBusDestroyFunction destroy;
};

struct security_data {
    GDBusPendingReply pending;
    DBusMessage* message;
    const GDBusMethodTable* method;
    void* iface_user_data;
};

struct property_data {
    DBusConnection* conn;
    GDBusPendingPropertySet id;
    DBusMessage* message;
};

typedef struct {
    int global_flags;
    struct generic_data* root;
    DBusList* generic_pending;

    GDBusPendingReply next_pending;
    DBusList* pending_security;

    const GDBusSecurityTable* security_table;
    GDBusPendingPropertySet next_pending_property;
    DBusList* pending_property_set;
} gdbus_object_globals;

static void process_changes(uv_idle_t* handle);
static void process_properties_from_interface(struct generic_data* data,
    struct interface_data* iface);
static void process_property_changes(struct generic_data* data);

#if defined(CONFIG_BUILD_FLAT) || defined(CONFIG_BUILD_PROTECTED)
/* TLS index for gdbus_object_t */
static int gdbus_object_tls_index;

/* Init once only by uv_once */
static void gdbus_object_index_alloc(void)
{
    gdbus_object_tls_index = task_tls_alloc(dbus_free);

    ASSERT(gdbus_object_tls_index >= 0);
}

static gdbus_object_globals* gdbus_object_get(void)
{
    static pthread_once_t once_guard = PTHREAD_ONCE_INIT;
    gdbus_object_globals* gdbus_object = NULL;

    pthread_once(&once_guard, gdbus_object_index_alloc);

    gdbus_object = (gdbus_object_globals*)task_tls_get_value(gdbus_object_tls_index);
    if (gdbus_object == NULL) {
        gdbus_object = calloc(1, sizeof(gdbus_object_globals));
        if (gdbus_object != NULL) {
            gdbus_object->global_flags = 0;
            gdbus_object->root = NULL;
            gdbus_object->generic_pending = NULL;
            gdbus_object->next_pending = 1;
            gdbus_object->pending_security = NULL;
            gdbus_object->security_table = NULL;
            gdbus_object->next_pending_property = 1;
            gdbus_object->pending_property_set = NULL;
            task_tls_set_value(gdbus_object_tls_index, (uintptr_t)gdbus_object);
        }
    }

    _dbus_assert(gdbus_object != NULL);
    return gdbus_object;
}
#else
/* Kernel build should use gdbus_object */
static gdbus_object_globals* gdbus_object_get(void)
{
    static gdbus_object_globals gdbus_object = {
        .global_flags = 0,
        .root = NULL,
        .generic_pending = NULL,
        .next_pending = 1,
        .pending_security = NULL,
        .security_table = NULL,
        .next_pending_property = 1,
        .pending_property_set = NULL,
    };

    return &gdbus_object;
}
#endif

#define global_flags gdbus_object_get()->global_flags
#define root gdbus_object_get()->root
#define generic_pending gdbus_object_get()->generic_pending
#define next_pending gdbus_object_get()->next_pending
#define pending_security gdbus_object_get()->pending_security
#define security_table gdbus_object_get()->security_table
#define next_pending_property gdbus_object_get()->next_pending_property
#define pending_property_set gdbus_object_get()->pending_property_set

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

static void append_arguments_printf(DBusString* str, const GDBusArgInfo* args,
    const char* direction)
{
    while (args && args->name) {
        _dbus_string_append_printf(str,
            "<arg name=\"%s\" type=\"%s\"",
            args->name, args->signature);

        if (direction)
            _dbus_string_append_printf(str,
                " direction=\"%s\"/>\n", direction);
        else
            _dbus_string_append_printf(str, "/>\n");

        args++;
    }
}

static gboolean check_experimental(int flags, int flag)
{
    if (!(flags & flag))
        return FALSE;

    return !(global_flags & G_DBUS_FLAG_ENABLE_EXPERIMENTAL);
}

static void append_annotation(DBusString* str, int flags,
    int flag_type, const char* append_annotate)
{
    if (flags & flag_type) {
        _dbus_string_append(str, append_annotate);
    }
}

static void generate_method_xml(DBusString* str, const GDBusMethodTable* method)
{
    if (check_experimental(method->flags, G_DBUS_METHOD_FLAG_EXPERIMENTAL))
        return;

    _dbus_string_append_printf(str, "<method name=\"%s\">", method->name);
    append_arguments_printf(str, method->in_args, "in");
    append_arguments_printf(str, method->out_args, "out");

    append_annotation(str, method->flags, G_DBUS_METHOD_FLAG_DEPRECATED,
        G_DBUS_ANNOTATE_DEPRECATED);
    append_annotation(str, method->flags, G_DBUS_METHOD_FLAG_NOREPLY,
        G_DBUS_ANNOTATE_NOREPLY);

    _dbus_string_append_printf(str, "</method>");
}

static void generate_signal_xml(DBusString* str, const GDBusSignalTable* signal)
{
    if (check_experimental(signal->flags, G_DBUS_SIGNAL_FLAG_EXPERIMENTAL))
        return;

    _dbus_string_append_printf(str, "<signal name=\"%s\">", signal->name);
    append_arguments_printf(str, signal->args, NULL);

    append_annotation(str, signal->flags, G_DBUS_SIGNAL_FLAG_DEPRECATED,
        G_DBUS_ANNOTATE_DEPRECATED);
    _dbus_string_append_printf(str, "</signal>\n");
}

static void generate_property_xml(DBusString* str, const GDBusPropertyTable* property)
{
    if (check_experimental(property->flags, G_DBUS_PROPERTY_FLAG_EXPERIMENTAL))
        return;

    _dbus_string_append_printf(str, "<property name=\"%s\" type=\"%s\" access=\"%s%s\">",
        property->name, property->type,
        property->get ? "read" : "",
        property->set ? "write" : "");

    append_annotation(str, property->flags, G_DBUS_PROPERTY_FLAG_DEPRECATED,
        G_DBUS_ANNOTATE_DEPRECATED);
    _dbus_string_append_printf(str, "</property>");
}

static void generate_interface_xml(DBusString* str, struct interface_data* iface)
{
    const GDBusMethodTable* method = iface->methods;
    while (method && method->name) {
        generate_method_xml(str, method);
        method++;
    }

    const GDBusSignalTable* signal = iface->signals;
    while (signal && signal->name) {
        generate_signal_xml(str, signal);
        signal++;
    }

    const GDBusPropertyTable* property = iface->properties;
    while (property && property->name) {
        generate_property_xml(str, property);
        property++;
    }
}

static void append_xml_header_node_begin(DBusString* str)
{
    _dbus_string_append_printf(str, DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);
    _dbus_string_append_printf(str, "<node>");
}

static void append_xml_node_end(DBusString* str)
{
    _dbus_string_append_printf(str, "</node>");
}

static void append_interfaces_xml(DBusString* str, DBusList* interfaces)
{
    DBusList* list = _dbus_list_get_first_link(&interfaces);
    while (list != NULL) {
        struct interface_data* iface = list->data;
        _dbus_string_append_printf(str, "<interface name=\"%s\">", iface->name);
        generate_interface_xml(str, iface);
        _dbus_string_append_printf(str, "</interface>");
        list = _dbus_list_get_next_link(&interfaces, list);
    }
}

static void append_child_nodes_xml(DBusString* str, DBusConnection* conn, const char* path)
{
    char** children = NULL;
    if (dbus_connection_list_registered(conn, path, &children)) {
        for (int i = 0; children[i]; i++) {
            _dbus_string_append_printf(str, "<node name=\"%s\"/>", children[i]);
        }
        dbus_free_string_array(children);
    }
}

static void generate_introspection_xml(DBusConnection* conn,
    struct generic_data* data, const char* path)
{
    DBusString str;

    free(data->introspect);
    data->introspect = NULL;

    if (!_dbus_string_init(&str)) {
        return;
    }

    append_xml_header_node_begin(&str);

    append_interfaces_xml(&str, data->interfaces);
    append_child_nodes_xml(&str, conn, path);

    append_xml_node_end(&str);

    _dbus_string_copy_data(&str, &data->introspect);
    _dbus_string_free(&str);
}

static DBusMessage* introspect(DBusConnection* connection,
    DBusMessage* message, void* user_data)
{
    struct generic_data* data = user_data;
    DBusMessage* reply;

    if (data->introspect == NULL)
        generate_introspection_xml(connection, data,
            dbus_message_get_path(message));

    reply = dbus_message_new_method_return(message);
    if (reply == NULL)
        return NULL;

    dbus_message_append_args(reply, DBUS_TYPE_STRING, &data->introspect,
        DBUS_TYPE_INVALID);

    return reply;
}

static DBusHandlerResult process_message(DBusConnection* connection,
    DBusMessage* message, const GDBusMethodTable* method,
    void* iface_user_data)
{
    DBusMessage* reply;

    reply = method->function(connection, message, iface_user_data);

    if (method->flags & G_DBUS_METHOD_FLAG_NOREPLY || dbus_message_get_no_reply(message)) {
        if (reply != NULL)
            dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (method->flags & G_DBUS_METHOD_FLAG_ASYNC) {
        if (reply == NULL)
            return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (reply == NULL)
        return DBUS_HANDLER_RESULT_NEED_MEMORY;

    dbus_send_message(connection, reply);

    return DBUS_HANDLER_RESULT_HANDLED;
}

void dbus_pending_success(DBusConnection* connection,
    GDBusPendingReply pending_reply)
{
    DBusList* list = _dbus_list_get_first_link(&pending_security);

    while (list) {
        struct security_data* secdata = list->data;
        list = _dbus_list_get_next_link(&pending_security, list);

        if (secdata->pending != pending_reply)
            continue;

        _dbus_list_remove(&pending_security, secdata);

        process_message(connection, secdata->message,
            secdata->method, secdata->iface_user_data);

        dbus_message_unref(secdata->message);
        free(secdata);
        return;
    }
}

void dbus_pending_error_valist(DBusConnection* connection,
    GDBusPendingReply pending_reply, const char* name,
    const char* format, va_list args)
{
    DBusList* list = _dbus_list_get_first_link(&pending_security);

    while (list != NULL) {
        struct security_data* secdata = list->data;
        DBusList* next = _dbus_list_get_next_link(&pending_security, list);

        if (secdata->pending == pending_reply) {
            _dbus_list_remove(&pending_security, secdata);
            dbus_send_error_valist(connection, secdata->message, name, format, args);
            dbus_message_unref(secdata->message);
            free(secdata);
            return;
        }

        list = next;
    }
}

void dbus_pending_error(DBusConnection* connection,
    GDBusPendingReply pending_reply,
    const char* name, const char* format, ...)
{
    va_list args;

    va_start(args, format);

    dbus_pending_error_valist(connection, pending_reply, name, format, args);

    va_end(args);
}

struct builtin_security_data {
    DBusConnection* conn;
    GDBusPendingReply pending;
};

static void builtin_security_result(dbus_bool_t authorized, void* user_data)
{
    struct builtin_security_data* data = user_data;

    if (authorized == TRUE)
        dbus_pending_success(data->conn, data->pending);
    else
        dbus_pending_error(data->conn, data->pending,
            DBUS_ERROR_AUTH_FAILED, NULL);

    free(data);
}

static void builtin_security_function(DBusConnection* conn,
    const char* action,
    gboolean interaction,
    GDBusPendingReply pending_reply)
{
    struct builtin_security_data* data;

    data = calloc(1, sizeof(struct builtin_security_data));
    if (data == NULL) {
        dbus_pending_error(conn, pending_reply, DBUS_ERROR_NO_MEMORY,
            "Failed to allocate memory for security data");
        return;
    }
    data->conn = conn;
    data->pending = pending_reply;

    if (dbus_polkit_check_authorization(conn, action, interaction,
            builtin_security_result, data, 30000)
        < 0)
        dbus_pending_error(conn, pending_reply, NULL, NULL);
}

static gboolean check_privilege(DBusConnection* conn, DBusMessage* msg,
    const GDBusMethodTable* method, void* iface_user_data)
{
    gboolean interaction = FALSE;
    const GDBusSecurityTable* security = security_table;

    while (security && security->privilege) {
        if (security->privilege == method->privilege) {
            struct security_data* secdata = calloc(1, sizeof(struct security_data));
            if (secdata == NULL) {
                error("Failed to allocate memory for security data");
                return FALSE;
            }
            secdata->pending = next_pending++;
            secdata->message = dbus_message_ref(msg);
            secdata->method = method;
            secdata->iface_user_data = iface_user_data;

            _dbus_list_prepend(&pending_security, secdata);

            interaction = (security->flags & G_DBUS_SECURITY_FLAG_ALLOW_INTERACTION)
                ? TRUE
                : FALSE;

            if (!(security->flags & G_DBUS_SECURITY_FLAG_BUILTIN) && security->function) {
                security->function(conn, security->action, interaction, secdata->pending);
            } else {
                builtin_security_function(conn, security->action, interaction,
                    secdata->pending);
            }

            return TRUE;
        }
        security++;
    }

    return FALSE;
}

static struct property_data* remove_pending_property_data(GDBusPendingPropertySet id)
{
    DBusList* l = _dbus_list_get_first_link(&pending_property_set);

    while (l != NULL) {
        struct property_data* propdata = l->data;
        if (propdata->id == id) {
            _dbus_list_remove_link(&pending_property_set, l);
            return propdata;
        }
        l = _dbus_list_get_next_link(&pending_property_set, l);
    }

    return NULL;
}

void dbus_pending_property_success(GDBusPendingPropertySet id)
{
    struct property_data* propdata;

    propdata = remove_pending_property_data(id);
    if (propdata == NULL)
        return;

    dbus_send_reply(propdata->conn, propdata->message,
        DBUS_TYPE_INVALID);
    dbus_message_unref(propdata->message);
    free(propdata);
}

void dbus_pending_property_error_valist(GDBusPendingReply id,
    const char* name, const char* format,
    va_list args)
{
    struct property_data* propdata;

    propdata = remove_pending_property_data(id);
    if (propdata == NULL)
        return;

    dbus_send_error_valist(propdata->conn, propdata->message, name,
        format, args);

    dbus_message_unref(propdata->message);
    free(propdata);
}

void dbus_pending_property_error(GDBusPendingReply id, const char* name,
    const char* format, ...)
{
    va_list args;

    va_start(args, format);

    dbus_pending_property_error_valist(id, name, format, args);

    va_end(args);
}

static void reset_parent(gpointer data, gpointer user_data)
{
    struct generic_data* child = data;
    struct generic_data* parent = user_data;

    child->parent = parent;
}

static void append_property(struct interface_data* iface,
    const GDBusPropertyTable* p, DBusMessageIter* dict)
{
    DBusMessageIter entry, value;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL,
        &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &p->name);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, p->type,
        &value);

    p->get(p, &value, iface->user_data);

    dbus_message_iter_close_container(&entry, &value);
    dbus_message_iter_close_container(dict, &entry);
}

static void append_properties(struct interface_data* data, DBusMessageIter* iter)
{
    DBusMessageIter dict;
    const GDBusPropertyTable* p = data->properties;

    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
        DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
            DBUS_TYPE_STRING_AS_STRING
                DBUS_TYPE_VARIANT_AS_STRING
                    DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
        &dict);

    while (p && p->name) {
        if (!check_experimental(p->flags, G_DBUS_PROPERTY_FLAG_EXPERIMENTAL)
            && p->get != NULL
            && (p->exists == NULL || p->exists(p, data->user_data))) {
            append_property(data, p, &dict);
        }
        p++;
    }

    dbus_message_iter_close_container(iter, &dict);
}

static void append_interface(gpointer data, gpointer user_data)
{
    struct interface_data* iface = data;
    DBusMessageIter* array = user_data;
    DBusMessageIter entry;

    dbus_message_iter_open_container(array, DBUS_TYPE_DICT_ENTRY, NULL,
        &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &iface->name);
    append_properties(data, &entry);
    dbus_message_iter_close_container(array, &entry);
}

static void emit_interfaces_added(struct generic_data* data)
{
    DBusMessage* signal;
    DBusMessageIter iter, array;

    if (root == NULL || data == root)
        return;

    /* Emit InterfacesAdded on the parent first so it appears first on the
     * bus as child objects may point to it.
     */
    if (data->parent && data->parent->added)
        emit_interfaces_added(data->parent);

    signal = dbus_message_new_signal(root->path,
        DBUS_INTERFACE_OBJECT_MANAGER,
        "InterfacesAdded");
    if (signal == NULL)
        return;

    dbus_message_iter_init_append(signal, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
        &data->path);

    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
        DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
            DBUS_TYPE_STRING_AS_STRING
                DBUS_TYPE_ARRAY_AS_STRING
                    DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                        DBUS_TYPE_STRING_AS_STRING
                            DBUS_TYPE_VARIANT_AS_STRING
                                DBUS_DICT_ENTRY_END_CHAR_AS_STRING
                                    DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
        &array);

    _dbus_list_foreach(&data->added, append_interface, &array);
    _dbus_list_clear(&data->added);

    dbus_message_iter_close_container(&iter, &array);

    /* Use dbus_connection_send to avoid recursive calls to dbus_flush */
    dbus_connection_send(data->conn, signal, NULL);
    dbus_message_unref(signal);
}

static struct interface_data* find_interface(DBusList* interfaces,
    const char* name)
{
    DBusList* list = _dbus_list_get_first_link(&interfaces);

    if (name == NULL)
        return NULL;

    while (list != NULL) {
        struct interface_data* iface = list->data;
        if (!strcmp0(name, iface->name))
            return iface;

        list = _dbus_list_get_next_link(&interfaces, list);
    }

    return NULL;
}

static gboolean dbus_args_have_signature(const GDBusArgInfo* args,
    DBusMessage* message)
{
    const char* sig = dbus_message_get_signature(message);
    const char* p = NULL;

    while (args && args->signature && *sig) {
        p = args->signature;

        while (*sig && *p) {
            if (*p++ != *sig++) {
                return FALSE;
            }
        }
        args++;
    }

    return !(*sig || (p && *p) || (args && args->signature));
}

static void add_pending(struct generic_data* data)
{
    if (uv_is_active((const uv_handle_t*)&data->handle) != 0)
        return;

    if (uv_idle_start(&data->handle, process_changes) == 0) {
        return;
    }

    _dbus_list_append(&generic_pending, data);
}

static gboolean remove_interface(struct generic_data* data, const char* name)
{
    struct interface_data* iface;

    iface = find_interface(data->interfaces, name);
    if (iface == NULL)
        return FALSE;

    process_properties_from_interface(data, iface);

    _dbus_list_remove(&data->interfaces, iface);

    if (iface->destroy) {
        iface->destroy(iface->user_data);
        iface->user_data = NULL;
    }

    /*
     * Interface being removed was just added, on the same mainloop
     * iteration? Don't send any signal
     */
    if (_dbus_list_find_last(&data->added, iface)) {
        _dbus_list_remove(&data->added, iface);
        free(iface->name);
        free(iface);
        return TRUE;
    }

    if (data->parent == NULL) {
        free(iface->name);
        free(iface);
        return TRUE;
    }

    _dbus_list_prepend(&data->removed, iface->name);
    free(iface);

    add_pending(data);

    return TRUE;
}

static struct generic_data* invalidate_parent_data(DBusConnection* conn,
    const char* child_path)
{
    struct generic_data *data = NULL, *child = NULL, *parent = NULL;
    char *parent_path, *slash;

    parent_path = strdup0(child_path);
    slash = strrchr(parent_path, '/');
    if (slash == NULL)
        goto done;

    if (slash == parent_path && parent_path[1] != '\0')
        parent_path[1] = '\0';
    else
        *slash = '\0';

    if (!strlen(parent_path))
        goto done;

    if (dbus_connection_get_object_path_data(conn, parent_path, (void*)&data) == FALSE) {
        goto done;
    }

    parent = invalidate_parent_data(conn, parent_path);

    if (data == NULL) {
        data = parent;
        if (data == NULL)
            goto done;
    }

    free(data->introspect);
    data->introspect = NULL;

    if (!dbus_connection_get_object_path_data(conn, child_path, (void*)&child))
        goto done;

    if (child == NULL || _dbus_list_find_last(&data->objects, child) != NULL)
        goto done;

    _dbus_list_prepend(&data->objects, child);
    child->parent = data;

done:
    free(parent_path);
    return data;
}

static inline const GDBusPropertyTable* find_property(const GDBusPropertyTable* properties,
    const char* name)
{
    const GDBusPropertyTable* p;

    for (p = properties; p && p->name; p++) {
        if (strcmp0(name, p->name) != 0)
            continue;

        if (check_experimental(p->flags,
                G_DBUS_PROPERTY_FLAG_EXPERIMENTAL))
            break;

        return p;
    }

    return NULL;
}

static DBusMessage* properties_get(DBusConnection* connection,
    DBusMessage* message, void* user_data)
{
    struct generic_data* data = user_data;
    struct interface_data* iface;
    const GDBusPropertyTable* property;
    const char *interface, *name;
    DBusMessageIter iter, value;
    DBusMessage* reply;

    if (!dbus_message_get_args(message, NULL,
            DBUS_TYPE_STRING, &interface,
            DBUS_TYPE_STRING, &name,
            DBUS_TYPE_INVALID))
        return NULL;

    iface = find_interface(data->interfaces, interface);
    if (iface == NULL)
        return dbus_create_error(message, DBUS_ERROR_INVALID_ARGS,
            "No such interface '%s'", interface);

    property = find_property(iface->properties, name);
    if (property == NULL)
        return dbus_create_error(message, DBUS_ERROR_INVALID_ARGS,
            "No such property '%s'", name);

    if (property->exists != NULL && !property->exists(property, iface->user_data))
        return dbus_create_error(message, DBUS_ERROR_INVALID_ARGS,
            "No such property '%s'", name);

    if (property->get == NULL)
        return dbus_create_error(message, DBUS_ERROR_INVALID_ARGS,
            "Property '%s' is not readable", name);

    reply = dbus_message_new_method_return(message);
    if (reply == NULL)
        return NULL;

    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT,
        property->type, &value);

    if (!property->get(property, &value, iface->user_data)) {
        dbus_message_unref(reply);
        return NULL;
    }

    dbus_message_iter_close_container(&iter, &value);

    return reply;
}

static DBusMessage* properties_get_all(DBusConnection* connection,
    DBusMessage* message, void* user_data)
{
    struct generic_data* data = user_data;
    struct interface_data* iface;
    const char* interface;
    DBusMessageIter iter;
    DBusMessage* reply;

    if (!dbus_message_get_args(message, NULL,
            DBUS_TYPE_STRING, &interface,
            DBUS_TYPE_INVALID))
        return NULL;

    iface = find_interface(data->interfaces, interface);
    if (iface == NULL)
        return dbus_create_error(message, DBUS_ERROR_INVALID_ARGS,
            "No such interface '%s'", interface);

    reply = dbus_message_new_method_return(message);
    if (reply == NULL)
        return NULL;

    dbus_message_iter_init_append(reply, &iter);

    append_properties(iface, &iter);

    return reply;
}

static char* validate_arguments(DBusMessage* message, DBusMessageIter* iter,
    struct generic_data* data, struct interface_data** iface_out, const char** name)
{
    const char* interface = NULL;
    char* error_msg = NULL;

    do {
        if (!dbus_message_iter_init(message, iter)) {
            if (asprintf(&error_msg, "No arguments given") < 0)
                error_msg = DBUS_ERROR_OOM;
            break;
        }

        if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_STRING) {
            if (asprintf(&error_msg, "Invalid argument type: '%c'",
                    dbus_message_iter_get_arg_type(iter))
                < 0)
                error_msg = DBUS_ERROR_OOM;
            break;
        }

        dbus_message_iter_get_basic(iter, &interface);
        dbus_message_iter_next(iter);

        if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_STRING) {
            if (asprintf(&error_msg, "Invalid argument type: '%c'",
                    dbus_message_iter_get_arg_type(iter))
                < 0)
                error_msg = DBUS_ERROR_OOM;
            break;
        }

        dbus_message_iter_get_basic(iter, name);
        dbus_message_iter_next(iter);

        if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_VARIANT) {
            if (asprintf(&error_msg, "Invalid argument type: '%c'",
                    dbus_message_iter_get_arg_type(iter))
                < 0)
                error_msg = DBUS_ERROR_OOM;
            break;
        }

        *iface_out = find_interface(data->interfaces, interface);
        if (*iface_out == NULL) {
            if (asprintf(&error_msg, "No such interface '%s'", interface) < 0)
                error_msg = DBUS_ERROR_OOM;
            break;
        }

    } while (0);

    return error_msg;
}

static DBusMessage* properties_set(DBusConnection* connection,
    DBusMessage* message, void* user_data)
{
    struct generic_data* data = user_data;
    DBusMessageIter iter, sub;
    struct interface_data* iface = NULL;
    const GDBusPropertyTable* property;
    const char* name = NULL;
    struct property_data* propdata;
    gboolean valid_signature;
    char* signature;

    char* err_str = validate_arguments(message, &iter, data, &iface, &name);
    if (err_str) {
        DBusMessage* reply = dbus_create_error(message, DBUS_ERROR_INVALID_ARGS,
            "%s", err_str);
        free(err_str);
        return reply;
    }

    property = find_property(iface->properties, name);
    if (property == NULL)
        return dbus_create_error(message,
            DBUS_ERROR_UNKNOWN_PROPERTY,
            "No such property '%s'", name);

    if (property->set == NULL)
        return dbus_create_error(message,
            DBUS_ERROR_PROPERTY_READ_ONLY,
            "Property '%s' is not writable", name);

    if (property->exists != NULL && !property->exists(property, iface->user_data))
        return dbus_create_error(message,
            DBUS_ERROR_UNKNOWN_PROPERTY,
            "No such property '%s'", name);

    dbus_message_iter_recurse(&iter, &sub);
    signature = dbus_message_iter_get_signature(&sub);
    valid_signature = strcmp0(signature, property->type) ? FALSE : TRUE;
    dbus_free(signature);
    if (!valid_signature)
        return dbus_create_error(message,
            DBUS_ERROR_INVALID_SIGNATURE,
            "Invalid signature for '%s'", name);

    propdata = malloc(sizeof(struct property_data));
    if (propdata == NULL) {
        return dbus_create_error(message, DBUS_ERROR_NO_MEMORY,
            "Failed to allocate memory for property data");
    }

    propdata->id = next_pending_property++;
    propdata->message = dbus_message_ref(message);
    propdata->conn = connection;
    _dbus_list_prepend(&pending_property_set, propdata);

    property->set(property, &sub, propdata->id, iface->user_data);

    return NULL;
}

static const GDBusMethodTable properties_methods[] = {
    { GDBUS_METHOD("Get",
        GDBUS_ARGS({ "interface", "s" }, { "name", "s" }),
        GDBUS_ARGS({ "value", "v" }),
        properties_get) },
    { GDBUS_ASYNC_METHOD("Set",
        GDBUS_ARGS({ "interface", "s" }, { "name", "s" },
            { "value", "v" }),
        NULL,
        properties_set) },
    { GDBUS_METHOD("GetAll",
        GDBUS_ARGS({ "interface", "s" }),
        GDBUS_ARGS({ "properties", "a{sv}" }),
        properties_get_all) },
    {}
};

static const GDBusSignalTable properties_signals[] = {
    { GDBUS_SIGNAL("PropertiesChanged",
        GDBUS_ARGS({ "interface", "s" },
            { "changed_properties", "a{sv}" },
            { "invalidated_properties", "as" })) },
    {}
};

static void append_name(gpointer data, gpointer user_data)
{
    char* name = data;
    DBusMessageIter* iter = user_data;

    dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &name);
}

static void emit_interfaces_removed(struct generic_data* data)
{
    DBusMessage* signal;
    DBusMessageIter iter, array;

    if (root == NULL || data == root)
        return;

    signal = dbus_message_new_signal(root->path,
        DBUS_INTERFACE_OBJECT_MANAGER,
        "InterfacesRemoved");
    if (signal == NULL)
        return;

    dbus_message_iter_init_append(signal, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
        &data->path);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
        DBUS_TYPE_STRING_AS_STRING, &array);

    _dbus_list_foreach(&data->removed, append_name, &array);
    _dbus_list_clear_full(&data->removed, free);

    dbus_message_iter_close_container(&iter, &array);

    /* Use dbus_connection_send to avoid recursive calls to dbus_flush */
    dbus_connection_send(data->conn, signal, NULL);
    dbus_message_unref(signal);
}

static void remove_pending(struct generic_data* data)
{
    uv_idle_stop(&data->handle);

    _dbus_list_remove(&generic_pending, data);
}

static void process_changes(uv_idle_t* handle)
{
    struct generic_data* data = handle->data;

    remove_pending(data);

    if (data->added != NULL)
        emit_interfaces_added(data);

    /* Flush pending properties */
    if (data->pending_prop == TRUE)
        process_property_changes(data);

    if (data->removed != NULL)
        emit_interfaces_removed(data);
}

static void generic_unregister(DBusConnection* connection, void* user_data)
{
    struct generic_data* data = user_data;
    struct generic_data* parent = data->parent;

    if (parent != NULL)
        _dbus_list_remove(&parent->objects, data);

    process_changes(&data->handle);
    uv_close((uv_handle_t*)&data->handle, NULL);

    _dbus_list_foreach(&data->objects, reset_parent, data->parent);
    _dbus_list_clear(&data->objects);

    dbus_connection_unref(data->conn);
    free(data->introspect);
    free(data->path);
    free(data);
}

static DBusHandlerResult generic_message(DBusConnection* connection,
    DBusMessage* message, void* user_data)
{
    struct generic_data* data = user_data;
    struct interface_data* iface;
    const GDBusMethodTable* method;
    const char* interface;

    interface = dbus_message_get_interface(message);

    iface = find_interface(data->interfaces, interface);
    if (iface == NULL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    for (method = iface->methods; method && method->name && method->function; method++) {

        if (dbus_message_is_method_call(message, iface->name, method->name) == FALSE)
            continue;

        if (check_experimental(method->flags,
                G_DBUS_METHOD_FLAG_EXPERIMENTAL))
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        if (dbus_args_have_signature(method->in_args, message) == FALSE)
            continue;

        if (check_privilege(connection, message, method, iface->user_data) == TRUE)
            return DBUS_HANDLER_RESULT_HANDLED;

        return process_message(connection, message, method, iface->user_data);
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusObjectPathVTable generic_table = {
    .unregister_function = generic_unregister,
    .message_function = generic_message,
};

static const GDBusMethodTable introspect_methods[] = {
    { GDBUS_METHOD("Introspect", NULL,
        GDBUS_ARGS({ "xml", "s" }), introspect) },
    {}
};

static void append_interfaces(struct generic_data* data, DBusMessageIter* iter)
{
    DBusMessageIter array;

    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
        DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
            DBUS_TYPE_STRING_AS_STRING
                DBUS_TYPE_ARRAY_AS_STRING
                    DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                        DBUS_TYPE_STRING_AS_STRING
                            DBUS_TYPE_VARIANT_AS_STRING
                                DBUS_DICT_ENTRY_END_CHAR_AS_STRING
                                    DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
        &array);

    _dbus_list_foreach(&data->interfaces, append_interface, &array);

    dbus_message_iter_close_container(iter, &array);
}

static void append_object(gpointer data, gpointer user_data)
{
    struct generic_data* child = data;
    DBusMessageIter* array = user_data;
    DBusMessageIter entry;

    dbus_message_iter_open_container(array, DBUS_TYPE_DICT_ENTRY, NULL,
        &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_OBJECT_PATH,
        &child->path);
    append_interfaces(child, &entry);
    dbus_message_iter_close_container(array, &entry);

    _dbus_list_foreach(&child->objects, append_object, user_data);
}

static DBusMessage* get_objects(DBusConnection* connection,
    DBusMessage* message, void* user_data)
{
    struct generic_data* data = user_data;
    DBusMessage* reply;
    DBusMessageIter iter;
    DBusMessageIter array;

    reply = dbus_message_new_method_return(message);
    if (reply == NULL)
        return NULL;

    dbus_message_iter_init_append(reply, &iter);

    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
        DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
            DBUS_TYPE_OBJECT_PATH_AS_STRING
                DBUS_TYPE_ARRAY_AS_STRING
                    DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                        DBUS_TYPE_STRING_AS_STRING
                            DBUS_TYPE_ARRAY_AS_STRING
                                DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                    DBUS_TYPE_STRING_AS_STRING
                                        DBUS_TYPE_VARIANT_AS_STRING
                                            DBUS_DICT_ENTRY_END_CHAR_AS_STRING
                                                DBUS_DICT_ENTRY_END_CHAR_AS_STRING
                                                    DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
        &array);

    _dbus_list_foreach(&data->objects, append_object, &array);

    dbus_message_iter_close_container(&iter, &array);

    return reply;
}

static const GDBusMethodTable manager_methods[] = {
    { GDBUS_METHOD("GetManagedObjects", NULL,
        GDBUS_ARGS({ "objects", "a{oa{sa{sv}}}" }), get_objects) },
    {}
};

static const GDBusSignalTable manager_signals[] = {
    { GDBUS_SIGNAL("InterfacesAdded",
        GDBUS_ARGS({ "object", "o" },
            { "interfaces", "a{sa{sv}}" })) },
    { GDBUS_SIGNAL("InterfacesRemoved",
        GDBUS_ARGS({ "object", "o" }, { "interfaces", "as" })) },
    {}
};

static gboolean check_methods_experimental(const GDBusMethodTable* methods)
{
    const GDBusMethodTable* method = methods;
    while (method && method->name) {
        if (!check_experimental(method->flags, G_DBUS_METHOD_FLAG_EXPERIMENTAL))
            return FALSE;
        method++;
    }
    return TRUE;
}

static gboolean check_signals_experimental(const GDBusSignalTable* signals)
{
    const GDBusSignalTable* signal = signals;
    while (signal && signal->name) {
        if (!check_experimental(signal->flags, G_DBUS_SIGNAL_FLAG_EXPERIMENTAL))
            return FALSE;
        signal++;
    }
    return TRUE;
}

static gboolean check_properties_experimental(const GDBusPropertyTable* properties)
{
    const GDBusPropertyTable* property = properties;
    while (property && property->name) {
        if (!check_experimental(property->flags, G_DBUS_PROPERTY_FLAG_EXPERIMENTAL))
            return FALSE;
        property++;
    }
    return TRUE;
}

static struct interface_data* create_interface_data(const char* name,
    const GDBusMethodTable* methods,
    const GDBusSignalTable* signals,
    const GDBusPropertyTable* properties,
    void* user_data,
    GDBusDestroyFunction destroy)
{
    struct interface_data* iface = calloc(1, sizeof(struct interface_data));
    if (iface == NULL) {
        error("Failed to allocate memory for interface data");
        return NULL;
    }

    iface->name = strdup0(name);
    iface->methods = methods;
    iface->signals = signals;
    iface->properties = properties;
    iface->user_data = user_data;
    iface->destroy = destroy;

    return iface;
}

static gboolean add_interface(struct generic_data* data,
    const char* name,
    const GDBusMethodTable* methods,
    const GDBusSignalTable* signals,
    const GDBusPropertyTable* properties,
    void* user_data,
    GDBusDestroyFunction destroy)
{
    struct interface_data* iface;

    if (check_methods_experimental(methods)
        && check_signals_experimental(signals)
        && check_properties_experimental(properties)) {
        info("Interface %s is experimental, Nothing to register", name);
        return FALSE;
    }

    iface = create_interface_data(name, methods, signals, properties, user_data, destroy);
    if (iface == NULL)
        return FALSE;

    _dbus_list_append(&data->interfaces, iface);

    if (data->parent) {
        _dbus_list_append(&data->added, iface);
        add_pending(data);
    }

    return TRUE;
}

static struct generic_data* object_path_ref(DBusConnection* connection,
    const char* path)
{
    struct generic_data* data;

    if (dbus_connection_get_object_path_data(connection, path, (void*)&data)
        == TRUE) {
        if (data != NULL) {
            data->refcount++;
            return data;
        }
    }

    data = calloc(1, sizeof(struct generic_data));
    if (data == NULL) {
        error("Failed to allocate memory for object path data");
        return NULL;
    }
    data->conn = dbus_connection_ref(connection);
    data->path = strdup0(path);
    data->refcount = 1;
    if (uv_idle_init(uv_default_loop(), &data->handle) != 0) {
        dbus_connection_unref(data->conn);
        free(data->path);
        free(data);
        return NULL;
    }

    data->handle.data = data;

    data->introspect = strdup0(DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE "<node></node>");

    if (!dbus_connection_register_object_path(connection, path,
            &generic_table, data)) {
        dbus_connection_unref(data->conn);
        uv_close((uv_handle_t*)&data->handle, NULL);
        free(data->path);
        free(data->introspect);
        free(data);
        return NULL;
    }

    invalidate_parent_data(connection, path);

    add_interface(data, DBUS_INTERFACE_INTROSPECTABLE, introspect_methods,
        NULL, NULL, data, NULL);

    return data;
}

static void object_path_unref(DBusConnection* connection, const char* path)
{
    struct generic_data* data = NULL;

    if (dbus_connection_get_object_path_data(connection, path, (void*)&data)
        == FALSE)
        return;

    if (data == NULL)
        return;

    data->refcount--;

    if (data->refcount > 0)
        return;

    remove_interface(data, DBUS_INTERFACE_INTROSPECTABLE);
    remove_interface(data, DBUS_INTERFACE_PROPERTIES);

    invalidate_parent_data(data->conn, data->path);

    dbus_connection_unregister_object_path(data->conn, data->path);
}

static gboolean check_signal(DBusConnection* conn, const char* path,
    const char* interface, const char* name,
    const GDBusArgInfo** args)
{
    struct generic_data* data = NULL;
    struct interface_data* iface;
    const GDBusSignalTable* signal;

    *args = NULL;
    if (!dbus_connection_get_object_path_data(conn, path, (void*)&data)
        || data == NULL) {
        error("dbus_connection_emit_signal: path %s isn't registered",
            path);
        return FALSE;
    }

    iface = find_interface(data->interfaces, interface);
    if (iface == NULL) {
        error("dbus_connection_emit_signal: %s does not implement %s",
            path, interface);
        return FALSE;
    }

    for (signal = iface->signals; signal && signal->name; signal++) {
        if (strcmp0(signal->name, name) != 0)
            continue;

        if (signal->flags & G_DBUS_SIGNAL_FLAG_EXPERIMENTAL) {
            const char* env = getenv("GDBUS_EXPERIMENTAL");
            if (env == NULL || strcmp0(env, "1") != 0)
                break;
        }

        *args = signal->args;
        return TRUE;
    }

    error("No signal named %s on interface %s", name, interface);
    return FALSE;
}

gboolean dbus_register_interface(DBusConnection* connection,
    const char* path, const char* name,
    const GDBusMethodTable* methods,
    const GDBusSignalTable* signals,
    const GDBusPropertyTable* properties,
    void* user_data,
    GDBusDestroyFunction destroy)
{
    struct generic_data* data;

    if (!dbus_validate_path(path, NULL)) {
        error("Invalid object path: %s", path);
        return FALSE;
    }

    if (!dbus_validate_interface(name, NULL)) {
        error("Invalid interface: %s", name);
        return FALSE;
    }

    data = object_path_ref(connection, path);
    if (data == NULL)
        return FALSE;

    if (find_interface(data->interfaces, name)) {
        object_path_unref(connection, path);
        return FALSE;
    }

    if (!add_interface(data, name, methods, signals, properties, user_data,
            destroy)) {
        object_path_unref(connection, path);
        return FALSE;
    }

    if (properties != NULL
        && !find_interface(data->interfaces, DBUS_INTERFACE_PROPERTIES))
        add_interface(data, DBUS_INTERFACE_PROPERTIES,
            properties_methods, properties_signals, NULL,
            data, NULL);

    free(data->introspect);
    data->introspect = NULL;

    return TRUE;
}

gboolean dbus_unregister_interface(DBusConnection* connection,
    const char* path, const char* name)
{
    struct generic_data* data = NULL;

    if (path == NULL)
        return FALSE;

    if (dbus_connection_get_object_path_data(connection, path,
            (void*)&data)
        == FALSE)
        return FALSE;

    if (data == NULL)
        return FALSE;

    if (remove_interface(data, name) == FALSE)
        return FALSE;

    free(data->introspect);
    data->introspect = NULL;

    object_path_unref(connection, data->path);

    return TRUE;
}

gboolean dbus_register_security(const GDBusSecurityTable* security)
{
    if (security_table != NULL)
        return FALSE;

    security_table = security;

    return TRUE;
}

gboolean dbus_unregister_security(const GDBusSecurityTable* security)
{
    security_table = NULL;

    return TRUE;
}

DBusMessage* dbus_create_error_valist(DBusMessage* message, const char* name,
    const char* format, va_list args)
{
    char str[1024];

    /* Check if the message can be replied */
    if (dbus_message_get_no_reply(message))
        return NULL;

    if (format)
        vsnprintf(str, sizeof(str), format, args);
    else
        str[0] = '\0';

    return dbus_message_new_error(message, name, str);
}

DBusMessage* dbus_create_error(DBusMessage* message, const char* name,
    const char* format, ...)
{
    va_list args;
    DBusMessage* reply;

    va_start(args, format);

    reply = dbus_create_error_valist(message, name, format, args);

    va_end(args);

    return reply;
}

DBusMessage* dbus_create_reply_valist(DBusMessage* message,
    int type, va_list args)
{
    DBusMessage* reply;

    /* Check if the message can be replied */
    if (dbus_message_get_no_reply(message))
        return NULL;

    reply = dbus_message_new_method_return(message);
    if (reply == NULL)
        return NULL;

    if (dbus_message_append_args_valist(reply, type, args) == FALSE) {
        dbus_message_unref(reply);
        return NULL;
    }

    return reply;
}

DBusMessage* dbus_create_reply(DBusMessage* message, int type, ...)
{
    va_list args;
    DBusMessage* reply;

    va_start(args, type);

    reply = dbus_create_reply_valist(message, type, args);

    va_end(args);

    return reply;
}

static void dbus_flush(DBusConnection* connection)
{
    DBusList* l;
    DBusList* next;

    for (l = _dbus_list_get_first_link(&generic_pending); l;) {
        struct generic_data* data = l->data;

        next = _dbus_list_get_next_link(&generic_pending, l);
        _dbus_list_remove_link(&generic_pending, l);
        l = next;

        if (data->conn != connection)
            continue;

        process_changes(&data->handle);
    }
}

gboolean dbus_send_message(DBusConnection* connection, DBusMessage* message)
{
    dbus_bool_t result = FALSE;

    if (!message)
        return FALSE;

    if (dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_METHOD_CALL)
        dbus_message_set_no_reply(message, TRUE);
    else if (dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_SIGNAL) {
        const char* path = dbus_message_get_path(message);
        const char* interface = dbus_message_get_interface(message);
        const char* name = dbus_message_get_member(message);
        const GDBusArgInfo* args;

        if (!check_signal(connection, path, interface, name, &args))
            goto out;
    }

    /* Flush pending signal to guarantee message order */
    dbus_flush(connection);

    result = dbus_connection_send(connection, message, NULL);

out:
    dbus_message_unref(message);

    return result;
}

gboolean dbus_send_message_with_reply(DBusConnection* connection,
    DBusMessage* message,
    DBusPendingCall** call, int timeout)
{
    dbus_bool_t ret;

    /* Flush pending signal to guarantee message order */
    dbus_flush(connection);

    ret = dbus_connection_send_with_reply(connection, message, call,
        timeout);

    if (ret == TRUE && call != NULL && *call == NULL) {
        error("Unable to send message (passing fd blocked?)");
        return FALSE;
    }

    return ret;
}

gboolean dbus_send_error_valist(DBusConnection* connection,
    DBusMessage* message, const char* name,
    const char* format, va_list args)
{
    DBusMessage* error;

    error = dbus_create_error_valist(message, name, format, args);
    if (error == NULL)
        return FALSE;

    return dbus_send_message(connection, error);
}

gboolean dbus_send_error(DBusConnection* connection, DBusMessage* message,
    const char* name, const char* format, ...)
{
    va_list args;
    gboolean result;

    va_start(args, format);

    result = dbus_send_error_valist(connection, message, name,
        format, args);

    va_end(args);

    return result;
}

gboolean dbus_send_reply_valist(DBusConnection* connection,
    DBusMessage* message, int type, va_list args)
{
    DBusMessage* reply;

    reply = dbus_create_reply_valist(message, type, args);
    if (!reply)
        return FALSE;

    return dbus_send_message(connection, reply);
}

gboolean dbus_send_reply(DBusConnection* connection,
    DBusMessage* message, int type, ...)
{
    va_list args;
    gboolean result;

    va_start(args, type);

    result = dbus_send_reply_valist(connection, message, type, args);

    va_end(args);

    return result;
}

gboolean dbus_emit_signal(DBusConnection* connection,
    const char* path, const char* interface,
    const char* name, int type, ...)
{
    va_list args;
    gboolean result;

    va_start(args, type);

    result = dbus_emit_signal_valist(connection, path, interface,
        name, type, args);

    va_end(args);

    return result;
}

gboolean dbus_emit_signal_valist(DBusConnection* connection,
    const char* path, const char* interface,
    const char* name, int type, va_list args)
{
    DBusMessage* signal;
    dbus_bool_t ret;
    const GDBusArgInfo* args_info;

    if (!check_signal(connection, path, interface, name, &args_info))
        return FALSE;

    signal = dbus_message_new_signal(path, interface, name);
    if (signal == NULL) {
        error("Unable to allocate new %s.%s signal", interface, name);
        return FALSE;
    }

    ret = dbus_message_append_args_valist(signal, type, args);
    if (!ret)
        goto fail;

    if (dbus_args_have_signature(args_info, signal) == FALSE) {
        error("%s.%s: got unexpected signature '%s'", interface, name,
            dbus_message_get_signature(signal));
        ret = FALSE;
        goto fail;
    }

    return dbus_send_message(connection, signal);

fail:
    dbus_message_unref(signal);

    return ret;
}

static void dbus_list_reverse(DBusList** list)
{
    DBusList* last;

    last = NULL;
    while (*list) {
        last = *list;
        *list = last->next;
        last->next = last->prev;
        last->prev = *list;
    }

    *list = last;
}

static DBusMessage* create_properties_changed_signal(struct generic_data* data,
    struct interface_data* iface)
{
    DBusMessage* signal = dbus_message_new_signal(data->path,
        DBUS_INTERFACE_PROPERTIES, "PropertiesChanged");
    if (!signal) {
        error("Unable to allocate new " DBUS_INTERFACE_PROPERTIES
              ".PropertiesChanged signal");
    }
    return signal;
}

static void process_valid_properties(struct interface_data* iface,
    DBusMessageIter* dict, DBusList** invalidated)
{
    DBusList* list = _dbus_list_get_first_link(&iface->pending_prop);
    while (list != NULL) {
        GDBusPropertyTable* p = list->data;
        list = _dbus_list_get_next_link(&iface->pending_prop, list);
        if (!p->get)
            continue;

        if (p->exists && !p->exists(p, iface->user_data)) {
            _dbus_list_prepend(invalidated, p);
            continue;
        }
        append_property(iface, p, dict);
    }
}

static void process_invalid_properties(DBusList* invalidated,
    DBusMessageIter* array)
{
    DBusList* l;
    for (l = _dbus_list_get_first_link(&invalidated); l;
         l = _dbus_list_get_next_link(&invalidated, l)) {
        GDBusPropertyTable* p = l->data;
        dbus_message_iter_append_basic(array, DBUS_TYPE_STRING, &p->name);
    }
}

static void process_properties_from_interface(struct generic_data* data,
    struct interface_data* iface)
{
    DBusMessageIter iter, dict, array;
    DBusList* invalidated = NULL;
    DBusMessage* signal;

    if (!iface->pending_prop)
        return;

    signal = create_properties_changed_signal(data, iface);
    if (!signal)
        return;

    dbus_list_reverse(&iface->pending_prop);

    dbus_message_iter_init_append(signal, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface->name);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
        DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
            DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING
                DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
        &dict);

    process_valid_properties(iface, &dict, &invalidated);
    dbus_message_iter_close_container(&iter, &dict);

    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
        DBUS_TYPE_STRING_AS_STRING, &array);
    process_invalid_properties(invalidated, &array);
    _dbus_list_clear(&invalidated);
    dbus_message_iter_close_container(&iter, &array);

    _dbus_list_clear(&iface->pending_prop);
    /* Use dbus_connection_send to avoid recursive calls to dbus_flush */
    dbus_connection_send(data->conn, signal, NULL);
    dbus_message_unref(signal);
}

static void process_property_changes(struct generic_data* data)
{
    DBusList* l;

    data->pending_prop = FALSE;

    for (l = _dbus_list_get_first_link(&data->interfaces); l;
         l = _dbus_list_get_next_link(&data->interfaces, l)) {
        struct interface_data* iface = l->data;

        process_properties_from_interface(data, iface);
    }
}

void dbus_emit_property_changed_full(DBusConnection* connection,
    const char* path, const char* interface,
    const char* name,
    GDbusPropertyChangedFlags flags)
{
    const GDBusPropertyTable* property;
    struct generic_data* data;
    struct interface_data* iface;

    if (path == NULL)
        return;

    if (!dbus_connection_get_object_path_data(connection, path, (void**)&data)
        || data == NULL)
        return;

    iface = find_interface(data->interfaces, interface);
    if (iface == NULL)
        return;

    /*
     * If ObjectManager is attached, don't emit property changed if
     * interface is not yet published
     */
    if (root && _dbus_list_find_last(&data->added, iface) != NULL)
        return;

    property = find_property(iface->properties, name);
    if (property == NULL) {
        error("Could not find property %s in %p", name,
            iface->properties);
        return;
    }

    if (_dbus_list_find_last(&iface->pending_prop, (void*)property) != NULL)
        return;

    data->pending_prop = TRUE;
    _dbus_list_prepend(&iface->pending_prop, (void*)property);

    if (flags & G_DBUS_PROPERTY_CHANGED_FLAG_FLUSH)
        process_property_changes(data);
    else
        add_pending(data);
}

void dbus_emit_property_changed(DBusConnection* connection, const char* path,
    const char* interface, const char* name)
{
    dbus_emit_property_changed_full(connection, path, interface, name, 0);
}

gboolean dbus_get_properties(DBusConnection* connection, const char* path,
    const char* interface, DBusMessageIter* iter)
{
    struct generic_data* data;
    struct interface_data* iface;

    if (path == NULL)
        return FALSE;

    if (!dbus_connection_get_object_path_data(connection, path, (void**)&data)
        || data == NULL)
        return FALSE;

    iface = find_interface(data->interfaces, interface);
    if (iface == NULL)
        return FALSE;

    append_properties(iface, iter);

    return TRUE;
}

gboolean dbus_attach_object_manager(DBusConnection* connection)
{
    struct generic_data* data;

    data = object_path_ref(connection, "/");
    if (data == NULL)
        return FALSE;

    add_interface(data, DBUS_INTERFACE_OBJECT_MANAGER,
        manager_methods, manager_signals,
        NULL, data, NULL);
    root = data;

    return TRUE;
}

gboolean dbus_detach_object_manager(DBusConnection* connection)
{
    if (!dbus_unregister_interface(connection, "/",
            DBUS_INTERFACE_OBJECT_MANAGER))
        return FALSE;

    root = NULL;

    return TRUE;
}

void dbus_set_flags(int flags)
{
    global_flags = flags;
}

int dbus_get_flags(void)
{
    return global_flags;
}
