# SWrt AI 操作指南

## 项目本质

Fork 自 QiuSimons/YAOF 的 OpenWrt 自用固件编译项目。上游每 3 小时自动同步到 `SCRIPTS/`，`SELF/` 是自用覆盖层（不同步）。

## 文件对应关系

| 上游（SCRIPTS/） | 自用（SELF/） |
|---|---|
| `01_get_ready.sh` | `01_get_ready_self.sh` |
| `02_prepare_package.sh` | `02_prepare_package_self.sh` |
| `R2S/02_target_only.sh` | `R2S/02_target_only_self.sh` |
| `X86/02_target_only.sh` | `X86/02_target_only_self.sh` |
| `SEED/R2S/config.seed` | `R2S/config_self.seed` |
| `SEED/X86/config.seed` | `X86/config_self.seed` |

## 上游同步后检查流程

```bash
# 1. 看改了什么
git log --oneline -5
git diff HEAD~1 --name-only

# 2. 只关注 SCRIPTS/ 下的变更，逐个对比 SELF 对应文件
# 3. 判断是否需要同步
```

### 同步判断原则

- **上游 bug 修复 / 功能改进** → 同步到 SELF，保留自用定制
- **上游新增功能（SELF 不需要）** → 不同步
- **上游与自用定制冲突** → 询问用户决定

### SELF 自用定制（不要覆盖）

- FullCone-NAT / fullcone6 / Docker → 注释掉
- 默认 IP → 192.168.2.1
- R2S：LAN/WAN 交换、cortex-a53+crypto、LRNG 移除
- X86：Hyper-V 专用（model/pstate/CPU调频/网卡顺序）

## 铁律

1. **永远不修改 SCRIPTS/**，下次同步会被覆盖
2. **SELF 中的注释是故意的**，不要顺手取消注释
3. **config_self.seed 用注释而非删除**，删除会导致 defconfig 补回默认值
