# Linux 6.12 内核补丁

## 结论
**编译和使用 SFE v2 并不强制依赖本目录下的补丁。**

SFE v2 通过外部模块兼容层（`sfe_compat.h`）对 Linux 5.15 ~ 6.12 的 API 变动作了自动适配，在官方标准内核上即可直接作为外部模块加载运行。

---

## 文件列表

### `999-optional-dev-fast-xmit-optimization.patch` (可选)

* **改动点**：
  在 `net/core/dev.c` 与 `include/linux/netdevice.h` 中导出 `dev_fast_xmit` 函数与 `DEV_FAST_XMIT_SUPPORTED` 宏。
* **技术原理**：
  SFE 在软件快转时，通过 `HARD_TX_TRYLOCK` 尝试直接抢占网卡发送队列，并直接调用网卡驱动的 `netdev_start_xmit`。若尝试失败则自动退回普通 `dev_queue_xmit`。该机制绕过了 Linux 的 qdisc 排队调度层，在万兆小包/高并发场景下能略微降低 CPU 软中断占用。
* **是否需要使用**：
  * **不打此补丁**：SFE 会自动退回调用标准的 `dev_queue_xmit` 发包，功能完整，稳定性一致；
  * **打此补丁**：若追求极致吞吐与微秒级延迟，可将此补丁放入 OpenWrt 的目标内核补丁目录（如 `target/linux/generic/pending-6.12/`）中编译。
