# CppServer

## 中文

### 1. 项目介绍

这是一个基于 `cpp-httplib`、`nlohmann/json` 和自定义线程池适配的轻量级 C++ HTTP 服务骨架。当前默认提供两个 service：

- `api`：`/`、`/status`、`/sample/*`、`/docs`
- `file`：静态文件挂载，默认服务 `mount/`

核心结构：

- `Application::ConfigureApplication(...)`：应用装配入口
- `Server`：生命周期外壳
- `Compositor`：service 蓝图与 runtime 编排
- `RoutingService<TContext>` / `RouterModule<TContext>`：路由层抽象

功能特点：

- service 蓝图和 runtime 实例分离，便于理解“如何装配”和“如何运行”
- 使用 `ServiceTag + instanceId` 标识 service，避免字符串式误用
- 文件 service 支持“启动时挂载 + 运行期变化感知”
- API 路由可自动生成 OpenAPI 与 Swagger UI
- 请求处理接入线程池，适合轻量并发服务

### 2. 如何构建、部署、测试

本地构建：

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

本地运行：

```powershell
.\build\server.exe
```

或：

```powershell
Set-Location build
.\server.exe
```

Docker：

```powershell
docker build -t cpp-server .
docker run -d --rm --name cpp-server -p 8080:8080 -p 8081:8081 cpp-server
```

测试：

```powershell
ctest --test-dir build --output-on-failure
curl http://127.0.0.1:8080/
curl http://127.0.0.1:8080/status
curl http://127.0.0.1:8080/docs
curl http://127.0.0.1:8081/hello.txt
```

端口覆盖示例：

```cpp
CppServer::Core::ServerOptions options;
options.service_port_overrides = {
    {9000, 1},
    {9001, 1},
};

CppServer::Core::Server server(options);
CppServer::Application::ConfigureApplication(server, options);
server.Start();
```

### 3. 二次开发指南和样例

在已有 `api` service 下加新路由：

1. 新建一个继承 `RouterModule<TContext>` 的 router
2. 在 `Application.cpp` 中通过 `.AddRouter<...>()` 注册
3. 重新构建后检查 `/docs`

最小示例：

```cpp
template <typename TContext>
class TimeRouter final : public CppServer::Routing::RouterModule<TContext> {
public:
  std::string RouterName() const override { return "TIME"; }
  void Register(httplib::API::Router<TContext> &router) override {
    router.Get("/time", [](const httplib::Request &) { return "ok"; });
  }
};
```

```cpp
.AddRouter<CppServer::Routers::TimeRouter<ApiServiceContext>>()
```

添加新 service：

1. 定义新的 `Context`
2. 定义新的 `ServiceTag`
3. 编写 router
4. 在 `Application.cpp` 中 `Compose<NewServiceTag>(...)`

最小示例：

```cpp
struct Admin {
  using Context = CppServer::Services::Admin::Context;
  static constexpr std::uint16_t ID = 10;
  inline static constexpr std::string_view DisplayName = "admin";
};
```

```cpp
compositor
    .Compose<Admin>(CppServer::Core::DEFAULT_SERVICE_INSTANCE_ID, {8090, 1})
    .AddRouter<CppServer::Routers::AdminRouter<Admin::Context>>();
```

二开建议：

- 尽量把状态放在 runtime 自己的 `TContext` 中
- 如需共享状态，自己负责加锁或使用原子
- `ConfigureHttpServer(...)` 适合挂底层行为
- `ConfigureService(...)` 适合做 service 级装配

### 4. 项目主要类结构和模板行为

启动链路：

```text
main.cpp
  -> Server
  -> Application::ConfigureApplication(...)
  -> Compositor.Compose<TServiceTag>(...)
  -> ComposedService<TContext>  // 蓝图
  -> Server.Start()
  -> ServiceRuntimeSet<TContext> // 运行集合
```

关键点：

- `ComposedService<TContext>` 是蓝图，不直接监听端口
- `ServiceRuntimeSet<TContext>` 是蓝图实例化后的运行集合，一个 service 可对应多个端口 runtime
- `TServiceTag` 至少需要提供 `Context`、`ID`、`DisplayName`
- `TContext` 是每个 runtime 的上下文，不同 service 可有不同结构
- `ServiceKey = (ServiceTagId << 16) | ServiceInstanceId`

