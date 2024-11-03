# Utils

[[English](./README.md) | [中文](./README_zh-cn.md)]

## Project Overview

The current directory mainly contains some common tool implementations provided by the framework.
| Tool | Brief description of tool |
| -- | -- |
| `gdbus` | Encapsulation of the `D-Bus` interface for convenient operation of `D-Bus`. |
| `kvdb` | `Key-value pair` data access interface based on local database. |
| `log` | Provides an `Log API` interface compatible with the `Android` platform.<br>Used to directly use the `Android` LOG API in `Vela`. |
| `trace` | Provides a dotting tool for user-space programs. |

## Project Description

### 1. gdbus

The `gdbus` module is an API module that further encapsulates the `D-Bus` interface. Since the `D-Bus` interface is relatively complex to use, when actually using modules, by encapsulating the `D-Bus` interface again, it will be more convenient to use. `gdbus` provides an API interface with easier operation to facilitate our operation of `D-Bus`.

### 2. kvdb

#### 2.1 Introduction

`kvdb` provides a set of read and write interfaces for local databases. The `API` design refers to the `Android` `properties` access specification. At the same time, a command-line tool is provided to facilitate local rapid debugging.
The `kvdb` in `Vela` supports local permanent storage and cross-core calls (requiring Unix domain socket and rpmsg socket support respectively). The key-value that needs to be permanently stored in a file needs to start with `"persist."`.
The underlying implementation of `kvdb` contains three mechanisms:
1. Based on the open source `UnQLite` database, it depends on the database;
2. Another one is based on `MTD CONFIG` (currently only used for nor flash);
3. The last one is based on `file` files.

> General interface description of `kvdb`: [frameworks/utils/include/kvdb.h](include/kvdb.h)

#### 2.2 Typical configuration of `kvdb`

| kvdb Configuration | Description |
| -- | -- |
| CONFIG_KVDB_PRIORITY | KVDB task priority, default is the system default value. |
| CONFIG_KVDB_STACKSIZE | KVDB stack space allocation, default is the system default value. |
| CONFIG_KVDB_SERVER | KVDB SERVER mode. Indicates whether the current CPU is the main CPU for reading and writing files. If it is 'n', only the KVDB on other CPUs will be called. |
| CONFIG_KVDB_DIRECT | KVDB DIRECT mode: In scenarios without rpmsg socket (no need for cross-core), this mode can be used.<br>Only one of CONFIG_KVDB_DIRECT and CONFIG_KVDB_SERVER modes can be selected. |
| CONFIG_KVDB_COMMIT_INTERVAL | KVDB commit interval (in seconds), default is 5.<br>KVDB has an internal cache and only after committing is the data truly written to the file. If power is off before the `CONFIG_KVDB_COMMIT_INTERVAL` time after submitting a persist type kv, the data will not be truly written to the `persist.db` file. The shorter the `CONFIG_KVDB_COMMIT_INTERVAL` time is set, the more frequently kvdb writes the internal cache to the file, which will affect system performance to a certain extent. |
| CONFIG_KVDB_SOURCE_PATH | KVDB default value loading path, default is `"/etc/build.prop"`, supports multiple paths, just separate them with `;`. At each startup, KV values will be automatically loaded from this file. |
| CONFIG_KVDB_UNQLITE | Configure to use unqlite database to store kv. |
| CONFIG_KVDB_NVS | Configure to use nvs to store kv. |
| CONFIG_KVDB_FILE | Configure to use file to store kv. |

> Only one of the three backends for data storage, `CONFIG_KVDB_UNQLITE`, `CONFIG_KVDB_NVS`, and `CONFIG_KVDB_FILE`, can be selected.

### 3. log

The log module itself is a wrapper layer that encapsulates the Vela log system, making the API consistent with the log API in Android. When we port Android applications or frameworks to Vela, we don't need to provide our own log integration anymore, we can use this module directly. Here is the structure of the log module:

```log
android log api
       |
      \|/
  log wrapper
       |
      \|/
  vela log impl
```

### 4. trace

This module mainly contains instrumentation tools for user-space programs. We can achieve instrumentation analysis by manually instrumenting in user programs. The atrace tool provided in trace is mainly used in conjunction with the instrumentation tools provided by the Vela system.

## Usage Guide

### 1. gdbus

1. To enable the `CONFIG_LIB_DBUS` build option

2. Instantiation

GDBus is an interface module and requires the caller to instantiate it and pass the instantiated object when calling the GDBus interface.
The caller needs to create a structure. The members should at least include `gdbus Connection` and `client` instance objects, similar to the following:

