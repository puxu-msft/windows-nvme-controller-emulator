# vnvme 安装脚本
# 用法: .\scripts\install.ps1 [-TestSigning] [-Force]
# 需要以管理员权限运行

param(
    [switch]$TestSigning,
    [switch]$Force
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

# 路径配置
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$DriversDir = $ScriptDir

# 如果从 scripts 目录运行，向上查找
if (-not (Test-Path (Join-Path $ScriptDir "vnvme.sys"))) {
    $DriversDir = Join-Path (Split-Path -Parent $ScriptDir) "build\Debug\x64\drivers"
}
if (-not (Test-Path (Join-Path $DriversDir "vnvme.sys"))) {
    $DriversDir = $ScriptDir  # 可能是打包后的目录
}

$DriverSys = Join-Path $DriversDir "vnvme.sys"
$DriverInf = Join-Path $DriversDir "vnvme.inf"
$ChildInf = Join-Path $DriversDir "vnvme_child.inf"

Write-Host "========================================" -ForegroundColor White
Write-Host "  VNVME 驱动安装脚本" -ForegroundColor White
Write-Host "========================================" -ForegroundColor White

# 检查文件
Write-Host "`n检查驱动文件..." -ForegroundColor Cyan

if (-not (Test-Path $DriverSys)) {
    throw "未找到驱动文件: $DriverSys"
}
if (-not (Test-Path $DriverInf)) {
    throw "未找到 INF 文件: $DriverInf"
}

Write-Host "  驱动文件: $DriverSys" -ForegroundColor Green
Write-Host "  INF 文件: $DriverInf" -ForegroundColor Green

# 启用测试签名
if ($TestSigning) {
    Write-Host "`n配置测试签名模式..." -ForegroundColor Cyan
    
    $testsigning = (bcdedit /enum | Select-String "testsigning" | Select-String "Yes")
    
    if (-not $testsigning) {
        Write-Host "  启用测试签名..." -ForegroundColor Yellow
        bcdedit /set testsigning on
        Write-Host "  测试签名已启用，需要重启才能生效" -ForegroundColor Yellow
        Write-Host "  请重启后重新运行此脚本 (不带 -TestSigning 参数)" -ForegroundColor Yellow
        
        $restart = Read-Host "是否现在重启? (Y/N)"
        if ($restart -eq "Y" -or $restart -eq "y") {
            Restart-Computer
        }
        exit 0
    } else {
        Write-Host "  测试签名已启用" -ForegroundColor Green
    }
}

# 查找 devcon
Write-Host "`n查找 devcon 工具..." -ForegroundColor Cyan

$devcon = $null
$searchPaths = @(
    "C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe",
    "C:\Program Files (x86)\Windows Kits\10\Tools\10.0.22621.0\x64\devcon.exe",
    "$env:WDKContentRoot\Tools\x64\devcon.exe",
    (Join-Path $ScriptDir "devcon.exe")
)

foreach ($path in $searchPaths) {
    if (Test-Path $path) {
        $devcon = $path
        break
    }
}

if (-not $devcon) {
    throw "未找到 devcon.exe，请安装 WDK 或将 devcon.exe 复制到脚本目录"
}

Write-Host "  devcon: $devcon" -ForegroundColor Green

# 检查现有安装
Write-Host "`n检查现有安装..." -ForegroundColor Cyan

$existingDevice = & $devcon find "ROOT\VNVME" 2>$null
if ($existingDevice -match "ROOT\\VNVME") {
    if ($Force) {
        Write-Host "  发现现有安装，正在卸载..." -ForegroundColor Yellow
        & $devcon remove "ROOT\VNVME"
        Start-Sleep -Seconds 2
    } else {
        throw "驱动已安装。使用 -Force 参数强制重新安装"
    }
}

# 安装主驱动
Write-Host "`n安装主驱动 (vnvme.sys)..." -ForegroundColor Cyan

Push-Location $DriversDir
try {
    $result = & $devcon install vnvme.inf "ROOT\VNVME" 2>&1
    Write-Host $result
    
    if ($LASTEXITCODE -ne 0) {
        throw "驱动安装失败"
    }
} finally {
    Pop-Location
}

# 验证安装
Write-Host "`n验证安装..." -ForegroundColor Cyan

$device = Get-PnpDevice | Where-Object { $_.InstanceId -like "*VNVME*" }
if ($device) {
    Write-Host "  设备状态: $($device.Status)" -ForegroundColor Green
    Write-Host "  实例 ID: $($device.InstanceId)" -ForegroundColor Green
} else {
    Write-Warning "  未能在 PnP 设备列表中找到设备"
}

# 检查控制设备
Write-Host "`n检查控制设备..." -ForegroundColor Cyan

$controlDevice = "\\.\VNVMEControl"
try {
    $handle = [System.IO.File]::Open($controlDevice, 'Open', 'ReadWrite', 'None')
    $handle.Close()
    Write-Host "  控制设备可访问: $controlDevice" -ForegroundColor Green
} catch {
    Write-Warning "  控制设备无法访问 (这可能是正常的，取决于驱动实现)"
}

# 安装子设备 INF (预安装)
if (Test-Path $ChildInf) {
    Write-Host "`n预安装子设备 INF..." -ForegroundColor Cyan
    
    pnputil /add-driver $ChildInf /install 2>&1 | ForEach-Object { Write-Host "  $_" }
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  子设备 INF 已预安装" -ForegroundColor Green
    }
}

# 完成
Write-Host "`n========================================" -ForegroundColor Green
Write-Host "  安装完成!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

Write-Host "`n下一步:" -ForegroundColor Cyan
Write-Host "  1. 启动 vnvme-server.exe 服务" -ForegroundColor White
Write-Host "  2. 使用 vnvmectl.exe 创建虚拟磁盘" -ForegroundColor White
Write-Host "  3. 在设备管理器中检查 NVMe 控制器" -ForegroundColor White
