/**
 * @file prp.c
 * @brief PRP (Physical Region Page) 解析
 * 
 * 解析 NVMe 命令中的 PRP 列表，进行数据传输。
 */

#include "vnvme.h"

//===========================================================================
// PRP 解析
//===========================================================================

/**
 * @brief 解析 PRP 列表获取物理地址
 */
NTSTATUS
VnvmeParsePrpList(
    _In_ ULONGLONG Prp1,
    _In_ ULONGLONG Prp2,
    _In_ ULONG DataLength,
    _Out_ PVNVME_PRP_ENTRY* PrpEntries,
    _Out_ PULONG EntryCount
    )
{
    ULONG pageSize = PAGE_SIZE;
    ULONG numPages;
    ULONG firstPageOffset;
    ULONG firstPageBytes;
    PVNVME_PRP_ENTRY entries;
    ULONG entryIndex = 0;
    
    *PrpEntries = NULL;
    *EntryCount = 0;
    
    if (DataLength == 0) {
        return STATUS_SUCCESS;
    }
    
    // 计算第一页偏移和字节数
    firstPageOffset = (ULONG)(Prp1 & (pageSize - 1));
    firstPageBytes = pageSize - firstPageOffset;
    
    if (firstPageBytes >= DataLength) {
        // 数据完全在第一页内
        numPages = 1;
    } else {
        // 需要多页
        numPages = 1 + ((DataLength - firstPageBytes + pageSize - 1) / pageSize);
    }
    
    TRACE_VERBOSE("VnvmeParsePrpList: DataLength=%u, NumPages=%u", DataLength, numPages);
    
    // 分配 PRP 条目数组
    entries = (PVNVME_PRP_ENTRY)VNVME_ALLOC_POOL(
        NonPagedPoolNx, 
        numPages * sizeof(VNVME_PRP_ENTRY)
        );
    
    if (entries == NULL) {
        TRACE_ERROR("VnvmeParsePrpList: Failed to allocate PRP entries");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(entries, numPages * sizeof(VNVME_PRP_ENTRY));
    
    // 第一个条目：PRP1
    entries[entryIndex].PhysicalAddress = Prp1 & ~((ULONGLONG)pageSize - 1);
    entries[entryIndex].Offset = firstPageOffset;
    entries[entryIndex].Length = (firstPageBytes >= DataLength) ? DataLength : firstPageBytes;
    entryIndex++;
    
    if (numPages == 1) {
        // 单页，完成
        *PrpEntries = entries;
        *EntryCount = entryIndex;
        return STATUS_SUCCESS;
    }
    
    if (numPages == 2) {
        // 双页，PRP2 直接是第二页地址
        entries[entryIndex].PhysicalAddress = Prp2 & ~((ULONGLONG)pageSize - 1);
        entries[entryIndex].Offset = 0;
        entries[entryIndex].Length = DataLength - firstPageBytes;
        entryIndex++;
    } else {
        // 多页，PRP2 是 PRP 列表地址
        // TODO: Phase 4 - 实现完整的 PRP 列表解析
        // 当前不支持超过 2 页的传输，返回错误防止数据损坏
        TRACE_ERROR("VnvmeParsePrpList: PRP list parsing not implemented for %u pages (max 2 supported)", numPages);
        VNVME_FREE_POOL(entries);
        return STATUS_NOT_IMPLEMENTED;
    }
    
    *PrpEntries = entries;
    *EntryCount = entryIndex;
    
    return STATUS_SUCCESS;
}

/**
 * @brief 释放 PRP 条目数组
 */
VOID
VnvmeFreePrpEntries(
    _In_ PVNVME_PRP_ENTRY PrpEntries
    )
{
    if (PrpEntries != NULL) {
        VNVME_FREE_POOL(PrpEntries);
    }
}

//===========================================================================
// 数据传输
//===========================================================================

/**
 * @brief 从主机内存读取数据
 */
NTSTATUS
VnvmeReadFromHostMemory(
    _In_ ULONGLONG PhysicalAddress,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    PHYSICAL_ADDRESS physAddr;
    PVOID mappedAddr;
    
    physAddr.QuadPart = PhysicalAddress;
    
    TRACE_VERBOSE("VnvmeReadFromHostMemory: PA=0x%016llX, Len=%u", PhysicalAddress, Length);
    
    // 映射物理地址
    mappedAddr = MmMapIoSpace(physAddr, Length, MmCached);
    if (mappedAddr == NULL) {
        TRACE_ERROR("VnvmeReadFromHostMemory: MmMapIoSpace failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 复制数据
    RtlCopyMemory(Buffer, mappedAddr, Length);
    
    // 取消映射
    MmUnmapIoSpace(mappedAddr, Length);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 向主机内存写入数据
 */
NTSTATUS
VnvmeWriteToHostMemory(
    _In_ ULONGLONG PhysicalAddress,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    PHYSICAL_ADDRESS physAddr;
    PVOID mappedAddr;
    
    physAddr.QuadPart = PhysicalAddress;
    
    TRACE_VERBOSE("VnvmeWriteToHostMemory: PA=0x%016llX, Len=%u", PhysicalAddress, Length);
    
    // 映射物理地址
    mappedAddr = MmMapIoSpace(physAddr, Length, MmCached);
    if (mappedAddr == NULL) {
        TRACE_ERROR("VnvmeWriteToHostMemory: MmMapIoSpace failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 复制数据
    RtlCopyMemory(mappedAddr, Buffer, Length);
    
    // 取消映射
    MmUnmapIoSpace(mappedAddr, Length);
    
    return STATUS_SUCCESS;
}
