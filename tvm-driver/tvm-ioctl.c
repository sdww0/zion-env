#include <asm/sbi.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <asm/page.h>
#include <linux/highmem.h>
#include <linux/io.h>

#include "tvm.h"
#include "tvm_user.h"

void rtvm_reserve_tvm_mem(unsigned long count) // 请求分配的页数量
{
    vaddr_t vaddr = 0;
    phys_addr_t device_phys_addr = 0;
    // unsigned long count = min_pages;
    vaddr = (vaddr_t)dma_alloc_coherent(tvm_dev.this_device,
                                        count << PAGE_SHIFT,
                                        &device_phys_addr,
                                        GFP_KERNEL | __GFP_DMA32);

    pr_info("[tvm-driver] rtvm_reserve_tvm_mem(): vaddr=%lx, device_phys_addr=%llx\n", vaddr, device_phys_addr);
    if (device_phys_addr != 0)
    {
        struct sbiret ret = sbi_ecall(TVM_SBI_EXT_ID, 1014,
                                      0, (unsigned long)device_phys_addr, count, 0, 0, 0);
    }
    else
    {
        pr_info("[tvm-driver] !!!ERROR!!! device_phys_addr is 0.\n");
    }
}

void rtvm_reserve_enclave_mem(unsigned long count)
{
    vaddr_t vaddr = 0;
    phys_addr_t device_phys_addr = 0;
    // unsigned long count = min_pages;
    // unsigned long count = 1024 * 256;
    vaddr = (vaddr_t)dma_alloc_coherent(tvm_dev.this_device,
                                        count << PAGE_SHIFT,
                                        &device_phys_addr,
                                        GFP_KERNEL | __GFP_DMA32);

    pr_info("[tvm-driver] rtvm_reserve_enclave_mem(): vaddr=%lx, device_phys_addr=%llx\n", vaddr, device_phys_addr);
    if (device_phys_addr != 0)
    {
        struct sbiret ret = sbi_ecall(TVM_SBI_EXT_ID, 1014,
                                      1, (unsigned long)device_phys_addr, count, 0, 0, 0);
    }
    else
    {
        pr_info("[tvm-driver] !!!ERROR!!! device_phys_addr is 0.\n");
    }
}

void rtvm_cycle_begin(void)
{

    struct sbiret ret = sbi_ecall(TVM_SBI_EXT_ID, 1027,
                                      0, 0, 0, 0, 0, 0);

}

void rtvm_cycle_end(void)
{

    struct sbiret ret = sbi_ecall(TVM_SBI_EXT_ID, 1028,
                                      0, 0, 0, 0, 0, 0);

}

void potara_clean_sec_mem(void)
{
    struct sbiret ret = sbi_ecall(TVM_SBI_EXT_ID, 1029,
                                      0, 0, 0, 0, 0, 0);
}

#define PAGE_SIZE 4096

void rtvm_page_region_access(unsigned long base_address)
{
    unsigned long access_address = base_address + PAGE_SIZE;
    pr_info("[tvm-driver] Access the page region: address=0x%lx\n", access_address);
    unsigned long value = (*(unsigned long *)access_address);
    pr_info("[tvm-driver] Access the page region: value=0x%lx\n", value);
}

void rtvm_page_region_modify(unsigned long base_address)
{
    unsigned long access_address = base_address + PAGE_SIZE;
    pr_info("[tvm-driver] Modify the page region: address=0x%lx\n", access_address);
    (*(unsigned long *)access_address) = 1024;
    pr_info("[tvm-driver] Modify the page region: value=0x%lx\n", (*(unsigned long *)access_address));
}

void rtvm_cvm_private_memory_access(unsigned long base_address)
{
    unsigned long access_address = base_address + 1024 * 1024 * 16 * 2 - PAGE_SIZE * 2;
    pr_info("[tvm-driver] Access the private memory: address=0x%lx\n", access_address);
    pr_info("[tvm-driver] SATP: %lx", csr_read(satp));
    unsigned long value = (*(unsigned long *)access_address);
    pr_info("[tvm-driver] Access the private memory: value=0x%lx\n", value);
}

