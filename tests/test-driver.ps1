#
# test-driver.ps1
# VNVME 驱动测试脚本
#

param(
    [switch]$Install,
    [switch]$Uninstall,
    [switch]$Test,
    [switch]$All,
    [string]$DriverPath = "$PSScriptRoot\..\x64\Debug\vnvme"
)

$ErrorActionPreference = "Stop"

function Write-TestHeader($name) {
    Write-Host ""
    Write-Host "=" * 60 -ForegroundColor Cyan
    Write-Host "TEST: $name" -ForegroundColor Cyan
    Write-Host "=" * 60 -ForegroundColor Cyan
}

function Write-TestResult($passed, $message) {
    if ($passed) {
        Write-Host "[PASS] $message" -ForegroundColor Green
    } else {
        Write-Host "[FAIL] $message" -ForegroundColor Red
    }
}

function Test-AdminPrivileges {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]$identity
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Install-VnvmeDriver {
    Write-TestHeader "Installing VNVME Driver"
    
    if (-not (Test-AdminPrivileges)) {
        Write-Host "Error: Administrator privileges required" -ForegroundColor Red
        return $false
    }
    
    $infPath = Join-Path $DriverPath "vnvme.inf"
    if (-not (Test-Path $infPath)) {
        Write-Host "Error: INF file not found: $infPath" -ForegroundColor Red
        return $false
    }
    
    Write-Host "Using INF: $infPath"
    
    # 使用 pnputil 安装驱动
    Write-Host "Adding driver package..."
    $result = & pnputil.exe /add-driver $infPath /install 2>&1
    Write-Host $result
    
    # 创建设备节点
    Write-Host "Creating device node..."
    $devconPath = "C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe"
    
    if (Test-Path $devconPath) {
        $result = & $devconPath install $infPath "ROOT\VNVME" 2>&1
        Write-Host $result
    } else {
        Write-Host "Warning: devcon.exe not found at $devconPath" -ForegroundColor Yellow
        Write-Host "Please install the device manually using Device Manager" -ForegroundColor Yellow
    }
    
    return $true
}

function Uninstall-VnvmeDriver {
    Write-TestHeader "Uninstalling VNVME Driver"
    
    if (-not (Test-AdminPrivileges)) {
        Write-Host "Error: Administrator privileges required" -ForegroundColor Red
        return $false
    }
    
    # 停止服务
    Write-Host "Stopping driver..."
    $service = Get-Service -Name "vnvme" -ErrorAction SilentlyContinue
    if ($service) {
        Stop-Service -Name "vnvme" -Force -ErrorAction SilentlyContinue
    }
    
    # 移除设备
    $devconPath = "C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe"
    if (Test-Path $devconPath) {
        Write-Host "Removing device..."
        $result = & $devconPath remove "ROOT\VNVME" 2>&1
        Write-Host $result
    }
    
    # 移除驱动包
    Write-Host "Removing driver package..."
    $packages = & pnputil.exe /enum-drivers 2>&1
    foreach ($line in $packages) {
        if ($line -match "vnvme\.inf") {
            $oemInf = ($packages[$packages.IndexOf($line) - 1] -split ":")[1].Trim()
            Write-Host "Removing $oemInf..."
            & pnputil.exe /delete-driver $oemInf /force 2>&1
        }
    }
    
    return $true
}

function Test-DriverLoad {
    Write-TestHeader "Driver Load Test"
    
    $passed = $true
    
    # 检查服务是否存在
    $service = Get-Service -Name "vnvme" -ErrorAction SilentlyContinue
    if ($service) {
        Write-TestResult $true "Driver service exists"
        Write-Host "  Service Status: $($service.Status)"
    } else {
        Write-TestResult $false "Driver service not found"
        $passed = $false
    }
    
    # 检查设备是否存在
    $device = Get-PnpDevice | Where-Object { $_.HardwareId -like "*VNVME*" }
    if ($device) {
        Write-TestResult $true "Device found in Device Manager"
        Write-Host "  Device Name: $($device.FriendlyName)"
        Write-Host "  Status: $($device.Status)"
    } else {
        Write-TestResult $false "Device not found in Device Manager"
        $passed = $false
    }
    
    return $passed
}

