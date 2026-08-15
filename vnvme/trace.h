/**
 * @file trace.h
 * @brief WPP 跟踪定义
 */

#ifndef _TRACE_H_
#define _TRACE_H_

//
// begin_wpp config
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
// end_wpp
//

/*
 * WPP 跟踪控制 GUID
 * {12345678-ABCD-EF01-2345-6789ABCDEF01}
 */
#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID( \
        VnvmeTraceGuid, (12345678,ABCD,EF01,23,45,67,89,AB,CD,EF,01), \
        WPP_DEFINE_BIT(TRACE_DRIVER)     \
        WPP_DEFINE_BIT(TRACE_DEVICE)     \
        WPP_DEFINE_BIT(TRACE_QUEUE)      \
        WPP_DEFINE_BIT(TRACE_IOCTL)      \
        WPP_DEFINE_BIT(TRACE_PNP)        \
        WPP_DEFINE_BIT(TRACE_POLL)       \
    )

#define WPP_FLAG_LEVEL_LOGGER(flag, level) \
    WPP_LEVEL_LOGGER(flag)

#define WPP_FLAG_LEVEL_ENABLED(flag, level) \
    (WPP_LEVEL_ENABLED(flag) && WPP_CONTROL(WPP_BIT_ ## flag).Level >= level)

/*
 * 跟踪级别
 */
#define TRACE_LEVEL_CRITICAL    1
#define TRACE_LEVEL_ERROR       2
#define TRACE_LEVEL_WARNING     3
#define TRACE_LEVEL_INFORMATION 4
#define TRACE_LEVEL_VERBOSE     5

/*
 * 如果 WPP 未启用，提供空实现
 */
#ifndef WPP_INIT_TRACING

#define WPP_INIT_TRACING(DriverObject, RegistryPath)
#define WPP_CLEANUP(DriverObject)

#define TraceEvents(level, flags, msg, ...) \
    do { \
        UNREFERENCED_PARAMETER(level); \
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "VNVME: " msg "\n", ##__VA_ARGS__); \
    } while(0)

#endif /* WPP_INIT_TRACING */

/*
 * 模块化跟踪宏
 * 
 * 使用方式: TRACE_MOD_INFO(TRACE_QUEUE, "Queue %d created", qid)
 * 
 * 这些宏允许按模块过滤跟踪输出。在 WPP 启用时，可通过 tracelog 工具
 * 选择性启用特定模块的跟踪；在 WPP 未启用时，回退到 DbgPrint。
 */
#define TRACE_MOD_ERROR(flag, msg, ...)   TraceEvents(TRACE_LEVEL_ERROR, flag, msg, ##__VA_ARGS__)
#define TRACE_MOD_WARN(flag, msg, ...)    TraceEvents(TRACE_LEVEL_WARNING, flag, msg, ##__VA_ARGS__)
#define TRACE_MOD_INFO(flag, msg, ...)    TraceEvents(TRACE_LEVEL_INFORMATION, flag, msg, ##__VA_ARGS__)
#define TRACE_MOD_DEBUG(flag, msg, ...)   TraceEvents(TRACE_LEVEL_VERBOSE, flag, msg, ##__VA_ARGS__)
#define TRACE_MOD_VERBOSE(flag, msg, ...) TraceEvents(TRACE_LEVEL_VERBOSE, flag, msg, ##__VA_ARGS__)

/*
 * 便捷宏 (默认使用 TRACE_DRIVER 标志)
 */
#define TRACE_ERROR(msg, ...)   TRACE_MOD_ERROR(TRACE_DRIVER, msg, ##__VA_ARGS__)
#define TRACE_WARN(msg, ...)    TRACE_MOD_WARN(TRACE_DRIVER, msg, ##__VA_ARGS__)
#define TRACE_INFO(msg, ...)    TRACE_MOD_INFO(TRACE_DRIVER, msg, ##__VA_ARGS__)
#define TRACE_DEBUG(msg, ...)   TRACE_MOD_DEBUG(TRACE_DRIVER, msg, ##__VA_ARGS__)
#define TRACE_VERBOSE(msg, ...) TRACE_MOD_VERBOSE(TRACE_DRIVER, msg, ##__VA_ARGS__)

/*
 * 函数跟踪宏 (用于调试函数入口/出口)
 */
#if DBG

#define TRACE_FUNC_ENTER() \
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DRIVER, ">>> %s", __FUNCTION__)

#define TRACE_FUNC_EXIT() \
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DRIVER, "<<< %s", __FUNCTION__)

#define TRACE_FUNC_EXIT_STATUS(status) \
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DRIVER, "<<< %s (0x%08X)", __FUNCTION__, (status))

#define TRACE_FUNC_EXIT_NTSTATUS(status) \
    do { \
        NTSTATUS _s = (status); \
        if (NT_SUCCESS(_s)) { \
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DRIVER, "<<< %s SUCCESS", __FUNCTION__); \
        } else { \
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "<<< %s FAILED (0x%08X)", __FUNCTION__, _s); \
        } \
    } while(0)

#else

#define TRACE_FUNC_ENTER()                  ((void)0)
#define TRACE_FUNC_EXIT()                   ((void)0)
#define TRACE_FUNC_EXIT_STATUS(status)      ((void)0)
#define TRACE_FUNC_EXIT_NTSTATUS(status)    ((void)0)

#endif /* DBG */

#endif /* _TRACE_H_ */
