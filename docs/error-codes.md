# NVMe 错误码参考

本文档提供完整的 NVMe 状态码 (Status Code) 定义，基于 NVMe 1.4 规范。

## 状态字段格式

完成队列条目 (CQE) 中的状态字段 (Offset 0x0E, 16-bit):

```
Bits    Field   Description
[0]     P       阶段位 (Phase Tag)
[8:1]   SC      状态码 (Status Code)
[11:9]  SCT     状态码类型 (Status Code Type)
[13:12] -       保留
[14]    M       更多 (More) - 还有更多状态
[15]    DNR     不要重试 (Do Not Retry)
```

## 状态码类型 (SCT)

| 值 | 类型 | 说明 |
|----|------|------|
| 0 | Generic Command Status | 通用命令状态 |
| 1 | Command Specific Status | 命令特定状态 |
| 2 | Media and Data Integrity Errors | 介质和数据完整性错误 |
| 3 | Path Related Status | 路径相关状态 |
| 4-6 | Reserved | 保留 |
| 7 | Vendor Specific | 厂商特定 |

---

## 通用命令状态 (SCT = 0x0)

### 成功状态

| SC | 名称 | 说明 | C 定义 |
|----|------|------|--------|
| 0x00 | Successful Completion | 命令成功完成 | `NVME_SC_SUCCESS` |

### 通用错误

| SC | 名称 | 说明 | C 定义 |
|----|------|------|--------|
| 0x01 | Invalid Command Opcode | 无效的命令操作码 | `NVME_SC_INVALID_OPCODE` |
| 0x02 | Invalid Field in Command | 命令中存在无效字段 | `NVME_SC_INVALID_FIELD` |
| 0x03 | Command ID Conflict | 命令 ID 冲突 | `NVME_SC_CID_CONFLICT` |
| 0x04 | Data Transfer Error | 数据传输错误 | `NVME_SC_DATA_XFER_ERROR` |
| 0x05 | Commands Aborted due to Power Loss | 因断电导致命令中止 | `NVME_SC_POWER_LOSS` |
| 0x06 | Internal Error | 内部错误 | `NVME_SC_INTERNAL` |
| 0x07 | Command Abort Requested | 命令中止请求 | `NVME_SC_ABORT_REQ` |
| 0x08 | Command Aborted due to SQ Deletion | 因 SQ 删除导致命令中止 | `NVME_SC_ABORT_SQ_DEL` |
| 0x09 | Command Aborted due to Failed Fused | 因融合命令失败导致中止 | `NVME_SC_ABORT_FUSED_FAIL` |
| 0x0A | Command Aborted due to Missing Fused | 因缺少融合命令导致中止 | `NVME_SC_ABORT_FUSED_MISSING` |
| 0x0B | Invalid Namespace or Format | 无效的命名空间或格式 | `NVME_SC_INVALID_NS` |
| 0x0C | Command Sequence Error | 命令序列错误 | `NVME_SC_CMD_SEQ_ERROR` |
| 0x0D | Invalid SGL Segment Descriptor | 无效的 SGL 段描述符 | `NVME_SC_INVALID_SGL_SEG` |
| 0x0E | Invalid Number of SGL Descriptors | SGL 描述符数量无效 | `NVME_SC_INVALID_SGL_COUNT` |
| 0x0F | Data SGL Length Invalid | 数据 SGL 长度无效 | `NVME_SC_INVALID_SGL_LEN` |
| 0x10 | Metadata SGL Length Invalid | 元数据 SGL 长度无效 | `NVME_SC_INVALID_MDATA_SGL_LEN` |
| 0x11 | SGL Descriptor Type Invalid | SGL 描述符类型无效 | `NVME_SC_INVALID_SGL_TYPE` |
| 0x12 | Invalid Use of CMB | 无效的 CMB 使用 | `NVME_SC_INVALID_CMB_USE` |
| 0x13 | Invalid PRP Offset | 无效的 PRP 偏移 | `NVME_SC_INVALID_PRP_OFFSET` |
| 0x14 | Atomic Write Unit Exceeded | 超出原子写入单元 | `NVME_SC_AWU_EXCEEDED` |
| 0x15 | Operation Denied | 操作被拒绝 | `NVME_SC_OP_DENIED` |
| 0x16 | SGL Offset Invalid | SGL 偏移无效 | `NVME_SC_INVALID_SGL_OFFSET` |
| 0x18 | Host Identifier Inconsistent Format | 主机标识符格式不一致 | `NVME_SC_HOSTID_FORMAT` |
| 0x19 | Keep Alive Timer Expired | 保活定时器过期 | `NVME_SC_KA_EXPIRED` |
| 0x1A | Keep Alive Timeout Invalid | 保活超时无效 | `NVME_SC_KA_TIMEOUT_INVALID` |
| 0x1B | Command Aborted due to Preempt | 因抢占导致命令中止 | `NVME_SC_ABORT_PREEMPT` |
| 0x1C | Sanitize Failed | 清除操作失败 | `NVME_SC_SANITIZE_FAILED` |
| 0x1D | Sanitize In Progress | 清除操作进行中 | `NVME_SC_SANITIZE_IN_PROGRESS` |

