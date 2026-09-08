#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of_address.h>

#include "zion-enclave.h"
#include "../../deps/tvm-driver/tvm.h"

#define ZION_DRIVER_VERSION "1.0.0"

/* Used only on platforms without a zion,trusted-memory DT node (QEMU). */
static unsigned long core_pages = 131072;
module_param(core_pages, ulong, 0444);
MODULE_PARM_DESC(core_pages,
		 "trusted-pool pages when zion,trusted-memory is absent");

static int zion_core_page_count(unsigned long *page_count)
{
	struct device_node *node;
	struct resource resource;
	resource_size_t size;
	int ret;

	node = of_find_compatible_node(NULL, NULL, "zion,trusted-memory");
	if (!node) {
		if (!core_pages)
			return -EINVAL;
		*page_count = core_pages;
		return 0;
	}

	ret = of_address_to_resource(node, 0, &resource);
	of_node_put(node);
	if (ret)
		return ret;
	size = resource_size(&resource);
	if (!size || (size & (PAGE_SIZE - 1)) ||
	    (size >> PAGE_SHIFT) > ULONG_MAX)
		return -EINVAL;
	*page_count = (unsigned long)(size >> PAGE_SHIFT);
	return 0;
}

static int __init zion_driver_init(void)
{
	unsigned long page_count;
	int ret;

	ret = zion_core_page_count(&page_count);
	if (ret) {
		pr_err("zion: cannot determine trusted-pool size: %d\n", ret);
		return ret;
	}

	ret = tvm_frontend_register();
	if (ret)
		return ret;
	ret = zion_frontend_register(false);
	if (ret) {
		tvm_frontend_unregister();
		return ret;
	}

	/* One allocator and one SBI reservation serve both public device ABIs. */
	ret = rtvm_reserve_core(page_count, 0);
	if (ret) {
		pr_err("zion: shared trusted-pool reservation failed: %d\n", ret);
		zion_frontend_unregister();
		tvm_frontend_unregister();
		return ret;
	}

	pr_info("zion: unified driver v%s ready, pages=%lu, devices=/dev/zion_cvm,/dev/zion_enclave\n",
		ZION_DRIVER_VERSION, page_count);
	return 0;
}

static void __exit zion_driver_exit(void)
{
	/* rtvm_reserve_core pins this module because the core cannot be released. */
	zion_frontend_unregister();
	tvm_frontend_unregister();
}

module_init(zion_driver_init);
module_exit(zion_driver_exit);

MODULE_DESCRIPTION("Zion unified CVM and Zion enclave driver");
MODULE_AUTHOR("Zion contributors");
MODULE_VERSION(ZION_DRIVER_VERSION);
MODULE_LICENSE("Dual BSD/GPL");
