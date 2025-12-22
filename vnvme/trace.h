/**
 * @file trace.h
 * @brief WPP 跟踪定义
 */

#ifndef _TRACE_H_
#define _TRACE_H_

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
 * 便捷宏
 */
#define TRACE_ERROR(msg, ...)   TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, msg, ##__VA_ARGS__)
#define TRACE_WARN(msg, ...)    TraceEvents(TRACE_LEVEL_WARNING, TRACE_DRIVER, msg, ##__VA_ARGS__)
#define TRACE_INFO(msg, ...)    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, msg, ##__VA_ARGS__)
#define TRACE_VERBOSE(msg, ...) TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DRIVER, msg, ##__VA_ARGS__)

#endif /* _TRACE_H_ */
