# vnvme 卸载脚本
# 用法: .\scripts\uninstall.ps1 [-RemoveFiles] [-DisableTestSigning]
# 需要以管理员权限运行

param(
    [switch]$RemoveFiles,
    [switch]$DisableTestSigning
)

$ErrorActionPreference = "Stop"

# 检查管理员权限
function Test-Administrator {
    $currentUser = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentUser)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Administrator)) {
    Write-Host "错误: 此脚本需要管理员权限" -ForegroundColor Red
    Write-Host "请以管理员身份重新运行 PowerShell" -ForegroundColor Yellow
    exit 1
}

Write-Host "========================================" -ForegroundColor White
Write-Host "  VNVME 驱动卸载脚本" -ForegroundColor White
Write-Host "========================================" -ForegroundColor White

# 查找 devcon
Write-Host "`n查找 devcon 工具..." -ForegroundColor Cyan

$devcon = $null
$searchPaths = @(
    "C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe",
    "C:\Program Files (x86)\Windows Kits\10\Tools\10.0.22621.0\x64\devcon.exe",
    "$env:WDKContentRoot\Tools\x64\devcon.exe"
)

foreach ($path in $searchPaths) {
    if (Test-Path $path) {
        $devcon = $path
        break
    }
}

if (-not $devcon) {
    Write-Warning "未找到 devcon.exe，尝试使用 pnputil"
}

# 停止 vnvme-server 服务
Write-Host "`n停止 vnvme-server 服务..." -ForegroundColor Cyan

$serverProcess = Get-Process -Name "vnvme-server" -ErrorAction SilentlyContinue
if ($serverProcess) {
    Write-Host "  正在停止 vnvme-server.exe (PID: $($serverProcess.Id))..." -ForegroundColor Yellow
    Stop-Process -Id $serverProcess.Id -Force
    Start-Sleep -Seconds 2
    Write-Host "  服务已停止" -ForegroundColor Green
} else {
    Write-Host "  vnvme-server 未在运行" -ForegroundColor Gray
}

# 移除子设备
Write-Host "`n移除子设备 (PDO)..." -ForegroundColor Cyan

$childDevices = Get-PnpDevice | Where-Object { 
    $_.InstanceId -like "*VEN_1B36*DEV_0010*" -and 
    $_.InstanceId -like "*VNVME*" 
}

foreach ($device in $childDevices) {
    Write-Host "  移除: $($device.InstanceId)" -ForegroundColor Yellow
    
    if ($devcon) {
        & $devcon remove "@$($device.InstanceId)" 2>&1 | Out-Null
    } else {
        pnputil /remove-device "$($device.InstanceId)" /subtree 2>&1 | Out-Null
    }
}

if (-not $childDevices) {
    Write-Host "  未发现子设备" -ForegroundColor Gray
}

# 移除主设备
Write-Host "`n移除主设备 (FDO)..." -ForegroundColor Cyan

if ($devcon) {
    $result = & $devcon find "ROOT\VNVME" 2>&1
    
    if ($result -match "ROOT\\VNVME") {
        Write-Host "  移除 ROOT\VNVME..." -ForegroundColor Yellow
        & $devcon remove "ROOT\VNVME" 2>&1 | ForEach-Object { Write-Host "  $_" }
    } else {
        Write-Host "  ROOT\VNVME 设备未安装" -ForegroundColor Gray
    }
} else {
    $mainDevice = Get-PnpDevice | Where-Object { $_.InstanceId -like "ROOT\VNVME*" }
    if ($mainDevice) {
        pnputil /remove-device "$($mainDevice.InstanceId)" 2>&1 | ForEach-Object { Write-Host "  $_" }
    }
}

# 删除驱动包
Write-Host "`n删除驱动包..." -ForegroundColor Cyan

$driverPackages = pnputil /enum-drivers | Select-String -Context 0,5 "vnvme" | 
    ForEach-Object { 
        if ($_ -match "oem\d+\.inf") { $matches[0] } 
    }

foreach ($package in $driverPackages) {
    Write-Host "  删除驱动包: $package" -ForegroundColor Yellow
    pnputil /delete-driver $package /uninstall /force 2>&1 | ForEach-Object { Write-Host "    $_" }
}

if (-not $driverPackages) {
    Write-Host "  未发现已安装的驱动包" -ForegroundColor Gray
}

# 删除驱动文件
if ($RemoveFiles) {
    Write-Host "`n删除驱动文件..." -ForegroundColor Cyan
    
    $filesToRemove = @(
        "$env:SystemRoot\System32\drivers\vnvme.sys",
        "$env:SystemRoot\INF\vnvme.inf",
        "$env:SystemRoot\INF\vnvme_child.inf"
    )
    
    foreach ($file in $filesToRemove) {
        if (Test-Path $file) {
            Remove-Item $file -Force -ErrorAction SilentlyContinue
            Write-Host "  已删除: $file" -ForegroundColor Yellow
        }
    }
}

# 清理注册表
Write-Host "`n清理注册表..." -ForegroundColor Cyan

$regPaths = @(
    "HKLM:\SYSTEM\CurrentControlSet\Services\vnvme",
    "HKLM:\SYSTEM\CurrentControlSet\Enum\ROOT\VNVME"
)

foreach ($regPath in $regPaths) {
    if (Test-Path $regPath) {
        Remove-Item $regPath -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "  已删除: $regPath" -ForegroundColor Yellow
    }
}

# 禁用测试签名
if ($DisableTestSigning) {
    Write-Host "`n禁用测试签名模式..." -ForegroundColor Cyan
    bcdedit /set testsigning off
    Write-Host "  测试签名已禁用，需要重启生效" -ForegroundColor Yellow
}

# 验证
Write-Host "`n验证卸载..." -ForegroundColor Cyan

$remainingDevices = Get-PnpDevice | Where-Object { $_.InstanceId -like "*VNVME*" }
if ($remainingDevices) {
    Write-Warning "  仍有设备残留:"
    foreach ($device in $remainingDevices) {
        Write-Host "    - $($device.InstanceId)" -ForegroundColor Yellow
    }
    Write-Host "  可能需要重启后才能完全清理" -ForegroundColor Yellow
} else {
    Write-Host "  所有 VNVME 设备已移除" -ForegroundColor Green
}

# 完成
Write-Host "`n========================================" -ForegroundColor Green
Write-Host "  卸载完成!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

if ($DisableTestSigning -or $remainingDevices) {
    Write-Host "`n建议重启系统以完成清理" -ForegroundColor Yellow
}
