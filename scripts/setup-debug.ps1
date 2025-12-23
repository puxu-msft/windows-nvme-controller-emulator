# setup-debug.ps1
# 配置 VNVME 驱动调试参数
# 需要管理员权限运行

#Requires -RunAsAdministrator

param(
    [ValidateRange(0, 5)]
    [int]$Level = 4,  # 默认 DEBUG 级别
    
    [ValidateScript({ $_ -ge 0 })]
    [uint32]$Flags = 0xFFFFFFFF,  # 默认所有模块
    
    [switch]$Reset,
    [switch]$Show
)

$RegistryPath = "HKLM:\SYSTEM\CurrentControlSet\Services\vnvme\Parameters"

# 调试级别名称
$LevelNames = @{
    0 = "NONE"
    1 = "ERROR"
    2 = "WARNING"
    3 = "INFO"
    4 = "DEBUG"
    5 = "VERBOSE"
}

# 调试模块标志
$FlagNames = @{
    0x00000001 = "DRIVER"
    0x00000002 = "PNP"
    0x00000004 = "IOCTL"
    0x00000008 = "BUS"
    0x00000010 = "BAR0"
    0x00000020 = "PCIE"
    0x00000040 = "DOORBELL"
    0x00000080 = "QUEUE"
    0x00000100 = "ADMIN"
    0x00000200 = "IO"
    0x00000400 = "PRP"
    0x00000800 = "STORAGE"
    0x00001000 = "SHM"
    0x00002000 = "USER"
    0x00004000 = "PERF"
}

function Show-CurrentSettings {
    Write-Host "`n===== VNVME Debug Settings =====" -ForegroundColor Cyan
    
    if (Test-Path $RegistryPath) {
        $props = Get-ItemProperty -Path $RegistryPath -ErrorAction SilentlyContinue
        
        $level = if ($props.DebugLevel) { $props.DebugLevel } else { 3 }
        $flags = if ($props.DebugFlags) { $props.DebugFlags } else { 0xFFFFFFFF }
        
        Write-Host "DebugLevel: $level ($($LevelNames[$level]))" -ForegroundColor Yellow
        Write-Host "DebugFlags: 0x$($flags.ToString('X8'))" -ForegroundColor Yellow
        
        Write-Host "`nEnabled Modules:" -ForegroundColor Green
        foreach ($flag in $FlagNames.Keys | Sort-Object) {
            if ($flags -band $flag) {
                Write-Host "  [X] $($FlagNames[$flag])" -ForegroundColor Green
            } else {
                Write-Host "  [ ] $($FlagNames[$flag])" -ForegroundColor DarkGray
            }
        }
    } else {
        Write-Host "Registry path not found. Using defaults." -ForegroundColor Yellow
        Write-Host "DebugLevel: 3 (INFO)" -ForegroundColor Yellow
        Write-Host "DebugFlags: 0xFFFFFFFF (ALL)" -ForegroundColor Yellow
    }
    
    Write-Host "================================`n" -ForegroundColor Cyan
}

function Reset-Settings {
    if (Test-Path $RegistryPath) {
        Remove-ItemProperty -Path $RegistryPath -Name "DebugLevel" -ErrorAction SilentlyContinue
        Remove-ItemProperty -Path $RegistryPath -Name "DebugFlags" -ErrorAction SilentlyContinue
        Write-Host "Debug settings reset to defaults." -ForegroundColor Green
    } else {
        Write-Host "Nothing to reset." -ForegroundColor Yellow
    }
}

function Set-DebugSettings {
    param($Level, $Flags)
    
    # 确保注册表路径存在
    if (-not (Test-Path $RegistryPath)) {
        New-Item -Path $RegistryPath -Force | Out-Null
        Write-Host "Created registry path: $RegistryPath" -ForegroundColor Green
    }
    
    # 设置调试级别
    Set-ItemProperty -Path $RegistryPath -Name "DebugLevel" -Value $Level -Type DWord
    Write-Host "Set DebugLevel = $Level ($($LevelNames[$Level]))" -ForegroundColor Green
    
    # 设置调试标志
    Set-ItemProperty -Path $RegistryPath -Name "DebugFlags" -Value $Flags -Type DWord
    Write-Host "Set DebugFlags = 0x$($Flags.ToString('X8'))" -ForegroundColor Green
    
    Write-Host "`nNote: Restart the driver for changes to take effect." -ForegroundColor Yellow
}

# 主逻辑
if ($Show) {
    Show-CurrentSettings
} elseif ($Reset) {
    Reset-Settings
    Show-CurrentSettings
} else {
    Set-DebugSettings -Level $Level -Flags $Flags
    Show-CurrentSettings
}