建议阅读顺序：`main.cpp` -> `Application.cpp` -> `Server.*` -> `Compositor.h` -> `RoutingService.h` -> `services/*`

---

## English

### 1. Overview

This is a lightweight C++ HTTP service skeleton built on `cpp-httplib`, `nlohmann/json`, and a custom thread-pool adapter. It ships with two default services:

- `api`: `/`, `/status`, `/sample/*`, `/docs`
- `file`: static file mount from `mount/`

Core structure:

- `Application::ConfigureApplication(...)`: application composition entry
- `Server`: lifecycle shell
- `Compositor`: service blueprint and runtime orchestration
- `RoutingService<TContext>` / `RouterModule<TContext>`: routing abstraction

Highlights:

- clear separation between service blueprints and runtime instances
- `ServiceTag + instanceId` identity model instead of string-only lookup
- file service supports startup mount plus runtime change awareness
- API routes can generate OpenAPI and Swagger UI automatically
- request execution is backed by a thread pool

### 2. Build, Deploy, Test

Build locally:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Run locally:

```powershell
.\build\server.exe
```

Or:

```powershell
Set-Location build
.\server.exe
```

Docker:

```powershell
docker build -t cpp-server .
docker run -d --rm --name cpp-server -p 8080:8080 -p 8081:8081 cpp-server
```

Test:

```powershell
ctest --test-dir build --output-on-failure
curl http://127.0.0.1:8080/
curl http://127.0.0.1:8080/status
curl http://127.0.0.1:8080/docs
curl http://127.0.0.1:8081/hello.txt
```

Port override example:

```cpp
CppServer::Core::ServerOptions options;
options.service_port_overrides = {
    {9000, 1},
    {9001, 1},
};

CppServer::Core::Server server(options);
CppServer::Application::ConfigureApplication(server, options);
server.Start();
```

### 3. Extension Guide and Examples

To add a new router to the existing `api` service:

1. create a router derived from `RouterModule<TContext>`
2. register it in `Application.cpp` with `.AddRouter<...>()`
3. rebuild and check `/docs`

Minimal example:

```cpp
template <typename TContext>
class TimeRouter final : public CppServer::Routing::RouterModule<TContext> {
public:
  std::string RouterName() const override { return "TIME"; }
  void Register(httplib::API::Router<TContext> &router) override {
    router.Get("/time", [](const httplib::Request &) { return "ok"; });
  }
};
```

```cpp
.AddRouter<CppServer::Routers::TimeRouter<ApiServiceContext>>()
```

To add a new service:

1. define a new `Context`
2. define a new `ServiceTag`
3. implement a router
4. compose it with `Compose<NewServiceTag>(...)`

```cpp
struct Admin {
  using Context = CppServer::Services::Admin::Context;
  static constexpr std::uint16_t ID = 10;
  inline static constexpr std::string_view DisplayName = "admin";
};
```

```cpp
compositor
    .Compose<Admin>(CppServer::Core::DEFAULT_SERVICE_INSTANCE_ID, {8090, 1})
    .AddRouter<CppServer::Routers::AdminRouter<Admin::Context>>();
```

Extension advice:

- keep state runtime-local in `TContext` when possible
- synchronize shared mutable state yourself
- use `ConfigureHttpServer(...)` for low-level server behavior
- use `ConfigureService(...)` for service-level composition

### 4. Main Structure and Template Behavior

Runtime flow:

```text
main.cpp
  -> Server
  -> Application::ConfigureApplication(...)
  -> Compositor.Compose<TServiceTag>(...)
  -> ComposedService<TContext>  // blueprint
  -> Server.Start()
  -> ServiceRuntimeSet<TContext> // runtime set
```

Key points:

- `ComposedService<TContext>` is a blueprint, not a live listener
- `ServiceRuntimeSet<TContext>` is the realized runtime set and may contain multiple port runtimes
- `TServiceTag` should provide `Context`, `ID`, and `DisplayName`
- `TContext` is per-runtime state
- `ServiceKey = (ServiceTagId << 16) | ServiceInstanceId`

Suggested reading order: `main.cpp` -> `Application.cpp` -> `Server.*` -> `Compositor.h` -> `RoutingService.h` -> `services/*`
