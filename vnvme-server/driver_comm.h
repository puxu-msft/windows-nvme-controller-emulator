/**
 * @file driver_comm.h
 * @brief 驱动通信模块接口
 */

#ifndef _VNVME_DRIVER_COMM_H_
#define _VNVME_DRIVER_COMM_H_

#include "types.h"
#include "../include/vnvme_common.h"
#include "../include/vnvme_ioctl.h"

//===========================================================================
// 共享内存上下文
//===========================================================================

typedef struct _SHM_CONTEXT {
    PVOID                           userAddress;        // 用户态映射基地址
    SIZE_T                          size;               // 总大小
    
    // 控制块
    PVNVME_SHM_CONTROL_BLOCK        controlBlock;
    
    // 通知环
    PVNVME_NOTIFY_RING              notifyRing;
    
    // 完成通知环
    PVNVME_COMPLETION_NOTIFY_RING   completionRing;
    
    // 数据缓冲区
    PVOID                           dataBuffer;
    SIZE_T                          dataBufferSize;
} SHM_CONTEXT, *PSHM_CONTEXT;

//===========================================================================
// 驱动上下文
//===========================================================================

typedef struct _DRIVER_COMM_CONTEXT {
    HANDLE              deviceHandle;       // 驱动设备句柄
    UINT32              driverVersion;      // 驱动版本
    SHM_CONTEXT         shm;                // 共享内存
    HANDLE              heartbeatThread;    // 心跳线程
    volatile BOOL       running;            // 运行状态
    UINT32              heartbeatIntervalMs;// 心跳间隔
    UINT64              commandsProcessed;  // 处理命令计数
} DRIVER_COMM_CONTEXT, *PDRIVER_COMM_CONTEXT;

//===========================================================================
// 函数声明
//===========================================================================

/**
 * 连接到驱动
 * 
 * @param pCtx 驱动上下文
 * @return TRUE 成功，FALSE 失败
 */
BOOL DriverConnect(PDRIVER_COMM_CONTEXT pCtx);

/**
 * 断开驱动连接
 * 
 * @param pCtx 驱动上下文
 */
void DriverDisconnect(PDRIVER_COMM_CONTEXT pCtx);

/**
 * 映射共享内存
 * 
 * @param pCtx 驱动上下文
 * @return TRUE 成功，FALSE 失败
 */
BOOL DriverMapSharedMemory(PDRIVER_COMM_CONTEXT pCtx);

/**
 * 取消映射共享内存
 * 
 * @param pCtx 驱动上下文
 */
void DriverUnmapSharedMemory(PDRIVER_COMM_CONTEXT pCtx);

/**
 * 发送用户态就绪通知
 * 
 * @param pCtx 驱动上下文
 * @return TRUE 成功，FALSE 失败
 */
BOOL DriverSendUserReady(PDRIVER_COMM_CONTEXT pCtx);

/**
 * 发送心跳
 * 
 * @param pCtx 驱动上下文
 * @return TRUE 成功，FALSE 失败
 */
BOOL DriverSendHeartbeat(PDRIVER_COMM_CONTEXT pCtx);

/**
 * 启动心跳线程
 * 
 * @param pCtx 驱动上下文
 * @param intervalMs 心跳间隔 (毫秒)
 * @return TRUE 成功，FALSE 失败
 */
BOOL DriverStartHeartbeat(PDRIVER_COMM_CONTEXT pCtx, UINT32 intervalMs);

/**
 * 停止心跳线程
 * 
 * @param pCtx 驱动上下文
 */
void DriverStopHeartbeat(PDRIVER_COMM_CONTEXT pCtx);

/**
 * 检查是否请求关闭
 * 
 * @param pCtx 驱动上下文
 * @return TRUE 应该关闭，FALSE 继续运行
 */
BOOL DriverIsShutdownRequested(PDRIVER_COMM_CONTEXT pCtx);

/**
 * 通知关闭完成
 * 
 * @param pCtx 驱动上下文
 */
void DriverNotifyShutdownComplete(PDRIVER_COMM_CONTEXT pCtx);

/**
 * 获取驱动版本
 * 
 * @param pCtx 驱动上下文
 * @param pVersion 输出版本号
 * @return TRUE 成功，FALSE 失败
 */
BOOL DriverGetVersion(PDRIVER_COMM_CONTEXT pCtx, UINT32* pVersion);

/**
 * 获取驱动状态
 * 
 * @param pCtx 驱动上下文
 * @param pStatus 输出状态结构
 * @return TRUE 成功，FALSE 失败
 */
BOOL DriverGetStatus(PDRIVER_COMM_CONTEXT pCtx, PVNVME_DRIVER_STATUS pStatus);

#endif /* _VNVME_DRIVER_COMM_H_ */
