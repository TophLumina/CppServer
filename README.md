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
curl http://127.0.0.1:8080/
curl http://127.0.0.1:8080/sample
curl http://127.0.0.1:8080/docs
curl http://127.0.0.1:8080/docs/openapi.json
```

## Endpoint behavior / 接口行为

- `/` returns runtime health JSON: `alive`, `uptime_seconds`, `host_cpu_usage_percent`, `host_memory_usage_percent`, `host_memory_usage_share`, `host_memory_usage`, `rtt_ms`, `rtt_source`
- `/` 返回运行时健康 JSON：`alive`, `uptime_seconds`, `host_cpu_usage_percent`, `host_memory_usage_percent`, `host_memory_usage_share`, `host_memory_usage`, `rtt_ms`, `rtt_source`
- `/sample` returns plain text ASCII art
- `/sample` 返回纯文本 ASCII 字符画
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

- `GET /`: enabled, TTL=`100ms`, `max_entries=16`
- `GET /`：启用缓存，TTL=`100ms`，`max_entries=16`
- `GET /sample`: disabled (`std::nullopt`)
- `GET /sample`：不启用缓存（`std::nullopt`）

## Register a new router / 注册新路由

Use `SampleRouter` as the reference implementation.
可参考 `SampleRouter` 作为实现模板。

1. Create a new router class in `services/` that derives from `CppServer::Services::RouterModule<TContext>`.
2. 在 `services/` 下新增一个继承 `CppServer::Services::RouterModule<TContext>` 的路由类。
3. Implement `RouterName()` and `Register(...)` in that class.
4. 在该类中实现 `RouterName()` 与 `Register(...)`。
5. (Optional) implement `ResolveCachePolicy(method, path)` to configure endpoint-level caching.
6. （可选）实现 `ResolveCachePolicy(method, path)` 以配置端点级缓存策略。
7. Include the router header in `Core.cpp`.
8. 在 `Core.cpp` 中包含该路由头文件。
9. Add one line in startup wiring:
10. 在启动装配代码中添加一行：

```cpp
services.RegisterRouter<CppServer::Routers::YourRouter<Utils::AppContext>>();
```

Reference:
参考：

- `services/SampleRouter.h` (`SampleRouter`)
- `Core.cpp` (router wiring with `services.RegisterRouter<...>()`)

## Expose to external network / 对外网络暴露

- The server listens on `0.0.0.0` inside the container.
- 服务在容器内监听 `0.0.0.0`。
- In production, open only required ports (for example, `8080-8081`).
- 生产环境请只开放需要的端口（例如 `8080-8081`）。
- On cloud VMs, allow matching inbound rules in security groups/firewalls.
- 云服务器部署时，需在安全组或防火墙中放行对应入站端口。
