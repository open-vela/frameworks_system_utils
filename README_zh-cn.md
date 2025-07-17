# Utils

[[English](./README.md) | [中文](./README_zh-cn.md)]

## 项目概览

当前目录下包含的主要是framework当中提供的一些常用工具实现.
| 工具 | 工具简要描述 |
| -- | -- |
| `gdbus` | 对`D-Bus`接口封装, 方便操作`D-Bus` |
| `kvdb` | 基于本地数据库的`键值对`数据存取接口 |
| `log` | 提供和`Android`平台兼容的`Log API`接口<br>用于在`Vela`当中直接使用`Android` LOG API |
| `trace` | 提供用于用户空间程序的打点工具 |

## 项目描述

### 1. gdbus

`gdbus`模块是对`D-Bus`接口的进一步封装的API模块. 由于`D-Bus`接口使用比较复杂, 在实际模块使用时, 通过将`D-Bus`接口再次封装, 会方便使用. `gdbus`通过提供更简易操作的API接口，方便我们去操作`D-Bus`.

### 2. kvdb

#### 2.1 简要介绍

`kvdb`提供了一套本地数据库的读写接口, `API`设计参考`Android`的`properties`存取规范, 同时提供了命令行工具以方便本地快速调试.
`Vela`中的`kvdb`支持本地永久化存储以及跨核调用(分别需要Unix domain socket和rpmsg socket支持), 需要永久存储到文件的键值需要以`"persist."`开头.
`kvdb`底层实现包含了三种机制:
1. 基于开源的`UnQLite`数据库, 依赖于数据库;
2. 另外一种是基于`MTD CONFIG` (目前仅用于nor flash);
3. 最后一种基于`file`文件;

> `kvdb`一般接口说明: [frameworks/utils/include/kvdb.h](include/kvdb.h)

#### 2.2 `kvdb`的常见配置说明

| kvdb配置 | 说明 |
| -- | -- |
| CONFIG_KVDB_PRIORITY | KVDB任务优先级, 默认为系统默认值 |
| CONFIG_KVDB_STACKSIZE | KVDB栈空间分配，默认为系统默认值 |
| CONFIG_KVDB_SERVER | KVDB SERVER模式. 表示当前CPU是否为读写文件的主CPU, 为n则只调用其他CPU上的KVDB |
| CONFIG_KVDB_DIRECT | KVDB DIRECT模式: 在无需rpmsg socket的场景（无需跨核），可使用此模式<br>CONFIG_KVDB_DIRECT 与 CONFIG_KVDB_SERVER 两种模式只能二选一 |
| CONFIG_KVDB_COMMIT_INTERVAL | KVDB提交间隔 (秒), 默认为5<br>KVDB有内部缓存, 提交后才真正写入文件, 如果提交persist类型的kv后, `CONFIG_KVDB_COMMIT_INTERVAL`时间前就下电, 数据不会真正写入到`persist.db`文件中. `CONFIG_KVDB_COMMIT_INTERVAL`时间设置的越短, `kvdb`将内部缓存写入文件越频繁, 会一定程度上影响系统性能 |
| CONFIG_KVDB_SOURCE_PATH | KVDB默认值加载路径，默认为`"/etc/build.prop"`, 支持多个路径, 用`;`分隔即可. 每次开机启动会自动从该文件加载KV值 |
| CONFIG_KVDB_UNQLITE | 配置使用 unqlite database 存储 kv |
| CONFIG_KVDB_NVS | 配置使用 nvs 存储 kv |
| CONFIG_KVDB_FILE | 配置使用 file 存储 kv |

> `CONFIG_KVDB_UNQLITE`,`CONFIG_KVDB_NVS`, `CONFIG_KVDB_FILE` 三种数据存储的backend只能三选一

### 3. log

log模块本身是一个wrapper层,底层将Vela log系统进行了封装,封装成的API是和Android当中的log API一致, 当我们将Android应用或者框架移植到Vela当中, 不需要再提供自己的log对接,直接使用当前模块就可以了.
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

### 4. trace

这个模块当中包含的主要是用于用户空间程序的打点工具. 我们可以通过在用户程序当中手动插桩来实现打点分析.
trace当中提供的atrace工具主要是配合Vela系统提供的打点工具来使用的.

## 使用指南

### 1. gdbus

1. 打开`CONFIG_LIB_DBUS`选项

2. 实例化

gdbus为接口模块, 需要调用者实例化, 并在调用gdbus接口时, 传入实例化对象.
调用者需要创建一个结构体, 成员至少有`gdbus Connection`和`client`实例对象, 类似如下:

