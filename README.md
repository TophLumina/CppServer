# Minimal C++ Server (Docker)

A lightweight C++ HTTP server built with `cpp-httplib` and `nlohmann/json`.
一个基于 `cpp-httplib` 和 `nlohmann/json` 的轻量 C++ HTTP 服务。

The current version tries to listen on the following ports:
当前版本会尝试监听以下端口：

- `8080`
- `8081`

If binding a preferred port fails, the process aborts immediately.
如果某个首选端口绑定失败，进程会立刻中止（abort）。

## 1) Build image / 构建镜像

```powershell
docker build -t cpp-server .
```

## 2) Run container / 运行容器

Map all 2 ports to the host to make every listener reachable.
建议将 2 个端口都映射到主机，确保所有监听器都可访问。

```powershell
docker run -d --name cpp-server `
	-p 8080:8080 `
	-p 8081:8081 `
	cpp-server
```

## 3) Test endpoints / 测试接口

You can use any bound port (example below uses `8080`).
你可以使用任一已绑定端口（下面示例使用 `8080`）。

```powershell
curl http://127.0.0.1:8080/status
curl http://127.0.0.1:8080/sample/asciiart
curl http://127.0.0.1:8080/docs
curl http://127.0.0.1:8080/docs/openapi.json
```

## Endpoint behavior / 接口行为

- `/status` returns runtime health JSON: `alive`, `uptime_seconds`, `host_cpu_usage_percent`, `host_memory_usage_percent`, `host_memory_usage_share`, `host_memory_usage`, `rtt_ms`, `rtt_source`
- `/status` 返回运行时健康 JSON：`alive`, `uptime_seconds`, `host_cpu_usage_percent`, `host_memory_usage_percent`, `host_memory_usage_share`, `host_memory_usage`, `rtt_ms`, `rtt_source`
- `/sample/asciiart` returns plain text ASCII art
- `/sample/asciiart` 返回纯文本 ASCII 字符画
- `/docs` serves Swagger UI
- `/docs` 提供 Swagger UI
- `/docs/openapi.json` returns OpenAPI JSON
- `/docs/openapi.json` 返回 OpenAPI JSON

## Cache policy / 缓存策略

- Cache policy is declared in each router via `ResolveCachePolicy(method, path)`.
- 缓存策略通过各路由中的 `ResolveCachePolicy(method, path)` 声明。
- Return `std::nullopt` to disable cache for that endpoint.
- 返回 `std::nullopt` 表示该端点不启用缓存。
- API resolves policy once at route registration time and applies it in endpoint runtime handlers.
- API 在路由注册阶段解析策略，并在端点运行时处理逻辑中应用。

Current policies in this project / 当前项目策略：

- `GET /status`: enabled, TTL=`100ms`, `max_entries=16`
- `GET /status`：启用缓存，TTL=`100ms`，`max_entries=16`
- `GET /sample/asciiart`: disabled (`std::nullopt`)
- `GET /sample/asciiart`：不启用缓存（`std::nullopt`）

## Register a new router / 注册新路由

Use `SampleRouter` as the reference implementation.
可参考 `SampleRouter` 作为实现模板。

1. Create a new router class in `services/` that derives from `CppServer::Routing::RouterModule<TContext>`.
2. 在 `services/` 下新增一个继承 `CppServer::Routing::RouterModule<TContext>` 的路由类。
3. Implement `RouterName()` and `Register(...)` in that class.
4. 在该类中实现 `RouterName()` 与 `Register(...)`。
5. (Optional) implement `ResolveCachePolicy(method, path)` to configure endpoint-level caching.
6. （可选）实现 `ResolveCachePolicy(method, path)` 以配置端点级缓存策略。
7. Include the router header in `Application.cpp`, where default service composition is assembled.
8. 在 `Application.cpp` 中包含该路由头文件，并在默认 service 组合装配逻辑中注册它。
9. Add one line in startup wiring:
10. 在启动装配代码中添加一行：

```cpp
apiservice.RegisterRouter<CppServer::Routers::YourRouter<CppServer::Services::SimpleApi::Context>>();
```

Reference:
参考：

- `services/simpleapi/SampleRouter.h` (`SampleRouter`)
- `Application.cpp` (default service composition with `compositor.Compose<TServiceTag>(instance_id, ...)`)
- `ServiceTags.h` (formal extension point for default and custom service tag definitions)

## Customize bootstrap / 自定义启动注入

The default startup wiring now lives in `Application.cpp`, so secondary development starts from `Application` and `main.cpp` stays as a thin entrypoint.
当前默认启动装配已移入 `Application.cpp`，因此二次开发从 `Application` 入手，`main.cpp` 只保留薄启动入口。

```cpp
#include "ServiceTags.h"
#include "services/simpleapi/YourRouter.h"

void CppServer::Application::ConfigureApplication(
                         CppServer::Core::Server &server,
                         const CppServer::Core::ServerOptions &options) {
  auto &compositor = server.Composition();
  compositor.Clear();

  compositor
    .Compose<CppServer::Core::ServiceTags::Api>(
      CppServer::Core::DEFAULT_SERVICE_INSTANCE_ID, {8080, 1})
    .AddRouter<CppServer::Routers::YourRouter<
      CppServer::Services::SimpleApi::Context>>()
    .AddSwaggerUI();
}
```

`TServiceTag` must provide `Context`, a 16-bit `ID`, and `DisplayName`.
`TServiceTag` 需要提供 `Context`、16 位 `ID` 和 `DisplayName`。

Put reusable default or custom tags in `ServiceTags.h` or a sibling custom tag header.
可复用的默认或自定义 tag 应放在 `ServiceTags.h` 或并列的自定义 tag 头文件中。

Available injection points:
可用注入点：

- `Application::ConfigureApplication(...)` as the central application customization entrypoint
- `Server::Composition().Compose<TServiceTag>(instance_id, ...)` to replace or add a composed service inside `Application.cpp`
- `Server::Composition().Find<TServiceTag>(instance_id)` to mutate an existing composed service inside `Application.cpp`

## Expose to external network / 对外网络暴露

- The server listens on `0.0.0.0` inside the container.
- 服务在容器内监听 `0.0.0.0`。
- In production, open only required ports (for example, `8080-8081`).
- 生产环境请只开放需要的端口（例如 `8080-8081`）。
- On cloud VMs, allow matching inbound rules in security groups/firewalls.
- 云服务器部署时，需在安全组或防火墙中放行对应入站端口。
