/**
 * @file vnvme_utils.h
 * @brief VNVME 公共工具函数和宏
 * 
 * 提供内核驱动中常用的内联函数和验证宏，
 * 减少代码重复，统一编码风格。
 */

#ifndef _VNVME_UTILS_H_
#define _VNVME_UTILS_H_

#include "vnvme_common.h"
#include "nvme_spec.h"

//===========================================================================
// NVMe 状态构造
//===========================================================================

/**
 * @brief 构造 NVMe 完成状态字段
 * 
 * Status 字段格式: [15:15] DNR, [14:14] M, [11:9] SCT, [8:1] SC, [0:0] P
 * 
 * @param sct Status Code Type (状态码类型)
 * @param sc Status Code (状态码)
 * @param phase Phase Tag (相位标签)
 * @return 构造的 16 位状态值
 */
static __inline UINT16 NvmeMakeStatus(UINT8 sct, UINT8 sc, UINT8 phase)
{
    return (UINT16)(phase | (sc << 1) | (sct << 9));
}

/**
 * @brief 构造成功状态
 */
static __inline UINT16 NvmeMakeSuccessStatus(UINT8 phase)
{
    return NvmeMakeStatus(NVME_SCT_GENERIC, NVME_SC_SUCCESS, phase);
}

//===========================================================================
// 命名空间验证
//===========================================================================

/**
 * @brief 验证命名空间 ID 是否在有效范围内
 * 
 * @param nsid 命名空间 ID (1-based)
 * @return TRUE 如果有效，FALSE 如果无效
 */
static __inline BOOLEAN NvmeValidateNsid(ULONG nsid)
{
    return (nsid > 0 && nsid <= VNVME_MAX_NAMESPACES);
}

/**
 * @brief 验证命名空间 ID 范围宏
 * 
 * 用于快速验证 NSID 是否在 [1, VNVME_MAX_NAMESPACES] 范围内
 */
#define VNVME_NSID_VALID(nsid) \
    ((nsid) > 0 && (nsid) <= VNVME_MAX_NAMESPACES)

/**
 * @brief 命名空间 ID 转数组索引
 * 
 * NSID 是 1-based，数组索引是 0-based
 */
#define VNVME_NSID_TO_INDEX(nsid) ((nsid) - 1)

//===========================================================================
// 队列 ID 验证
//===========================================================================

/**
 * @brief 验证 I/O 队列 ID 是否在有效范围内
 * 
 * @param qid 队列 ID (1-based for I/O queues)
 * @return TRUE 如果有效，FALSE 如果无效
 */
static __inline BOOLEAN NvmeValidateIoQueueId(USHORT qid)
{
    return (qid > 0 && qid <= VNVME_MAX_IO_QUEUES);
}

/**
 * @brief 验证 I/O 队列 ID 宏
 */
#define VNVME_IO_QUEUE_ID_VALID(qid) \
    ((qid) > 0 && (qid) <= VNVME_MAX_IO_QUEUES)

/**
 * @brief 队列 ID 转数组索引
 */
#define VNVME_QUEUE_ID_TO_INDEX(qid) ((qid) - 1)

//===========================================================================
// LBA 范围验证
//===========================================================================

/**
 * @brief 验证 LBA 范围是否有效
 * 
 * @param slba 起始 LBA
 * @param nlb 块数 (1-based count)
 * @param totalBlocks 总块数
 * @return TRUE 如果有效，FALSE 如果越界
 */
static __inline BOOLEAN NvmeValidateLbaRange(
    ULONGLONG slba,
    USHORT nlb,
    ULONGLONG totalBlocks
)
{
    return (slba + nlb <= totalBlocks);
}

#define VNVME_LBA_RANGE_VALID(slba, nlb, total) \
    ((slba) + (nlb) <= (total))

//===========================================================================
// 字节/块转换
//===========================================================================

/**
 * @brief LBA 转字节偏移
 */
#define VNVME_LBA_TO_BYTES(lba, blockSize) \
    ((ULONGLONG)(lba) * (blockSize))

/**
 * @brief 块数转字节数
 */
#define VNVME_BLOCKS_TO_BYTES(nlb, blockSize) \
    ((ULONGLONG)(nlb) * (blockSize))

//===========================================================================
// 安全边界检查
//===========================================================================

/**
 * @brief 安全的数组索引检查
 * 
 * 防止在访问固定大小数组前越界
 */
#define VNVME_ARRAY_INDEX_VALID(index, arraySize) \
    ((index) < (arraySize))

/**
 * @brief 安全的偏移+长度检查
 * 
 * 防止整数溢出
 */
static __inline BOOLEAN NvmeSafeRangeCheck(
    ULONGLONG offset,
    ULONG length,
    ULONGLONG totalSize
)
{
    // 检查加法溢出
    if (offset > totalSize) return FALSE;
    if (length > totalSize - offset) return FALSE;
    return TRUE;
}

#endif /* _VNVME_UTILS_H_ */
