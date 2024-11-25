# Utils

[[English](./README.md) | [中文](./README_zh-cn.md)]

## 项目概览

当前目录下包含的主要是 framework 当中提供的一些常用工具实现。
| 工具 | 工具简要描述 |
| -- | -- |
| `kvdb` | 基于本地数据库的`键值对`数据存取接口 |
| `log` | 提供和 `Android` 平台兼容的 `Log API` 接口<br>用于在 `openvela` 当中直接使用 Android LOG API |
| `trace` | 提供用于用户空间程序的打点工具 |

## 项目描述

### 1 kvdb

#### 简要介绍

`kvdb` 提供了一套本地数据库的读写接口, `API` 设计参考 `Android`的 `properties` 存取规范, 同时提供了命令行工具以方便本地快速调试。
`openvela` 中的 `kvdb` 支持本地永久化存储以及跨核调用(分别需要 Unix domain socket 和 rpmsg socket 支持), 需要永久存储到文件的键值需要以 `"persist."` 开头。
`kvdb` 底层实现包含了三种机制:
1. 基于开源的 `UnQLite` 数据库, 依赖于数据库。
2. 基于 `MTD CONFIG` (目前仅用于 nor flash)。
3. 最后一种基于 `file` 文件。

> `kvdb` 接口说明：[frameworks/utils/include/kvdb.h](include/kvdb.h)

#### kvdb 常见配置说明

| kvdb 配置 | 说明 |
| -- | -- |
| CONFIG_KVDB_PRIORITY | KVDB 任务优先级, 默认为系统默认值 |
| CONFIG_KVDB_STACKSIZE | KVDB 栈空间分配，默认为系统默认值 |
| CONFIG_KVDB_SERVER | KVDB SERVER 模式：表示当前 CPU 是否为读写文件的主 CPU, 为 n 则只调用其他 CPU 上的 KVDB |
| CONFIG_KVDB_DIRECT | KVDB DIRECT模式：在无需 rpmsg socket 的场景（无需跨核），可使用此模式<br>CONFIG_KVDB_DIRECT 与 CONFIG_KVDB_SERVER 两种模式只能二选一 |
| CONFIG_KVDB_COMMIT_INTERVAL | KVDB 提交间隔 (秒)，默认为 5 <br> KVDB 有内部缓存，提交后才真正写入文件, 如果提交 persist 类型的 kv 后, `CONFIG_KVDB_COMMIT_INTERVAL` 时间前就下电, 数据不会真正写入到 `persist.db` 文件中。 `CONFIG_KVDB_COMMIT_INTERVAL` 时间设置的越短, `kvdb` 将内部缓存写入文件越频繁, 会一定程度上影响系统性能 |
| CONFIG_KVDB_SOURCE_PATH | KVDB 默认值加载路径，默认为 `"/etc/build.prop"`, 支持多个路径, 用 `;` 分隔即可，每次开机启动会自动从该文件加载KV值 |
| CONFIG_KVDB_UNQLITE | 配置使用 unqlite database 存储 kv |
| CONFIG_KVDB_NVS | 配置使用 nvs 存储 kv |
| CONFIG_KVDB_FILE | 配置使用 file 存储 kv |


> `CONFIG_KVDB_UNQLITE`，`CONFIG_KVDB_NVS`，`CONFIG_KVDB_FILE` 三种数据存储的 `backend` 只能三选一。

### 2 log

log 模块本身是一个 wrapper 层，底层将 `openvela log` 系统进行了封装，封装成的 API 是和 Android 当中的 `log API` 一致, 当我们将 `Android` 应用或者框架移植到 `openvela` 当中时, 不需要再提供自己的 `log` 对接,直接使用当前模块就可以了。
以下是log模块的结构:

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

这个模块当中包含的主要是用于用户空间程序的打点工具，我们可以通过在用户程序当中手动插桩来实现打点分析，
trace 当中提供的 atrace 工具主要是配合 openvela 系统提供的打点工具来使用的。

## 使用指南

### 1 kvdb

`kvdb` 本身有多种使用形式，我们可以直接在代码当中集成，也可以直接在 `nsh` 当中以命令行程序的方式来使用。

#### 1.1 直接在代码当中集成的示例

以下是针对 `kvdb` 当中提供的监控 `key/value`变化的接口的 demo，针对简单和复杂场景，提供了两套 API：

1. 简单场景: 只能监控一个 `key`。

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

2. 复杂场景: 支持 `poll`，用户自由监控多个 `key`。

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

#### 1.2 在 nsh 当中以命令行的形式来使用

KVDB 提供了 `getprop` 和 `setprop` 两个命令行程序供用户使用，用户可以使用 `getprop` 和 `setprop` 方便地查看已存在的 KV 或是设置新的 KV。

这两个命令行在使能 KVDB 后默认开启。
- getprop：打印出设置的 property
    - nsh> getprop：列出当前所有`props`
    - nsh> getprop `key`：打印出 `key` 对应的`prop`

- setprop：设置或者删除 property
    - nsh> setprop `key` : 删除 `key` 对应的`prop`
    - nsh> setprop `key` `value` ：保存 `key`:`value`到数据库

下面是具体的使用示例:

```log
nsh> setprop name peter #添加名为name值为peter的键值对， 掉电消失
nsh> setprop persist.name1 peter1 #添加名为name1值为peter1的键值对， 掉电不消失

nsh> getprop            # 查看所有键值对
name: peter

nsh> setprop name       # 删除名为name的键值对
nsh> getprop name
```

### 2 log

1. 打开 `CONFIG_ANDROID_LIBBASE`。
2. 在程序当中直接使用标准的android log api来收集打印日志。

    ```cpp
    #include <log/log.h>

    #define LOG_TAG "MyAppTag"

    int main() {
        // 自定义优先级打印 log
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Formatted number: %d", 42);

        // 使用宏打印 INFO 级别 log
        ALOGI("ALOGI: A log message from my app.");
        return 0;
    }
    ```

### 3 trace

1. 打开 `CONFIG_SCHED_INSTRUMENTATION_DUMP` 和 `CONFIG_ATRACE` 选项。

2. 在程序当中需要跟踪的地方添加上打点信息:

    ```cpp
    // 使用是添加头文件，必须添加TAG
    #define ATRACE_TAG ATRACE_TAG_ALWAYS
    #include <cutils/trace.h>

    int main(int argc, char *argv[])
    {
        // 对当前函数进行插桩
        ATRACE_BEGIN("hello_main");
        sleep(1);
        ATRACE_INSTANT("printf");
        printf("hello world!");
        // 结束插桩
        ATRACE_END();
        return 0;
    }
    ```

3. 使用 `trace dump` 工具查看打点输出的结果:

    ```log
    hello-7   [0]   3.187400000: sched_wakeup_new: comm=hello pid=7 target_cpu=0
    hello-7   [0]   3.187400000: tracing_mark_write: B|7|hello_main
    hello-7   [0]   4.197700000: tracing_mark_write: I|7|printf
    hello-7   [0]   4.187700000: tracing_mark_write: E|7|hello_main
    ```

    atrace 的输出结果可以直接使用 [perfetto](https://ui.perfetto.dev/) 工具以可视化的形式来查看 trace 的时序图。
