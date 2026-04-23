# Minimal C++ Server (Docker)

## 1) Build image

```powershell
docker build -t cpp-server .
```

## 2) Run container

```powershell
docker run -d --name cpp-server -p 8080:8080 cpp-server
```

## 3) Test endpoints

```powershell
curl http://127.0.0.1:8080/
curl http://127.0.0.1:8080/status
curl http://127.0.0.1:8080/request-status
```

## Endpoint behavior

- `/` returns plain text server banner
- `/status` returns server status JSON (`ok`, `uptime_seconds`, `request_count`)
- `/request-status` returns current request meta and status JSON (`method`, `path`, `uptime_seconds`, `request_count`)

## Expose to external network

- Server listens on `0.0.0.0:8080` inside container
- Use `-p 8080:8080` to map host port
- If deploying on cloud VM, open inbound `8080` in security group/firewall
