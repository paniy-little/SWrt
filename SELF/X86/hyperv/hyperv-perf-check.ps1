# =============================================================================
# hyperv-perf-check.ps1 — Hyper-V host 侧 vRSS / VMMQ 只读检查（可选 opt-in 开启）
#
# 用法（只读）：
#   .\hyperv-perf-check.ps1 -VMName "OpenWrt"
#
# 显式开启推荐项（vRSS + VMMQ）：
#   .\hyperv-perf-check.ps1 -VMName "OpenWrt" -EnableRecommended
#
# 设计原则：
#   - 默认只读，绝不修改 host NIC / 不自动配置 SR-IOV / 不固定 VMMQ queue pair 数 /
#     不自动改 CPU affinity / 不改 VM CPU 数。
#   - 只有显式传入 -EnableRecommended 时才执行 Set-VMNetworkAdapter 开启 vRSS/VMMQ。
# =============================================================================
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$VMName,

    [switch]$EnableRecommended
)

$ErrorActionPreference = 'Stop'

function Show-Summary {
    param($Adapter)
    Write-Host "VM NIC Summary: $($Adapter.Name)" -ForegroundColor Cyan
    Write-Host "  vRSS          : $($Adapter.VrssEnabled)"
    Write-Host "  VMMQ          : $($Adapter.VmmqEnabled)"
    Write-Host "  VMMQ queuePairs: $($Adapter.VmmqQueuePairs)"
    Write-Host "  VMQ weight    : $($Adapter.VmqWeight)"
}

# 1. 只读检查 VM 网络适配器
$adapter = Get-VMNetworkAdapter -VMName $VMName -ErrorAction SilentlyContinue
if (-not $adapter) {
    Write-Error "VM '$VMName' not found or has no network adapters."
    exit 1
}
Show-Summary -Adapter $adapter

# 2. 只读检查宿主物理网卡 RSS
Write-Host ""
Write-Host "Host NIC RSS (Get-NetAdapterRss):" -ForegroundColor Cyan
try {
    Get-NetAdapterRss | Format-Table -AutoSize
} catch {
    Write-Host "  (no RSS-capable host adapter exposed or no permission)"
}

# 3. 仅在显式 opt-in 时修改
if ($EnableRecommended) {
    Write-Host ""
    Write-Host "Enabling recommended vRSS + VMMQ for '$VMName' ..." -ForegroundColor Yellow
    Set-VMNetworkAdapter -VMName $VMName -VrssEnabled $true -VmmqEnabled $true
    Write-Host "Done. Re-check in guest with: swrt-vm-perf" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "Read-only mode. To enable vRSS/VMMQ, re-run with -EnableRecommended." -ForegroundColor DarkGray
}