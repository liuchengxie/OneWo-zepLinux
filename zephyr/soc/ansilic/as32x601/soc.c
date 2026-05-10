#include <zephyr/kernel.h>
#include <zephyr/init.h>

static int as32x601_soc_init(void)
{
	return 0;
}

SYS_INIT(as32x601_soc_init, PRE_KERNEL_1, 0);
