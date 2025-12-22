# vnvme 构建脚本
# 用法: .\scripts\build.ps1 [-Configuration Debug|Release] [-Platform x64|ARM64] [-Clean]

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    
    [ValidateSet("x64", "ARM64")]
    [string]$Platform = "x64",
    
    [switch]$Clean,
    
    [switch]$SignDrivers,
    
    [switch]$CreatePackage
)

$ErrorActionPreference = "Stop"

# 项目路径
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$SolutionPath = Join-Path $ProjectRoot "vnvme.sln"
$OutputDir = Join-Path $ProjectRoot "build\$Configuration\$Platform"
$DriversDir = Join-Path $OutputDir "drivers"
$BinDir = Join-Path $OutputDir "bin"

# 检查 WDK 和 Visual Studio
function Test-BuildEnvironment {
    Write-Host "检查构建环境..." -ForegroundColor Cyan
    
    # 检查 MSBuild
    $msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
        -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
    
    if (-not $msbuild) {
        throw "未找到 MSBuild，请安装 Visual Studio 2022"
    }
    
    # 检查 WDK
    $wdkPath = "C:\Program Files (x86)\Windows Kits\10"
    if (-not (Test-Path $wdkPath)) {
        throw "未找到 Windows Driver Kit，请安装 WDK"
    }
    
    Write-Host "  MSBuild: $msbuild" -ForegroundColor Green
    Write-Host "  WDK: $wdkPath" -ForegroundColor Green
    
    return $msbuild
}

# 清理
function Invoke-Clean {
    Write-Host "`n清理构建目录..." -ForegroundColor Cyan
    
    $dirsToClean = @(
        (Join-Path $ProjectRoot "build"),
        (Join-Path $ProjectRoot "vnvme\x64"),
        (Join-Path $ProjectRoot "vnvme\ARM64"),
        (Join-Path $ProjectRoot "vnvme-server\x64"),
        (Join-Path $ProjectRoot "vnvme-server\ARM64"),
        (Join-Path $ProjectRoot "vnvmectl\x64"),
        (Join-Path $ProjectRoot "vnvmectl\ARM64")
    )
    
    foreach ($dir in $dirsToClean) {
        if (Test-Path $dir) {
            Remove-Item -Path $dir -Recurse -Force
            Write-Host "  已删除: $dir" -ForegroundColor Yellow
        }
    }
}

# 构建
function Invoke-Build {
    param([string]$MSBuild)
    
    Write-Host "`n构建项目 [$Configuration|$Platform]..." -ForegroundColor Cyan
    
    # 确保输出目录存在
    New-Item -ItemType Directory -Path $DriversDir -Force | Out-Null
    New-Item -ItemType Directory -Path $BinDir -Force | Out-Null
    
    # 构建解决方案
    $buildArgs = @(
        $SolutionPath,
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/t:Build",
        "/m",
        "/v:minimal"
    )
    
    Write-Host "  执行: MSBuild $($buildArgs -join ' ')" -ForegroundColor Gray
    
    & $MSBuild @buildArgs
    
    if ($LASTEXITCODE -ne 0) {
        throw "构建失败，退出码: $LASTEXITCODE"
    }
    
    Write-Host "  构建成功!" -ForegroundColor Green
}

# 复制输出文件
function Copy-BuildOutputs {
    Write-Host "`n复制构建输出..." -ForegroundColor Cyan
    
    # 驱动文件
    $driverFiles = @(
        "vnvme\$Platform\$Configuration\vnvme.sys",
        "vnvme\$Platform\$Configuration\vnvme.pdb",
        "vnvme\$Platform\$Configuration\vnvme.inf"
    )
    
    foreach ($file in $driverFiles) {
        $srcPath = Join-Path $ProjectRoot $file
        if (Test-Path $srcPath) {
            Copy-Item $srcPath -Destination $DriversDir -Force
            Write-Host "  已复制: $file" -ForegroundColor Green
        }
    }
    
    # 用户态程序
    $userFiles = @(
        "vnvme-server\$Platform\$Configuration\vnvme-server.exe",
        "vnvme-server\$Platform\$Configuration\vnvme-server.pdb",
        "vnvmectl\$Platform\$Configuration\vnvmectl.exe",
        "vnvmectl\$Platform\$Configuration\vnvmectl.pdb"
    )
    
    foreach ($file in $userFiles) {
        $srcPath = Join-Path $ProjectRoot $file
        if (Test-Path $srcPath) {
            Copy-Item $srcPath -Destination $BinDir -Force
            Write-Host "  已复制: $file" -ForegroundColor Green
        }
    }
    
    # INF 模板
    Copy-Item (Join-Path $ProjectRoot "templates\*.inf") -Destination $DriversDir -Force
}

