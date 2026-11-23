# PassWall / HomeProxy 隔离运行态证据（2026-11-20）

## 环境与边界

- 临时容器：`codex-proxy-runtime-lab-20261120-a1`，ImmortalWrt 24.10.4 `armsr/armv8` / `aarch64_generic`，镜像 ID `sha256:853b90acbb625db138ca4312702105ad99bae944fda6ea9419295fe99438aeca`。
- 安装版本：HomeProxy `26.300.08316~d72e293`、PassWall1 `26.11.11-r1`、PassWall2 `26.11.11-r1`、sing-box `1.12.25-r1`、microsocks `1.0.5-r2`。
- 容器实际共享 OrbStack `7.2.12-orbstack` 内核；包内 tproxy kmod 针对 `6.6.110` ABI。容器也没有 `CAP_NET_ADMIN`。因此不构成透明代理、nft/redirect/tproxy 运行态验收。
- 上游是本机隔离的 microsocks SOCKS5 fixture（`127.0.0.1:1080` 与 `:1084`），其后由容器普通网络出站。HTTPS 200 只证明插件生成的应用层代理链可用，不证明生产节点、OpenWrt 透明拦截或 CFI 自动热应用。

## 生成配置与受控节点变更

三个插件均从 UCI SOCKS 节点生成 sing-box 配置，outbound `server_port` / route final 随 UCI 节点更新。配置和 HTTPS 结果如下：

| 插件 | 入站 | 节点端口变更 | 进程 PID 变更 | 更新前/后 HTTPS |
|---|---|---|---|---|
| HomeProxy | mixed `5330` | `1084 → 1080` | `5647 → 6250` | 200/577 B；reload 后一次立即连接失败，重试为 200/577 B |
| PassWall1 | UCI SOCKS `1081` | `1080 → 1084` | `6979 → 7806` | 200/577 B；回切测试 PID `7806 → 8627`、`1084 → 1080` 后仍 200/577 B |
| PassWall2 | UCI SOCKS `1082` | `1080 → 1084` | `9319 → 9975` | 200/577 B |

实际请求：

```sh
curl -x socks5h://127.0.0.1:5330 https://example.com  # HomeProxy
curl -x socks5h://127.0.0.1:1081 https://example.com  # PassWall1
curl -x socks5h://127.0.0.1:1082 https://example.com  # PassWall2
```

## Reload 与限制

- HomeProxy 的 procd `reload_service()` 调用 `stop` 后 `start`。本次 `/etc/init.d/homeproxy reload` 观察到 sing-box PID 更换；一次紧跟 reload 的 curl 为 HTTP 000 / exit 7，稍后重试才恢复 200。因此此 reload 不是同一代理进程热改，也未达到全程无停机或已有连接零中断。
- PassWall1 / PassWall2 的 `/etc/init.d/* reload` 明确打印 `does not support configuration reloading` 和 `Performing full restart instead`，实现路径为 app stop 后 app start；PID 变化与生成 JSON 更新已观察。容器日志同时出现 nft 清理 `Operation not permitted`，透明代理恢复未能验证。
- HomeProxy feed 与 PassWall feed 的默认 sing-box 版本组合不兼容：1.14.4 拒绝 HomeProxy 生成的旧 inbound 字段；改用 ImmortalWrt 同发行版 `1.12.25-r1` 后 `sing-box check` 成功。不要把该 feed 混装行为标成支持。
- 为让此容器内 HomeProxy procd child 运行，实验中仅在临时容器禁用了 `/sbin/ujail` 并创建缺失的证书目录；这是容器权限限制下的实验规避，不是设备建议。生产兼容的 jail / capabilities 行为还需匹配内核设备验收。

## 验收判定

这组测试证明三套插件可从 UCI 生成 SOCKS outbound，并承载应用层 HTTPS 出站；三个插件的服务 reload 均会更换代理子进程。由于测试没有透明代理路径、匹配内核 kmods、真实远端代理节点或零停机热应用证据，PassWall、PassWall2、HomeProxy 均不能据此标为 CFI 自动热应用“已支持”。

运行态文件快照、服务日志、内核与包版本、curl 响应保存在本次会话临时目录 `/tmp/proxy-runtime-lab-evidence-20261120/`（压缩包：`/tmp/proxy-runtime-lab-evidence-20261120.tar.gz`）。