function Test-ControlDevice {
    Write-TestHeader "Control Device Test"
    
    $passed = $true
    $devicePath = "\\.\VNVMEControl"
    
    # 尝试打开控制设备
    try {
        $kernel32 = Add-Type -MemberDefinition @"
[DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
public static extern IntPtr CreateFile(
    string lpFileName,
    uint dwDesiredAccess,
    uint dwShareMode,
    IntPtr lpSecurityAttributes,
    uint dwCreationDisposition,
    uint dwFlagsAndAttributes,
    IntPtr hTemplateFile);

[DllImport("kernel32.dll", SetLastError=true)]
public static extern bool CloseHandle(IntPtr hObject);
"@ -Name "Kernel32" -Namespace "Win32" -PassThru
        
        $GENERIC_READ = 0x80000000
        $GENERIC_WRITE = 0x40000000
        $FILE_SHARE_READ = 0x1
        $FILE_SHARE_WRITE = 0x2
        $OPEN_EXISTING = 3
        $INVALID_HANDLE_VALUE = [IntPtr]::new(-1)
        
        $handle = [Win32.Kernel32]::CreateFile(
            $devicePath,
            $GENERIC_READ -bor $GENERIC_WRITE,
            $FILE_SHARE_READ -bor $FILE_SHARE_WRITE,
            [IntPtr]::Zero,
            $OPEN_EXISTING,
            0,
            [IntPtr]::Zero
        )
        
        if ($handle -eq $INVALID_HANDLE_VALUE) {
            $error = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
            Write-TestResult $false "Failed to open control device (Error: $error)"
            $passed = $false
        } else {
            Write-TestResult $true "Control device opened successfully"
            [Win32.Kernel32]::CloseHandle($handle) | Out-Null
        }
    }
    catch {
        Write-TestResult $false "Exception: $_"
        $passed = $false
    }
    
    return $passed
}

function Test-VnvmeCtl {
    Write-TestHeader "vnvmectl Test"
    
    $passed = $true
    $vnvmectlPath = Join-Path $DriverPath "..\vnvmectl.exe"
    
    if (-not (Test-Path $vnvmectlPath)) {
        Write-Host "Warning: vnvmectl.exe not found at $vnvmectlPath" -ForegroundColor Yellow
        Write-Host "Skipping vnvmectl tests" -ForegroundColor Yellow
        return $true
    }
    
    # 测试 version 命令
    Write-Host "Running: vnvmectl version"
    $result = & $vnvmectlPath version 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-TestResult $true "vnvmectl version succeeded"
        Write-Host "  $result"
    } else {
        Write-TestResult $false "vnvmectl version failed"
        $passed = $false
    }
    
    # 测试 status 命令
    Write-Host "Running: vnvmectl status"
    $result = & $vnvmectlPath status 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-TestResult $true "vnvmectl status succeeded"
        foreach ($line in $result) {
            Write-Host "  $line"
        }
    } else {
        Write-TestResult $false "vnvmectl status failed"
        $passed = $false
    }
    
    return $passed
}

function Run-AllTests {
    Write-Host ""
    Write-Host "=" * 60 -ForegroundColor Magenta
    Write-Host "VNVME Driver Test Suite" -ForegroundColor Magenta
    Write-Host "=" * 60 -ForegroundColor Magenta
    Write-Host ""
    
    $results = @{
        "Driver Load" = Test-DriverLoad
        "Control Device" = Test-ControlDevice
        "vnvmectl" = Test-VnvmeCtl
    }
    
    Write-Host ""
    Write-Host "=" * 60 -ForegroundColor Magenta
    Write-Host "Test Summary" -ForegroundColor Magenta
    Write-Host "=" * 60 -ForegroundColor Magenta
    
    $allPassed = $true
    foreach ($test in $results.Keys) {
        if ($results[$test]) {
            Write-Host "  [PASS] $test" -ForegroundColor Green
        } else {
            Write-Host "  [FAIL] $test" -ForegroundColor Red
            $allPassed = $false
        }
    }
    
    Write-Host ""
    if ($allPassed) {
        Write-Host "All tests PASSED!" -ForegroundColor Green
        return 0
    } else {
        Write-Host "Some tests FAILED!" -ForegroundColor Red
        return 1
    }
}

# 主逻辑
if ($Install) {
    Install-VnvmeDriver
}
elseif ($Uninstall) {
    Uninstall-VnvmeDriver
}
elseif ($Test -or $All) {
    $exitCode = Run-AllTests
    exit $exitCode
}
else {
    Write-Host "VNVME Driver Test Script"
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  .\test-driver.ps1 -Install      Install the driver"
    Write-Host "  .\test-driver.ps1 -Uninstall    Uninstall the driver"
    Write-Host "  .\test-driver.ps1 -Test         Run tests"
    Write-Host "  .\test-driver.ps1 -All          Run all tests"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  -DriverPath <path>  Path to driver directory (default: ..\x64\Debug\vnvme)"
}