### LBA 相关错误

| SC | 名称 | 说明 | C 定义 |
|----|------|------|--------|
| 0x80 | LBA Out of Range | LBA 超出范围 | `NVME_SC_LBA_RANGE` |
| 0x81 | Capacity Exceeded | 容量超出 | `NVME_SC_CAP_EXCEEDED` |
| 0x82 | Namespace Not Ready | 命名空间未就绪 | `NVME_SC_NS_NOT_READY` |
| 0x83 | Reservation Conflict | 预留冲突 | `NVME_SC_RESV_CONFLICT` |
| 0x84 | Format In Progress | 格式化进行中 | `NVME_SC_FORMAT_IN_PROGRESS` |

---

## 命令特定状态 (SCT = 0x1)

### Admin 命令特定

| SC | 名称 | 说明 | C 定义 |
|----|------|------|--------|
| 0x00 | Completion Queue Invalid | 完成队列无效 | `NVME_SC_CQ_INVALID` |
| 0x01 | Invalid Queue Identifier | 队列标识符无效 | `NVME_SC_QID_INVALID` |
| 0x02 | Invalid Queue Size | 队列大小无效 | `NVME_SC_QUEUE_SIZE` |
| 0x03 | Abort Command Limit Exceeded | 中止命令限制超出 | `NVME_SC_ABORT_LIMIT` |
| 0x05 | Async Event Request Limit Exceeded | 异步事件请求限制超出 | `NVME_SC_AER_LIMIT` |
| 0x06 | Invalid Firmware Slot | 固件槽位无效 | `NVME_SC_INVALID_FW_SLOT` |
| 0x07 | Invalid Firmware Image | 固件映像无效 | `NVME_SC_INVALID_FW_IMAGE` |
| 0x08 | Invalid Interrupt Vector | 中断向量无效 | `NVME_SC_INVALID_INT_VECTOR` |
| 0x09 | Invalid Log Page | 日志页无效 | `NVME_SC_INVALID_LOG_PAGE` |
| 0x0A | Invalid Format | 格式无效 | `NVME_SC_INVALID_FORMAT` |
| 0x0B | Firmware Activation Requires Reset | 固件激活需要重置 | `NVME_SC_FW_NEEDS_RESET` |
| 0x0C | Invalid Queue Deletion | 无效的队列删除 | `NVME_SC_INVALID_QUEUE_DEL` |
| 0x0D | Feature Identifier Not Saveable | 特性标识符不可保存 | `NVME_SC_FID_NOT_SAVEABLE` |
| 0x0E | Feature Not Changeable | 特性不可更改 | `NVME_SC_FID_NOT_CHANGEABLE` |
| 0x0F | Feature Not Namespace Specific | 特性非命名空间特定 | `NVME_SC_FID_NOT_NS_SPECIFIC` |
| 0x10 | Firmware Activation Requires Subsystem Reset | 固件激活需要子系统重置 | `NVME_SC_FW_NEEDS_SUBSYS_RESET` |

### I/O 命令特定

| SC | 名称 | 说明 | C 定义 |
|----|------|------|--------|
| 0x80 | Conflicting Attributes | 属性冲突 | `NVME_SC_CONFLICTING_ATTRS` |
| 0x81 | Invalid Protection Information | 保护信息无效 | `NVME_SC_INVALID_PI` |
| 0x82 | Attempted Write to Read Only Range | 尝试写入只读范围 | `NVME_SC_WRITE_RO` |

---

## 介质和数据完整性错误 (SCT = 0x2)