# 签名驱动
function Invoke-SignDrivers {
    Write-Host "`n签名驱动文件..." -ForegroundColor Cyan
    
    $certName = "VNVMETestCert"
    $certStore = "Cert:\CurrentUser\My"
    
    # 检查测试证书是否存在
    $cert = Get-ChildItem $certStore | Where-Object { $_.Subject -like "*$certName*" }
    
    if (-not $cert) {
        Write-Host "  创建测试证书..." -ForegroundColor Yellow
        $cert = New-SelfSignedCertificate `
            -Type CodeSigningCert `
            -Subject "CN=$certName" `
            -CertStoreLocation $certStore `
            -NotAfter (Get-Date).AddYears(3)
        
        # 导出到受信任根
        $certPath = Join-Path $env:TEMP "$certName.cer"
        Export-Certificate -Cert $cert -FilePath $certPath -Force | Out-Null
        Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\TrustedPublisher" | Out-Null
        Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\Root" | Out-Null
        Remove-Item $certPath
    }
    
    # 签名 .sys 文件
    $sysFile = Join-Path $DriversDir "vnvme.sys"
    if (Test-Path $sysFile) {
        $signtool = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe"
        
        & $signtool sign /s My /n $certName /t http://timestamp.digicert.com /fd SHA256 $sysFile
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  驱动已签名: vnvme.sys" -ForegroundColor Green
        } else {
            Write-Warning "  签名失败，请检查证书配置"
        }
    }
}

# 创建安装包
function New-DriverPackage {
    Write-Host "`n创建驱动包..." -ForegroundColor Cyan
    
    $packageDir = Join-Path $OutputDir "package"
    New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
    
    # 复制所需文件
    Copy-Item (Join-Path $DriversDir "*") -Destination $packageDir -Force
    Copy-Item (Join-Path $BinDir "*") -Destination $packageDir -Force
    Copy-Item (Join-Path $PSScriptRoot "install.ps1") -Destination $packageDir -Force
    Copy-Item (Join-Path $PSScriptRoot "uninstall.ps1") -Destination $packageDir -Force
    
    # 创建 CAT 文件
    $inf2cat = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x86\inf2cat.exe"
    if (Test-Path $inf2cat) {
        & $inf2cat /driver:$packageDir /os:10_$Platform /verbose
    }
    
    # 创建 ZIP
    $zipPath = Join-Path $OutputDir "vnvme-$Configuration-$Platform.zip"
    Compress-Archive -Path "$packageDir\*" -DestinationPath $zipPath -Force
    
    Write-Host "  驱动包已创建: $zipPath" -ForegroundColor Green
}

# 主流程
try {
    Write-Host "========================================" -ForegroundColor White
    Write-Host "  VNVME 构建脚本" -ForegroundColor White
    Write-Host "========================================" -ForegroundColor White
    Write-Host "  配置: $Configuration" -ForegroundColor White
    Write-Host "  平台: $Platform" -ForegroundColor White
    Write-Host "----------------------------------------" -ForegroundColor White
    
    $msbuild = Test-BuildEnvironment
    
    if ($Clean) {
        Invoke-Clean
    }
    
    Invoke-Build -MSBuild $msbuild
    Copy-BuildOutputs
    
    if ($SignDrivers) {
        Invoke-SignDrivers
    }
    
    if ($CreatePackage) {
        New-DriverPackage
    }
    
    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "  构建完成!" -ForegroundColor Green
    Write-Host "  输出目录: $OutputDir" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
}
catch {
    Write-Host "`n错误: $_" -ForegroundColor Red
    exit 1
}
