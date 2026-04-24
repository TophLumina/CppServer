# Minimal C++ Server (Docker)

A lightweight C++ HTTP server built with `cpp-httplib` and `nlohmann/json`.
一个基于 `cpp-httplib` 和 `nlohmann/json` 的轻量 C++ HTTP 服务。

The current version tries to listen on the following ports:
当前版本会尝试监听以下端口：

- `8080`
- `8081`
- `8082`
- `8083`

If a preferred port is unavailable, the server automatically falls back to any available port.
如果某个首选端口不可用，服务会自动回退并绑定到可用端口。

## 1) Build image / 构建镜像

```powershell
docker build -t cpp-server .
```

## 2) Run container / 运行容器

Map all 4 ports to the host to make every listener reachable.
建议将 4 个端口都映射到主机，确保所有监听器都可访问。

```powershell
docker run -d --name cpp-server `
	-p 8080:8080 `
	-p 8081:8081 `
	-p 8082:8082 `
	-p 8083:8083 `
	cpp-server
```

Use container logs to confirm the actual bound ports.
可通过容器日志确认实际绑定端口。

```powershell
docker logs cpp-server
```

## 3) Test endpoints / 测试接口

You can use any bound port (example below uses `8080`).
你可以使用任一已绑定端口（下面示例使用 `8080`）。

```powershell
curl http://127.0.0.1:8080/
curl http://127.0.0.1:8080/status
curl http://127.0.0.1:8080/request-status
```

## Endpoint behavior / 接口行为

- `/` returns JSON: `ok`, `message`
- `/` 返回 JSON：`ok`, `message`
- `/status` returns JSON: `ok`, `uptime_seconds`, `request_count`
- `/status` 返回 JSON：`ok`, `uptime_seconds`, `request_count`
- `/request-status` returns JSON: `ok`, `method`, `path`, `uptime_seconds`, `request_count`
- `/request-status` 返回 JSON：`ok`, `method`, `path`, `uptime_seconds`, `request_count`

## Expose to external network / 对外网络暴露

- The server listens on `0.0.0.0` inside the container.
- 服务在容器内监听 `0.0.0.0`。
- In production, open only required ports (for example, `8080-8083`).
- 生产环境请只开放需要的端口（例如 `8080-8083`）。
- On cloud VMs, allow matching inbound rules in security groups/firewalls.
- 云服务器部署时，需在安全组或防火墙中放行对应入站端口。