```cpp
typedef struct {
    DBusConnection* connection;
    GDBusClient* client;
    GDBusProxy* dbus_proxy[USER_SUPPORT_PROXY_MAX];
    bool client_ready;
} dbus_context;

dbus_context* ctx = malloc(sizeof(dbus_context));

// Create a DbusConnection instance
ctx->connection = g_dbus_setup_private(DBUS_BUS_SYSTEM, NULL, NULL);

// Set the disconnection function for the Connection. Otherwise, when the dbus Connection exits, the current process will exit synchronously. After adding this function, the current process will not exit.
g_dbus_set_disconnect_function(ctx->connection, system_dbus_disconnected, callback, NULL);

// Set the client name for the current dbus Connection
dbus_request_name(ctx->connection, client_name, &err);
ctx->client = g_dbus_client_new(ctx->connection, OFONO_SERVICE, OFONO_MANAGER_PATH);

// Set the property filter function for the proxy.
// ofono_interface_proxy_added listens for proxy addition and caches the required proxy instances on demand for subsequent operations on the properties and methods of this proxy.
// ofono_interface_proxy_removed listens for proxy removal and synchronously clears the cached proxy instances.
// In the object_filter function, set which proxies do not need to read property attributes.
// ofono_property_changed listens for changes in properties and caches the property values of the corresponding proxies on demand.
g_dbus_client_set_proxy_handlers(dbus_client, ofono_interface_proxy_added,
        ofono_interface_proxy_removed,
        object_filter,
        ofono_property_changed, tele);

// Set the callback function for successful dbus client connection on_dbus_client_ready
g_dbus_client_set_ready_watch(ctx->client, on_dbus_client_ready, cbd)
```

3. Exit and release

When the caller process exits, it needs to release the `dbus Connection` instance. Execute the following code:
```cpp
g_dbus_client_unref(ctx->client);
dbus_connection_close(ctx->connection);
dbus_connection_unref(ctx->connection);
```

4. Call the interface

Call the interface, pass in the instance of `voice manager proxy`, and initiate a dial request.

```cpp
g_dbus_proxy_method_call(ctx->dbus_proxy[VOICE_MANAGER], "Dial", dial_setup,
            dial_reply, param, dial_destory)
```

### 2. kvdb

`kvdb` itself has multiple usage forms. We can integrate it directly in the code or use it as a command-line program in `nsh`.

#### 2.1 Example of direct integration in code

The following is a demo of the interface provided in `kvdb` for monitoring key/value changes. For simple and complex scenarios, two sets of APIs are provided.

1. Simple scenario: Only one `key` can be monitored.

```cpp
int main(void)
{
    char newkey[PROPERTY_KEY_MAX];
    char newvalue[PROPERTY_VALUE_MAX];
    int ret = property_wait("tsetkey", newkey, newvalue, -1);
    if (ret < 0)
    {
        printf("property_wait failed, ret=%d\n", ret);
        goto out;
    }

    printf("the new key: %s\n", newkey);
    printf("the new value: %s\n", newvalue);

out:
    return ret;
}
```

2. Complex scenario: Supports polling. Users can freely monitor multiple `keys`.

```cpp
int main(void)
{
    struct pollfd fds[2];
    char newkey[PROPERTY_KEY_MAX];
    char newvalue[PROPERTY_VALUE_MAX];
    int fd1 = property_monitor_open("monitorkey*");
    int fd2 = property_monitor_open("testkey");

    fds[0].fd = fd1;
    fds[0].events = POLLIN;
    fds[1].fd = fd2;
    fds[1].events = POLLIN;
    int ret= poll(fds, 2, -1);
    if (ret <= 0)
        goto out;

    for (int i = 0; i < 2; i++)
    {
        if ((fds[i].revents & POLLIN) == 0)
            continue;

        ret = property_monitor_read(fds[i].fd, newkey, newvalue);
        if (ret < 0)
            goto out;

        printf("the new key: %s\n", newkey);
        printf("the new value: %s\n", newvalue);
    }

out:
    property_monitor_close(fd1);
    property_monitor_close(fd2);
    return ret;
}
```

### 3. log

1. To enable the `CONFIG_ANDROID_LIBBASE` build option
2. Then we can directly use the standard Android log API in the program to collect and print logs.

```c
#include <log/log.h>

// the tag for the ALOGI
#define LOG_TAG "MyAppTag"

int main() {
  // print log with custom priority level
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Formatted number: %d", 42);

  // Using ALOGI macro to print info level log
  ALOGI("ALOGI: A log message from my app.");
  return 0;
}
```

### 4. trace

1. To enable the `CONFIG_SCHED_INSTRUMENTATION_DUMP` and `CONFIG_ATRACE` build options

2. Add the instrumentation point in the program:

```cpp
// define the tag for tracing
#define ATRACE_TAG ATRACE_TAG_ALWAYS
#include <cutils/trace.h>

int main(int argc, char *argv[])
{
  // instrument the current function
  ATRACE_BEGIN("hello_main");
  sleep(1);
  ATRACE_INSTANT("printf");
  printf("hello world!");
  // end instrumentation
  ATRACE_END();
  return 0;
}
```

3. Show the instrumentation result with `trace dump` tool:

```log
   hello-7   [0]   3.187400000: sched_wakeup_new: comm=hello pid=7 target_cpu=0
   hello-7   [0]   3.187400000: tracing_mark_write: B|7|hello_main
   hello-7   [0]   4.197700000: tracing_mark_write: I|7|printf
   hello-7   [0]   4.187700000: tracing_mark_write: E|7|hello_main
```

In addition, the output result of atrace can also directly use the [perfetto](https://ui.perfetto.dev/) tool to view the timing diagram of the trace in a visual form.
