#ifndef GPIO_IOCTL_H_
#define GPIO_IOCTL_H_

#include <sys/ioctl.h>

#include <zephyr/drivers/gpio.h>

#define GPIO_IOC_MAGIC        'G'
#define GPIO_IOC_CONFIGURE    _IOW(GPIO_IOC_MAGIC, 0x01, struct gpio_ioctl_cfg)
#define GPIO_IOC_SET_IRQ      _IOW(GPIO_IOC_MAGIC, 0x02, struct gpio_ioctl_irq)
#define GPIO_IOC_GET_VALUE    _IOWR(GPIO_IOC_MAGIC, 0x03, struct gpio_ioctl_value)

struct gpio_ioctl_cfg {
	gpio_pin_t pin;
	gpio_flags_t flags;
};

struct gpio_ioctl_irq {
	gpio_pin_t pin;
	gpio_flags_t flags;
};

struct gpio_ioctl_value {
	gpio_pin_t pin;
	int value;
};

#endif
