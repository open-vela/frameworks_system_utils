# Utils

[[English](./README.md) | [中文](./README_zh-cn.md)]

## 项目概揽

当前目录下包含的主要是framework当中提供的一些常用工具实现

## 项目描述

### 1. gdbus

`gdbus`模块是对`D-Bus`接口的进一步封装的API模块. `D-Bus`接口使用比较复杂, 实际模块使用时, 将`D-Bus`接口再次封装, 提供更简易操作的API接口. 模块主要按文件分类接口功能如下:

1. `mainloop.c` : 主要实现`DBusConnection`连接与应用`loop`如何绑定的逻辑，与`lib UV loop`强制绑定. 当调用`g_dbus_setup_private`获取到`DbusConnection`新连接后，在`gdbus`内部调用再执行`setup_bus`接口，设置`dbusConnection`的`watch`,`timerout`和`dispatch`等`dbus`回调处理接口.

2. `watch.c` : `gdbus`可以对指定服务的上下线、指定信号的发生、指定属性的改变进行监控. 每一个`watch`由`struct filter_data`表示, 所有的`watch`连接在全局链表`listeners`中. 所有监控的信号都会被添加到总线规则中，并通过filter过滤进行处理.

3. `object.c` : 对`D-Bus`的标准接口`"org.freedesktop.DBus.ObjectManager"`进行拓展. `gdbus`中对`D-Bus`中的对象进一步拓展, 形成了对象树的概念, 每一个叶子节点都代表一个对象, 每个对象下可拥有多个接口, 每个接口下包含方法(`method`), 信号(`signal`), 属性(`properties`). 在`gdbus`中每一个对象由`object_path_ref`创建, 上下文为`struct generic_data`, 可通过下列函数进行根对象和叶子对象的创建. 通过`dbus_connection_register_object_path`绑定对象的消息处理函数`generic_message`.

4. `client.c` : `gdbus`通过`client.c`进行`GDBusClient`的实现. `GDBusClient`是应用与服务之间`CS`结构中`client`端的抽象, 通常需要绑定要访问服务的名字, 服务上的具体对象`path`来使用, `client`通过与`D-Bus`建立的`connection`与服务进行通信, 非直连模式.<br>默认`GDBusClient`只接收除`D-Bus daemon`和服务发来的`signal`. 通过监听`"NameOwnerChanged"`的`dbus signal`, 获取服务`service`的上线和下线, 通过主动调用`dbus`方法`"GetManagedObjects"`, 获取服务`service`提供的对外能力.<br>每一个`GDBusClient`都管理一条`GDBusProxy`链表,`g_dbus_proxy_new`用于创建一个`GDBusProxy`, 它由对象`path`, 接口`name`和`GDBusClient`唯一标识, `GDBusProxy`用于监控该对象接口上的属性`properties`变化, 因此会调用`g_dbus_add_properties_watch`去监听信号`PropertiesChanged`,当发生变化时调用`properties_changed`.<br>对于每一个`proxy`都会调用`get_all_properties`获取指定对象接口下的有效属性, 通过调用对方标准接口方法: `org.freedesktop.DBus.Properties.GetAll`, 参数是接口名, 返回值是字典数组, 字典类型为`{STRING，VARIANT}`, 将获取到的所有属性保存到`prop_list`中, 并针对每一个`properties`调用`prop_func`和`property_changed`函数, 前者由`g_dbus_proxy_set_property_watch`指定, 后者由`g_dbus_client_set_proxy_handlers`指定.

5. `polkit.c` : `gdbus`提供了`built-in`和外部安全认证, 其中`polkit`属于`built-in`服务, 它可通过`host`主机的接口对访问进行身份认证.

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

2. 然后在程序当中需要跟踪的地方添加上打点信息

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

3. 然后使用trace dump工具查看打点输出的结果

```log
   hello-7   [0]   3.187400000: sched_wakeup_new: comm=hello pid=7 target_cpu=0
   hello-7   [0]   3.187400000: tracing_mark_write: B|7|hello_main
   hello-7   [0]   4.197700000: tracing_mark_write: I|7|printf
   hello-7   [0]   4.187700000: tracing_mark_write: E|7|hello_main
```

另外就是 atrace 的输出结果也是可以直接使用 [perfetto](https://ui.perfetto.dev/) 工具以可视化的形式来查看 trace 的时序图的.
