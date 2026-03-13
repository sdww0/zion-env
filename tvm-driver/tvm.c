#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>


#include "tvm.h"

#define   DRV_DESCRIPTION   "tvm"
#define   DRV_VERSION       "1.0.0"

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Jie Wang <iwangjye@whu.edu.cn>");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("Dual BSD/GPL");

static const struct file_operations tvm_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = tvm_ioctl,
};


struct miscdevice tvm_dev = {
  .minor = MISC_DYNAMIC_MINOR,
  .name = "tvm",
  .fops = &tvm_fops,
  .mode = 0666,
};

static int __init tvm_dev_init(void)
{
  int  ret;


  ret = misc_register(&tvm_dev);
  if (ret < 0)
  {
    pr_err("tvm: misc_register() failed\n");
  }

  tvm_dev.this_device->coherent_dma_mask = DMA_BIT_MASK(32);

  pr_info("tvm: " DRV_DESCRIPTION " v" DRV_VERSION "\n");
  return ret;
}

static void __exit tvm_dev_exit(void)
{
  pr_info("tvm: tvm_dev_exit()\n");
  misc_deregister(&tvm_dev);
  return;
}

module_init(tvm_dev_init);
module_exit(tvm_dev_exit);