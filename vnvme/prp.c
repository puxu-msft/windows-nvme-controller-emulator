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
        // 多页，PRP2 是 PRP 列表的物理地址
        // PRP 列表是一个物理地址数组，每个条目 8 字节
        PHYSICAL_ADDRESS prpListPhysAddr;
        PULONGLONG prpList;
        ULONG remainingBytes;
        ULONG prpListSize;
        ULONG maxEntriesPerPage;
        ULONG i;
        
        prpListPhysAddr.QuadPart = Prp2;
        remainingBytes = DataLength - firstPageBytes;
        maxEntriesPerPage = pageSize / sizeof(ULONGLONG);
        
        // 计算 PRP 列表需要的大小
        prpListSize = (numPages - 1) * sizeof(ULONGLONG);
        if (prpListSize > pageSize) {
            // PRP 列表本身跨页，需要递归处理
            // 简化实现: 限制为单页 PRP 列表 (最多支持 512 个条目 = 2GB 传输)
            prpListSize = pageSize;
        }
        
        // 映射 PRP 列表
        prpList = (PULONGLONG)MmMapIoSpace(prpListPhysAddr, prpListSize, MmCached);
        if (prpList == NULL) {
            TRACE_ERROR("VnvmeParsePrpList: Failed to map PRP list at 0x%016llX", Prp2);
            VNVME_FREE_POOL(entries);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        
        // 解析 PRP 列表中的每个条目
        for (i = 0; i < numPages - 1 && remainingBytes > 0; i++) {
            ULONGLONG prpEntry = prpList[i];
            ULONG entryLength;
            
            // 检查是否是链式 PRP 列表 (最后一个条目可能指向下一个 PRP 列表页)
            // 简化实现: 假设所有条目都是数据页地址
            
            if (prpEntry == 0) {
                TRACE_ERROR("VnvmeParsePrpList: NULL PRP entry at index %u", i);
                MmUnmapIoSpace(prpList, prpListSize);
                VNVME_FREE_POOL(entries);
                return STATUS_INVALID_PARAMETER;
            }
            
            entries[entryIndex].PhysicalAddress = prpEntry & ~((ULONGLONG)pageSize - 1);
            entries[entryIndex].Offset = (ULONG)(prpEntry & (pageSize - 1));
            
            // 计算此条目的数据长度
            entryLength = pageSize - entries[entryIndex].Offset;
            if (entryLength > remainingBytes) {
                entryLength = remainingBytes;
            }
            entries[entryIndex].Length = entryLength;
            
            remainingBytes -= entryLength;
            entryIndex++;
            
            // 安全检查
            if (entryIndex >= numPages) {
                break;
            }
        }
        
        MmUnmapIoSpace(prpList, prpListSize);
        
        TRACE_VERBOSE("VnvmeParsePrpList: Parsed %u entries from PRP list", entryIndex);
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
