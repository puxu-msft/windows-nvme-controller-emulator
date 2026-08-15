# VNVME Unit Test Project

本目录包含基于 wdutf (Windows Driver Unit Test Framework) 的 VNVME 驱动单元测试。

## 前置条件

1. **wdutf 框架**: 需要安装并构建 wdutf 框架
   - 默认位置: `Q:\src\wdutf`
   - 设置环境变量: `WDUTF_PATH=Q:\src\wdutf`

2. **Visual Studio**: 需要安装以下组件
   - Desktop development with C++
   - Windows application development
   - MSVC v143 build tools
   - Windows SDK (10.0.26100.1 或更高)
   - Windows Driver Kit (10.0.26100.1 或更高)

3. **构建 wdutf**:
   ```powershell
   cd Q:\src\wdutf
   # 打开 WDUTF.sln 并构建
   ```

## 项目结构

```
vnvme-unit-test/
├── stdafx.h            # 预编译头文件
├── stdafx.cpp          # 预编译头源文件
├── targetver.h         # Windows SDK 版本
├── TestHelpers.h       # 测试辅助函数和宏
├── Test.cpp            # 测试模块初始化
├── ConfigTest.cpp      # 配置模块测试
├── ControllerTest.cpp  # 控制器管理测试
├── NamespaceTest.cpp   # 命名空间管理测试
├── IoctlTest.cpp       # IOCTL 接口测试
├── vnvme.def           # 驱动导出符号定义
└── vnvme-unit-test.vcxproj  # 项目文件
```

## 测试类

| 测试类 | 描述 |
|--------|------|
| `VNVMEDriverLoadTest` | 驱动加载和控制设备创建测试 |
| `ConfigurationTest` | 配置模块功能测试 |
| `IoctlTest` | IOCTL 接口验证测试 |
| `ControllerTest` | NVMe 控制器管理测试 |
| `NamespaceTest` | NVMe 命名空间管理测试 |

## 构建

1. 设置环境变量:
   ```powershell
   $env:WDUTF_PATH = "Q:\src\wdutf"
   ```

2. 打开 `vnvme.sln` 解决方案

3. 确保先构建 wdutf 框架

4. 构建 vnvme-unit-test 项目

## 运行测试

### 使用 Visual Studio Test Explorer

1. 打开 **Test** → **Test Explorer**
2. 点击 **Run All** 运行所有测试

### 使用命令行

```powershell
vstest.console.exe build\x64\Debug\test\vnvme-unit-test.dll
```

## 添加新测试

1. 创建新的测试文件 (如 `NewFeatureTest.cpp`)
2. 添加到项目文件
3. 使用标准 MS Test 宏:

```cpp
#include "stdafx.h"

namespace VNVMEUnitTest
{
    TEST_CLASS(NewFeatureTest)
    {
        TEST_METHOD_INITIALIZE(TestInit)
        {
            DdkThreadInit();
        }

        TEST_METHOD(TestSomething)
        {
            Assert::IsTrue(condition, L"Error message");
        }
    };
}
```

## 已知限制

- wdutf 的 WDF 支持目前是 "minimal"，部分 API 可能需要扩展
- 参见 [wdutf-integration-analysis.md](../../docs/wdutf-integration-analysis.md) 了解详情

## 待扩展的 WDF API

以下 API 需要在 wdutf 中实现才能完全支持 vnvme 测试:

- `WdfDeviceCreateSymbolicLink`
- `WdfDeviceInitAssignName`
- `WdfDeviceInitFree`
- `WdfDeviceInitSetDeviceType`
- `WdfDeviceInitSetExclusive`
- `WdfInterruptQueueDpcForIsr`
- `WdfPdoInitAddDeviceText`
- `WdfPdoInitSetDefaultLocale`
- `WdfPdoInitSetEventCallbacks`
- `WdfPdoMarkMissing`
- `WdfTimerGetParentObject`