```cpp
typedef struct {
    DBusConnection* connection;
    GDBusClient* client;
    GDBusProxy* dbus_proxy[USER_SUPPORT_PROXY_MAX];
    bool client_ready;
} dbus_context;

dbus_context* ctx = malloc(sizeof(dbus_context));

//创建DbusConnection实例
ctx->connection = g_dbus_setup_private(DBUS_BUS_SYSTEM, NULL, NULL);

//设置Connection退出函数，否则dbus Connection退出时，会同步退出当前进程，添加后，则不会退出当前进程
g_dbus_set_disconnect_function(ctx->connection, system_dbus_disconnected, callback, NULL);

//设置当前dbus Connection的client名字
dbus_request_name(ctx->connection, client_name, &err);
ctx->client = g_dbus_client_new(ctx->connection, OFONO_SERVICE, OFONO_MANAGER_PATH);

//设置proxy的property的filter函数，
//ofono_interface_proxy_added 监听proxy添加，按需缓存需要使用的proxy实例，供后续操作这个proxy的属性和method
//ofono_interface_proxy_removed 监听proxy删除，同步清除缓存的proxy实例
//在object_filter函数中设置哪些proxy不需要读取property属性
//ofono_property_changed 监听proper属性的变化，按需缓存对应proxy的proper值。
g_dbus_client_set_proxy_handlers(dbus_client, ofono_interface_proxy_added,
        ofono_interface_proxy_removed,
        object_filter,
        ofono_property_changed, tele);

//设置dbus client连接成功的回调函数on_dbus_client_ready
g_dbus_client_set_ready_watch(ctx->client, on_dbus_client_ready, cbd)
```

3. 退出释放

调用者进程退出时, 需要释放`dbus Connection`实例, 执行如下代码:

```cpp
g_dbus_client_unref(ctx->client);
dbus_connection_close(ctx->connection);
dbus_connection_unref(ctx->connection);
```

4. 调用接口

调用接口, 传入`voice manager proxy`实例, 发起dial请求:

```cpp
g_dbus_proxy_method_call(ctx->dbus_proxy[VOICE_MANAGER], "Dial", dial_setup,
            dial_reply, param, dial_destory)
```

### 2. kvdb

`kvdb`本身有多种使用形式,我们可以直接在代码当中集成,也可以直接在`nsh`当中以命令行程序的方式来使用.

#### 2.1 直接在代码当中集成的示例

以下是针对`kvdb`当中提供的监控key/value变化的接口的demo, 针对简单和复杂场景,提供了2套API:

1. 简单场景: 只能监控一个`key`

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

2. 复杂场景: 支持poll，用户自由监控多个key

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

#### 2.2 在nsh当中以命令行的形式来使用

KVDB提供了getprop和setprop两个命令行程序供用户使用，用户可以使用getprop和setprop方便地查看已存在的KV或是设置新的KV。
这两个命令行在使能KVDB后默认开启
getprop：打印出设置的property
- nsh> getprop：列出当前所有props
- nsh> getprop <key>: 打印出<key>对应的prop

setprop：设置或者删除property
- nsh> setprop <key> : 删除<key>对应的prop
- nsh> setprop <key> <value> : 保存<key>:<value>到数据库

下面是具体的使用示例:

```log
nsh> setprop name peter #添加名为name值为peter的键值对， 掉电消失
nsh> setprop persist.name1 peter1 #添加名为name1值为peter1的键值对， 掉电不消失

nsh> getprop            #查看所有键值对
name: peter

nsh> setprop name       #删除名为name的键值对
nsh> getprop name
```

### 3. log

1. 打开`CONFIG_ANDROID_LIBBASE`
2. 然后我们可以在程序当中直接使用标准的android log api来收集打印日志.

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

### 4. trace

1. 打开`CONFIG_SCHED_INSTRUMENTATION_DUMP`和`CONFIG_ATRACE`选项

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

3. 使用trace dump工具查看打点输出的结果:

```log
   hello-7   [0]   3.187400000: sched_wakeup_new: comm=hello pid=7 target_cpu=0
   hello-7   [0]   3.187400000: tracing_mark_write: B|7|hello_main
   hello-7   [0]   4.197700000: tracing_mark_write: I|7|printf
   hello-7   [0]   4.187700000: tracing_mark_write: E|7|hello_main
```

另外就是 atrace 的输出结果也是可以直接使用 [perfetto](https://ui.perfetto.dev/) 工具以可视化的形式来查看 trace 的时序图的.