| SC | 名称 | 说明 | C 定义 |
|----|------|------|--------|
| 0x80 | Write Fault | 写入故障 | `NVME_SC_WRITE_FAULT` |
| 0x81 | Unrecovered Read Error | 不可恢复的读取错误 | `NVME_SC_READ_ERROR` |
| 0x82 | End-to-end Guard Check Error | 端到端保护检查错误 | `NVME_SC_GUARD_CHECK` |
| 0x83 | End-to-end Application Tag Check Error | 应用标签检查错误 | `NVME_SC_APPTAG_CHECK` |
| 0x84 | End-to-end Reference Tag Check Error | 引用标签检查错误 | `NVME_SC_REFTAG_CHECK` |
| 0x85 | Compare Failure | 比较失败 | `NVME_SC_COMPARE_FAILED` |
| 0x86 | Access Denied | 访问被拒绝 | `NVME_SC_ACCESS_DENIED` |
| 0x87 | Deallocated or Unwritten Logical Block | 已释放或未写入的逻辑块 | `NVME_SC_UNWRITTEN_BLOCK` |

---

## C 语言定义

```c
// NVMe 状态码定义
typedef enum _NVME_STATUS_CODE {
    // Generic Command Status (SCT = 0)
    NVME_SC_SUCCESS                 = 0x0000,
    NVME_SC_INVALID_OPCODE          = 0x0001,
    NVME_SC_INVALID_FIELD           = 0x0002,
    NVME_SC_CID_CONFLICT            = 0x0003,
    NVME_SC_DATA_XFER_ERROR         = 0x0004,
    NVME_SC_POWER_LOSS              = 0x0005,
    NVME_SC_INTERNAL                = 0x0006,
    NVME_SC_ABORT_REQ               = 0x0007,
    NVME_SC_ABORT_SQ_DEL            = 0x0008,
    NVME_SC_LBA_RANGE               = 0x0080,
    NVME_SC_CAP_EXCEEDED            = 0x0081,
    NVME_SC_NS_NOT_READY            = 0x0082,
    
    // Command Specific Status (SCT = 1)
    NVME_SC_CQ_INVALID              = 0x0100,
    NVME_SC_QID_INVALID             = 0x0101,
    NVME_SC_QUEUE_SIZE              = 0x0102,
    NVME_SC_ABORT_LIMIT             = 0x0103,
    NVME_SC_INVALID_INT_VECTOR      = 0x0108,
    NVME_SC_INVALID_LOG_PAGE        = 0x0109,
    NVME_SC_INVALID_FORMAT          = 0x010A,
    NVME_SC_INVALID_QUEUE_DEL       = 0x010C,
    
    // Media Errors (SCT = 2)
    NVME_SC_WRITE_FAULT             = 0x0280,
    NVME_SC_READ_ERROR              = 0x0281,
    NVME_SC_COMPARE_FAILED          = 0x0285,
} NVME_STATUS_CODE;

// 构造完整状态字段
#define NVME_STATUS(sct, sc, dnr) \
    (((dnr) << 15) | ((sct) << 9) | ((sc) << 1))

// 解析状态字段
#define NVME_STATUS_GET_SC(status)  (((status) >> 1) & 0xFF)
#define NVME_STATUS_GET_SCT(status) (((status) >> 9) & 0x7)
#define NVME_STATUS_GET_DNR(status) (((status) >> 15) & 0x1)
#define NVME_STATUS_IS_ERROR(status) (NVME_STATUS_GET_SC(status) != 0)
```

---

## 错误处理最佳实践

### 可重试错误
以下错误可以通过重试解决 (DNR = 0):
- `NVME_SC_NS_NOT_READY` - 等待后重试
- `NVME_SC_ABORT_REQ` - 重新提交命令

### 不可重试错误
以下错误不应重试 (DNR = 1):
- `NVME_SC_INVALID_OPCODE` - 命令不支持
- `NVME_SC_INVALID_FIELD` - 命令参数错误
- `NVME_SC_LBA_RANGE` - 地址超出范围

### 错误日志记录
```c
VOID LogNvmeError(UINT16 Status, UINT16 CID) {
    UINT8 sc = NVME_STATUS_GET_SC(Status);
    UINT8 sct = NVME_STATUS_GET_SCT(Status);
    BOOLEAN dnr = NVME_STATUS_GET_DNR(Status);
    
    DbgPrint("NVMe Error: CID=%04X SCT=%d SC=0x%02X DNR=%d\n",
             CID, sct, sc, dnr);
}
```
