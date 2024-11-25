# Utils

[[English](./README.md) | [中文](./README_zh-cn.md)]

## Project Overview

The current directory mainly contains some common tool implementations provided by the framework.
| Tool | Brief description of tool |
| -- | -- |
| `kvdb` | `Key-value pair` data access interface based on local database. |
| `log` | Provides an `Log API` interface compatible with the `Android` platform.<br>Used to directly use the Android LOG API in `openvela`. |
| `trace` | Provides a dotting tool for user-space programs. |

## Project Description

### 1 kvdb

#### Brief introduction

`kvdb` provides a set of local database read and write interfaces. The `API` design refers to the `properties` access specification of `Android`, and also provides command line tools to facilitate local quick debugging.

`kvdb` in `openvela` supports local persistent storage and cross-core calls (requires Unix domain socket and rpmsg socket support respectively). The key value that needs to be permanently stored in the file needs to start with `"persist."`.

`kvdb` underlying implementation includes three mechanisms:

1. Based on the open source `UnQLite` database, dependent on the database.

2. Based on `MTD CONFIG` (currently only used for nor flash).

3. The last one is based on `file` files.

> `kvdb` interface description: [frameworks/utils/include/kvdb.h](include/kvdb.h)

#### kvdb common configuration description

| kvdb configuration | Description |
| -- | -- |
| CONFIG_KVDB_PRIORITY | KVDB task priority, defaults to system default |
| CONFIG_KVDB_STACKSIZE | KVDB stack space allocation, defaults to system default |
| CONFIG_KVDB_SERVER | KVDB SERVER mode: indicates whether the current CPU is the main CPU for reading and writing files, if it is n, only KVDB on other CPUs is called |
| CONFIG_KVDB_DIRECT | KVDB DIRECT mode: This mode can be used in scenarios where rpmsg socket is not required (no need for cross-core)<br>CONFIG_KVDB_DIRECT and CONFIG_KVDB_SERVER can only be selected from the two modes |
| CONFIG_KVDB_COMMIT_INTERVAL | KVDB commit interval (seconds), default is 5 <br> KVDB has internal cache, and the data is actually written to the file only after committing. If the power is turned off before `CONFIG_KVDB_COMMIT_INTERVAL` time after committing the persist type kv, the data will not be actually written to the `persist.db` file. The shorter the `CONFIG_KVDB_COMMIT_INTERVAL` time is set, the more frequently `kvdb` writes the internal cache to the file, which will affect the system performance to a certain extent. |
| CONFIG_KVDB_SOURCE_PATH | KVDB default value loading path, the default is `"/etc/build.prop"`, supports multiple paths, separated by `;`, and the KV value will be automatically loaded from this file every time the computer starts. |
| CONFIG_KVDB_UNQLITE | Configure to use unqlite database to store kv |
| CONFIG_KVDB_NVS | Configure to use nvs to store kv |
| CONFIG_KVDB_FILE | Configure to use file to store kv |

> Only one of the three data storage `backend`s `CONFIG_KVDB_UNQLITE`, `CONFIG_KVDB_NVS`, and `CONFIG_KVDB_FILE` can be selected.

### 2 log

The log module itself is a wrapper layer, which encapsulates the `openvela log` system at the bottom layer. The encapsulated API is consistent with the `log API` in Android. When we port the `Android` application or framework to `openvela`, we do not need to provide our own `log` connection, and can directly use the current module.
The following is the structure of the log module:

```log
android log api
       |
      \|/
  log wrapper
       |
      \|/
  vela log impl
```

### 3 trace

This module mainly contains the dot analysis tool for user space programs. We can implement dot analysis by manually inserting stubs in user programs.
The atrace tool provided in trace is mainly used in conjunction with the dot analysis tool provided by the openvela system.

## Usage Guide


### 1 kvdb

`kvdb` itself has multiple usage forms. We can integrate it directly in the code, or use it directly in `nsh` as a command line program.

#### 1.1 Example of direct integration in the code

The following is a demo of the interface provided by `kvdb` to monitor `key/value` changes. Two sets of APIs are provided for simple and complex scenarios:

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

2. Complex scenarios: Supports `poll`, allowing users to freely monitor multiple `keys`.

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

#### 1.2 Use in nsh as command line

KVDB provides two command line programs, `getprop` and `setprop`, for users to use. Users can use `getprop` and `setprop` to easily view existing KVs or set new KVs.

These two command lines are enabled by default after KVDB is enabled.
- getprop: print out the set property
- nsh> getprop: list all current `props`
- nsh> getprop `key`: print out the `prop` corresponding to `key`
- setprop: set or delete property
- nsh> setprop `key`: delete the `prop` corresponding to `key`
- nsh> setprop `key` `value`: save `key`:`value` to the database

Here are specific usage examples:

```log
nsh> setprop name peter # add a key-value pair named name with a value of peter, which disappears when the power is off
nsh> setprop persist.name1 peter1 # add a key-value pair named name1 with a value of peter1, which does not disappear when the power is off
nsh> getprop # view all key-value pairs
name: peter
nsh> setprop name # delete a key-value pair named name
nsh> getprop name
```


### 2 log

1. Enable `CONFIG_ANDROID_LIBBASE`.
2. Use the standard Android log API to collect and print logs directly in the program.

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

### 3 trace

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
