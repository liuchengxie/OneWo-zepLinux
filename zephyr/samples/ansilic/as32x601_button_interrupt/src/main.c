#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "gpio_ioctl.h"

#define BTN0_NODE DT_ALIAS(sw0)
#define BTN1_NODE DT_ALIAS(sw1)
#define BTN2_NODE DT_ALIAS(sw2)

struct gpio_handle {
	const struct device *dev;
	const char *path;
	bool in_use;
};

struct button_ctx {
	const struct gpio_dt_spec spec;
	const char *name;
	const char *path;
	uint32_t count;
	int fd;
};

static struct button_ctx buttons[] = {
	{
		.spec = GPIO_DT_SPEC_GET(BTN0_NODE, gpios),
		.name = "PB3",
		.path = "/dev/gpioB",
		.fd = -1,
	},
	{
		.spec = GPIO_DT_SPEC_GET(BTN1_NODE, gpios),
		.name = "PB4",
		.path = "/dev/gpioB",
		.fd = -1,
	},
	{
		.spec = GPIO_DT_SPEC_GET(BTN2_NODE, gpios),
		.name = "PE10",
		.path = "/dev/gpioE",
		.fd = -1,
	},
};

static struct gpio_handle gpio_handles[ARRAY_SIZE(buttons)];
static struct gpio_callback gpiob_callback;
static struct gpio_callback gpioe_callback;

static const struct gpio_dt_spec *lookup_gpio_spec(const char *path)
{
	if (strcmp(path, "/dev/gpioB") == 0) {
		return &buttons[0].spec;
	}

	if (strcmp(path, "/dev/gpioE") == 0) {
		return &buttons[2].spec;
	}

	return NULL;
}

int open(const char *path, int flags, ...)
{
	const struct gpio_dt_spec *spec;

	ARG_UNUSED(flags);

	spec = lookup_gpio_spec(path);
	if (spec == NULL) {
		return -ENOENT;
	}

	if (!device_is_ready(spec->port)) {
		return -ENODEV;
	}

	for (size_t i = 0; i < ARRAY_SIZE(gpio_handles); i++) {
		if (!gpio_handles[i].in_use) {
			gpio_handles[i].dev = spec->port;
			gpio_handles[i].path = path;
			gpio_handles[i].in_use = true;
			return (int)i;
		}
	}

	return -EMFILE;
}

int close(int fd)
{
	if (fd < 0 || fd >= (int)ARRAY_SIZE(gpio_handles) || !gpio_handles[fd].in_use) {
		return -EBADF;
	}

	gpio_handles[fd].dev = NULL;
	gpio_handles[fd].path = NULL;
	gpio_handles[fd].in_use = false;
	return 0;
}

int ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	void *arg;

	if (fd < 0 || fd >= (int)ARRAY_SIZE(gpio_handles) || gpio_handles[fd].dev == NULL) {
		return -EBADF;
	}

	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);

	switch (request) {
	case GPIO_IOC_CONFIGURE: {
		struct gpio_ioctl_cfg *cfg = arg;
		return gpio_pin_configure(gpio_handles[fd].dev, cfg->pin, cfg->flags);
	}
	case GPIO_IOC_SET_IRQ: {
		struct gpio_ioctl_irq *irq = arg;
		return gpio_pin_interrupt_configure(gpio_handles[fd].dev, irq->pin, irq->flags);
	}
	case GPIO_IOC_GET_VALUE: {
		struct gpio_ioctl_value *value = arg;
		value->value = gpio_pin_get(gpio_handles[fd].dev, value->pin);
		return value->value < 0 ? value->value : 0;
	}
	default:
		return -ENOTSUP;
	}
}

static void button_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(cb);

	if (port == buttons[0].spec.port) {
		if ((pins & BIT(buttons[0].spec.pin)) != 0U) {
			buttons[0].count++;
		}
		if ((pins & BIT(buttons[1].spec.pin)) != 0U) {
			buttons[1].count++;
		}
	}

	if (port == buttons[2].spec.port) {
		if ((pins & BIT(buttons[2].spec.pin)) != 0U) {
			buttons[2].count++;
		}
	}
}

int main(void)
{
	int ret;

	printk("AS32X601 button interrupt test start\n");

	for (size_t i = 0; i < ARRAY_SIZE(buttons); i++) {
		struct gpio_ioctl_cfg cfg = {
			.pin = buttons[i].spec.pin,
			.flags = GPIO_INPUT | GPIO_PULL_UP,
		};
		struct gpio_ioctl_irq irq = {
			.pin = buttons[i].spec.pin,
			.flags = GPIO_INT_EDGE_FALLING,
		};

		buttons[i].fd = open(buttons[i].path, 0);
		if (buttons[i].fd < 0) {
			printk("button %s open %s failed: %d\n", buttons[i].name, buttons[i].path,
			       buttons[i].fd);
			return buttons[i].fd;
		}

		ret = ioctl(buttons[i].fd, GPIO_IOC_CONFIGURE, &cfg);
		if (ret < 0) {
			printk("button %s configure failed: %d\n", buttons[i].name, ret);
			return ret;
		}

		ret = ioctl(buttons[i].fd, GPIO_IOC_SET_IRQ, &irq);
		if (ret < 0) {
			printk("button %s irq config failed: %d\n", buttons[i].name, ret);
			return ret;
		}

		printk("button %s ready on pin %u via %s\n",
		       buttons[i].name,
		       buttons[i].spec.pin,
		       buttons[i].path);
	}

	gpio_init_callback(&gpiob_callback, button_isr,
			       BIT(buttons[0].spec.pin) | BIT(buttons[1].spec.pin));
	ret = gpio_add_callback(buttons[0].spec.port, &gpiob_callback);
	if (ret < 0) {
		printk("GPIOB callback add failed: %d\n", ret);
		return ret;
	}

	gpio_init_callback(&gpioe_callback, button_isr, BIT(buttons[2].spec.pin));
	ret = gpio_add_callback(buttons[2].spec.port, &gpioe_callback);
	if (ret < 0) {
		printk("GPIOE callback add failed: %d\n", ret);
		return ret;
	}

	while (1) {
		struct gpio_ioctl_value v0 = { .pin = buttons[0].spec.pin };
		struct gpio_ioctl_value v1 = { .pin = buttons[1].spec.pin };
		struct gpio_ioctl_value v2 = { .pin = buttons[2].spec.pin };

		(void)ioctl(buttons[0].fd, GPIO_IOC_GET_VALUE, &v0);
		(void)ioctl(buttons[1].fd, GPIO_IOC_GET_VALUE, &v1);
		(void)ioctl(buttons[2].fd, GPIO_IOC_GET_VALUE, &v2);

		printk("lvl PB3=%d PB4=%d PE10=%d cnt PB3=%u PB4=%u PE10=%u\n",
		       v0.value, v1.value, v2.value,
		       buttons[0].count,
		       buttons[1].count,
		       buttons[2].count);
		k_msleep(1000);
	}

	for (size_t i = 0; i < ARRAY_SIZE(buttons); i++) {
		if (buttons[i].fd >= 0) {
			(void)close(buttons[i].fd);
		}
	}

	return 0;
}