void rtvm_cvm_phys_memory_access(unsigned long long base_address)
{
    // 1. 获取页对齐的物理基址和页内偏移
    phys_addr_t phys_page = base_address & ~0xFFFULL;
    unsigned long long offset = base_address & PAGE_SIZE; 
    
    // 2. 关键修改：使用 memremap 替代 ioremap
    // MEMREMAP_WB 表示使用 Write-Back (可缓存) 模式，这会绕过底层的 I/O 地址转换
    void *access_address = memremap(phys_page, PAGE_SIZE, MEMREMAP_WB);
    pr_info("[tvm-driver] Access the physical memory: access_address=0x%px, physical page=0x%llx, offset=0x%lx\n", 
            access_address, (unsigned long long)phys_page, offset);
    if (!access_address) {
        pr_err("[tvm-driver] memremap failed for paddr 0x%llx\n", (unsigned long long)phys_page);
        return;
    }

    
    // 3. 计算最终的虚拟地址
    // 注意：这里不再使用 __iomem 注解，因为它现在被视为普通内存
    void *address = access_address + offset;
    
    pr_info("[tvm-driver] Access the physical memory: address=0x%px, physical address = 0x%lx\n", 
            address, base_address);

    pr_info("[tvm-driver] SATP: %lx", csr_read(satp));
    
    // 4. 关键修改：既然映射成了普通内存，就不该再用 ioread32()
    // 直接用指针解引用即可。为了防止编译器优化掉这次读取，推荐使用 READ_ONCE()
    unsigned int value = READ_ONCE(*(unsigned int *)address);
    
    pr_info("[tvm-driver] Access the physical memory: value=0x%x\n", value);

    // 5. 别忘了使用配套的 unmap 函数释放映射
    memunmap(access_address);
}

void rtvm_cvm_phys_memory_modify(unsigned long base_address)
{
    // Use ioremap to create mapping to physical memory in linux
    void __iomem *access_address = ioremap(base_address & ~0xFFF, PAGE_SIZE);
    void __iomem *address = access_address + (base_address & 0xFFF);
    pr_info("[tvm-driver] Modify the physical memory: address=0x%lx, physical address = 0x%lx\n", address, base_address);
    pr_info("[tvm-driver] SATP: %lx", csr_read(satp));
    (*(unsigned long *)address) = 1024;
    pr_info("[tvm-driver] Modify the physical memory: address=0x%lx\n", address);
}

long tvm_ioctl(struct file *filep, unsigned int cmd, unsigned long count)
{
    long ret;
    //   char data[512];

    //   size_t ioc_size;

    //   if (!arg)
    //     return -EINVAL;

    //   ioc_size = _IOC_SIZE(cmd);
    //   ioc_size = ioc_size > sizeof(data) ? sizeof(data) : ioc_size;

    //   if (copy_from_user(data,(void __user *) arg, ioc_size))
    //     return -EFAULT;

    switch (cmd)
    {
    case RTVM_IOC_RESERVE_TVM_MEM:
        pr_info("[tvm-driver] RTVM_IOC_RESERVE_TVM_MEM, count=0x%lx\n", count);
        rtvm_reserve_tvm_mem(count);
        // ret = keystone_create_enclave(filep, (unsigned long) data);
        break;
    case RTVM_IOC_RESERVE_ENCLAVE_MEM:
        pr_info("[tvm-driver] RTVM_IOC_RESERVE_ENCLAVE_MEM, count=0x%lx\n", count);
        rtvm_reserve_enclave_mem(count);
        // ret = keystone_create_enclave(filep, (unsigned long) data);
        break;
    case RTVM_IOC_CYCLE_BEGIN:
        rtvm_cycle_begin();
        break;
    case RTVM_IOC_CYCLE_END:
        rtvm_cycle_end();
        break;
    case POTARA_IOC_CLEAN_SEC_MEM:
        pr_info("[tvm-driver] potara_clean_sec_mem\n");
        potara_clean_sec_mem();
        break;
    case RTVM_IOC_PAGE_REGION_ACCESS:
        pr_info("[tvm-driver] RTVM_IOC_PAGE_REGION_ACCESS, base_address=0x%lx\n", count);
        rtvm_page_region_access(count);
        break;
    case RTVM_IOC_PAGE_REGION_MODIFY:
        pr_info("[tvm-driver] RTVM_IOC_PAGE_REGION_MODIFY, base_address=0x%lx\n", count);
        rtvm_page_region_modify(count);
        break;
    case RTVM_IOC_CVM_PRIVATE_MEMORY_ACCESS:
        pr_info("[tvm-driver] RTVM_IOC_CVM_PRIVATE_MEMORY_ACCESS, base_address=0x%lx\n", count);
        rtvm_cvm_private_memory_access(count);
        break;
    case RTVM_IOC_CVM_PHYS_MEMORY_ACCESS:
        pr_info("[tvm-driver] RTVM_IOC_CVM_PHYS_MEMORY_ACCESS, base_address=0x%lx\n", count);
        rtvm_cvm_phys_memory_access(count);
        break;
    case RTVM_IOC_CVM_PHYS_MEMORY_MODIFY:
        pr_info("[tvm-driver] RTVM_IOC_CVM_PHYS_MEMORY_MODIFY, base_address=0x%lx\n", count);
        rtvm_cvm_phys_memory_modify(count);
        break;
    default:
        return -ENOSYS;
    }

    //   if (copy_to_user((void __user*) arg, data, ioc_size))
    //     return -EFAULT;
    ret = 0;
    return ret;
}