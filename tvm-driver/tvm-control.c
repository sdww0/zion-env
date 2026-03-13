
#include <sys/ioctl.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "tvm_user.h"

int main(int argc, char **argv)
{
    char *type;
    int count;
    unsigned long address = 0;

    if (argc == 2)
    {
        type = argv[1];
        printf("[tvm-control] type=%s\n", type);
    } else if (argc == 3)
    {
        type = argv[1];
        count = atoi(argv[2]);
        printf("[tvm-control] type=%s, count=0x%x\n", type, count);
    }else if (argc == 4){
        type = argv[1];
        count = atoi(argv[2]);
        address = strtoul(argv[3], NULL, 0);
        printf("[tvm-control] type=%s, count=0x%x, address=0x%lx\n", type, count, address);
    }

    int fd = open("/dev/tvm", O_RDWR);
    if (fd < 0)
    {
        printf("cannot open device file\n");
        return -1;
    }

    if (!strcmp(type, "tvm"))
    {
        printf("ioctl(): RTVM_IOC_RESERVE_TVM_MEM\n");
        if (ioctl(fd, RTVM_IOC_RESERVE_TVM_MEM, count))
        {
            printf("test_tvm failed\n");
        }
    }
    else if (!strcmp(type, "enclave"))
    {
        printf("ioctl(): RTVM_IOC_RESERVE_ENCLAVE_MEM\n");
        if (ioctl(fd, RTVM_IOC_RESERVE_ENCLAVE_MEM, count))
        {
            printf("test_tvm failed\n");
        }
    }
    else if (!strcmp(type, "cycle_begin"))
    {
        printf("ioctl(): cycle_begin\n");
        if (ioctl(fd, RTVM_IOC_CYCLE_BEGIN))
        {
            printf("test_tvm failed\n");
        }
    }
    else if (!strcmp(type, "cycle_end"))
    {
        printf("ioctl(): cycle_end\n");
        if (ioctl(fd, RTVM_IOC_CYCLE_END))
        {
            printf("test_tvm failed\n");
        }
    } 
    else if(!strcmp(type, "clean_sec_mem"))
    {
        printf("ioctl(): clean_sec_mem\n");
        if (ioctl(fd, POTARA_IOC_CLEAN_SEC_MEM))
        {
            printf("test_tvm failed\n");
        }

    }else if (!strcmp(type, "page_region_modify"))
    {
        printf("ioctl(): page_region_modify\n");
        if (ioctl(fd, RTVM_IOC_PAGE_REGION_MODIFY, address))
        {
            printf("test_tvm failed\n");
        }
    }else if (!strcmp(type, "cvm_private_memory_access"))
    {
        printf("ioctl(): cvm_private_memory_access\n");
        if (ioctl(fd, RTVM_IOC_CVM_PRIVATE_MEMORY_ACCESS, address))
        {
            printf("test_tvm failed\n");
        }
    }else if (!strcmp(type, "page_region_access"))
    {
        printf("ioctl(): page_region_access\n");
        if (ioctl(fd, RTVM_IOC_PAGE_REGION_ACCESS, address))
        {
            printf("test_tvm failed\n");
        } // ffffffd81fe00000
    }else if (!strcmp(type, "phys_memory_access"))
    {
        printf("ioctl(): phys_memory_access\n");
        if (ioctl(fd, RTVM_IOC_CVM_PHYS_MEMORY_ACCESS, address))
        {
            printf("test_tvm failed\n");
        }
    }else if (!strcmp(type, "phys_memory_modify"))
    {
        printf("ioctl(): phys_memory_modify\n");
        if (ioctl(fd, RTVM_IOC_CVM_PHYS_MEMORY_MODIFY, address))
        {
            printf("test_tvm failed\n");
        }
    }

    return 0;
}