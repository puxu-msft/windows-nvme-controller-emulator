# 驱动详细设计

## 模块划分

### 1. vnvme_driver.c - 驱动入口
```
主要函数:
- DriverEntry()          驱动加载入口
- VNvmeEvtDriverUnload() 驱动卸载
- VNvmeEvtDeviceAdd()    设备添加回调
```

### 2. vnvme_controller.c - 控制器模拟
```
主要函数:
- ControllerInitialize()    初始化控制器
- ControllerReset()         重置控制器
- ControllerEnable()        启用控制器
- ControllerDisable()       禁用控制器
- ControllerGetCapabilities() 获取能力
```

### 3. vnvme_admin.c - Admin 命令处理
```
主要函数:
- AdminQueueProcess()       处理 Admin 队列
- AdminIdentify()           处理 Identify 命令
- AdminCreateIoCq()         创建 I/O 完成队列
- AdminCreateIoSq()         创建 I/O 提交队列
- AdminDeleteIoCq()         删除 I/O 完成队列
- AdminDeleteIoSq()         删除 I/O 提交队列
- AdminGetFeatures()        获取特性
- AdminSetFeatures()        设置特性
```

### 4. vnvme_io.c - I/O 命令处理
```
主要函数:
- IoQueueProcess()          处理 I/O 队列
- IoRead()                  读取命令
- IoWrite()                 写入命令
- IoFlush()                 刷新命令
```

### 5. vnvme_namespace.c - 命名空间管理
```
主要函数:
- NamespaceCreate()         创建命名空间
- NamespaceDestroy()        销毁命名空间
- NamespaceGetInfo()        获取命名空间信息
```

### 6. vnvme_backend.c - 存储后端
```
主要函数:
- BackendInitialize()       初始化后端
- BackendRead()             读取数据
- BackendWrite()            写入数据
- BackendFlush()            刷新数据
- BackendShutdown()         关闭后端
```

## 文件结构
```
virtual-nvme-driver/
├── docs/                   # 文档目录
├── src/
│   ├── driver/
│   │   ├── vnvme_driver.c
│   │   ├── vnvme_driver.h
│   │   ├── vnvme_controller.c
│   │   ├── vnvme_controller.h
│   │   ├── vnvme_admin.c
│   │   ├── vnvme_admin.h
│   │   ├── vnvme_io.c
│   │   ├── vnvme_io.h
│   │   ├── vnvme_namespace.c
│   │   ├── vnvme_namespace.h
│   │   ├── vnvme_backend.c
│   │   ├── vnvme_backend.h
│   │   ├── vnvme_queue.c
│   │   ├── vnvme_queue.h
│   │   └── vnvme_common.h
│   └── include/
│       └── nvme_spec.h     # NVMe 规范定义
├── inf/
│   └── vnvme.inf           # 驱动安装信息
├── test/                   # 测试代码
└── tools/                  # 辅助工具
```

## 状态机

### 控制器状态
```
DISABLED ──[CC.EN=1]──> ENABLING ──[ready]──> ENABLED
    ^                                            │
    └──────────[CC.EN=0]─────────────────────────┘
```
