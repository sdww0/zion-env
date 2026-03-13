#include <linux/dma-mapping.h>


//#define TVM_SBI_EXT_ID 0x8424b46
#define TVM_SBI_EXT_ID 0x8424b45

typedef uintptr_t vaddr_t;
typedef uintptr_t paddr_t;

extern struct miscdevice tvm_dev;


extern long tvm_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);
extern void tvm_reserve_mem(void);

extern void rtvm_cycle_begin(void);
extern void rtvm_cycle_begin(void);