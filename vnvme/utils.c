/**
 * @file utils.c
 * @brief 通用辅助函数
 * 
 * 提供驱动中通用的辅助功能，如字符串分配、内存操作等。
 */

#include "vnvme.h"

//===========================================================================
// 字符串辅助函数
//===========================================================================

/**
 * @brief 分配并复制宽字符串
 * 
 * 使用 VNVME_POOL_TAG 从分页池分配内存并复制字符串。
 * 调用方负责使用 VNVME_FREE_POOL 释放返回的内存。
 * 
 * @param SourceString 源字符串 (null 结尾)
 * @return 新分配的字符串副本，失败返回 NULL
 */
PWSTR
VnvmeAllocateString(
    _In_ PCWSTR SourceString
    )
{
    SIZE_T length;
    PWSTR result;
    
    if (SourceString == NULL) {
        return NULL;
    }
    
    length = (wcslen(SourceString) + 1) * sizeof(WCHAR);
    result = (PWSTR)ExAllocatePool2(POOL_FLAG_PAGED, length, VNVME_POOL_TAG);
    if (result != NULL) {
        RtlCopyMemory(result, SourceString, length);
    }
    return result;
}

/**
 * @brief 分配多字符串 (双 null 结尾)
 * 
 * 用于 PnP 设备 ID 查询等需要多字符串格式的场景。
 * 格式: "String1\0String2\0\0"
 * 
 * @param String1 第一个字符串 (必须)
 * @param String2 第二个字符串 (可选, NULL 表示只有一个字符串)
 * @return 新分配的多字符串，失败返回 NULL
 */
PWSTR
VnvmeAllocateMultiString(
    _In_ PCWSTR String1,
    _In_opt_ PCWSTR String2
    )
{
    SIZE_T len1, len2, totalLen;
    PWSTR result;
    
    if (String1 == NULL) {
        return NULL;
    }
    
    len1 = (wcslen(String1) + 1) * sizeof(WCHAR);
    len2 = String2 ? (wcslen(String2) + 1) * sizeof(WCHAR) : 0;
    totalLen = len1 + len2 + sizeof(WCHAR);  // 额外的 null 结尾
    
    result = (PWSTR)ExAllocatePool2(POOL_FLAG_PAGED, totalLen, VNVME_POOL_TAG);
    if (result != NULL) {
        RtlZeroMemory(result, totalLen);
        RtlCopyMemory(result, String1, len1);
        if (String2) {
            RtlCopyMemory((PCHAR)result + len1, String2, len2);
        }
    }
    return result;
}
