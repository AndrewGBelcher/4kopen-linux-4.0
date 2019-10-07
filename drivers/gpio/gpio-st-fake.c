/*
 * Copyright (C) 2015 STMicroelectronics
 *
 * Fake gpio driver to simulate GPIOs with any system register.
 *
 * Author: Seraphin Bonnaffe <seraphin.bonnaffe@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#define FGPIO_REG_INDEX		1
#define FGPIO_SIZE_INDEX	2
#define FGPIO_PER_BYTE		8
#define FGPIO_SINGLE_REG_SIZE	32

struct st_fakegpio {
	struct gpio_chip chip;
	struct regmap *gpio_regmap;
	unsigned int ctrl_reg;
	unsigned int ctrl_reg_size;
	unsigned int *dir;
};

static int st_fakegpio_get(struct gpio_chip *gc, unsigned pin)
{
	struct st_fakegpio *fgpio = container_of(gc, struct st_fakegpio, chip);
	unsigned int offset, bitnum, regval;
	int ret;

	offset = pin / FGPIO_SINGLE_REG_SIZE;
	bitnum = pin % FGPIO_SINGLE_REG_SIZE;

	ret = regmap_read(fgpio->gpio_regmap, fgpio->ctrl_reg + offset,
			  &regval);
	if (ret) {
		dev_err(gc->dev, "failed to read register %x: %d\n",
			fgpio->ctrl_reg + offset, ret);
		return ret;
	}

	return !!(regval & BIT(bitnum));
}

static void st_fakegpio_set(struct gpio_chip *gc, unsigned pin, int val)
{
	struct st_fakegpio *fgpio = container_of(gc, struct st_fakegpio, chip);
	unsigned int offset, regval, bitnum;
	int ret;

	offset = pin / FGPIO_SINGLE_REG_SIZE;
	bitnum = pin % FGPIO_SINGLE_REG_SIZE;

	ret = regmap_read(fgpio->gpio_regmap, fgpio->ctrl_reg + offset,
			  &regval);
	if (ret)
		dev_err(gc->dev, "failed to read register %x: %d\n",
			fgpio->ctrl_reg + offset, ret);

	if (val)
		set_bit(bitnum, (long unsigned *) &regval);
	else
		clear_bit(bitnum, (long unsigned *) &regval);

	ret = regmap_write(fgpio->gpio_regmap, fgpio->ctrl_reg + offset,
			   regval);
	if (ret < 0)
		dev_err(gc->dev, "failed to write register %x: %d\n",
			fgpio->ctrl_reg + offset, ret);
}

static int st_fakegpio_direction_input(struct gpio_chip *gc, unsigned pin)
{
	struct st_fakegpio *fgpio = container_of(gc, struct st_fakegpio, chip);
	unsigned int offset, bitnum;

	if (pin > gc->ngpio)
		return -EINVAL;

	offset = pin / FGPIO_SINGLE_REG_SIZE;
	bitnum = pin % FGPIO_SINGLE_REG_SIZE;

	set_bit(bitnum, (long unsigned *) &fgpio->dir[offset]);

	return 0;
}

static int st_fakegpio_direction_output(struct gpio_chip *gc,
					 unsigned pin, int val)
{
	struct st_fakegpio *fgpio = container_of(gc, struct st_fakegpio, chip);
	unsigned int offset, bitnum;

	if (pin > gc->ngpio)
		return -EINVAL;

	offset = pin / FGPIO_SINGLE_REG_SIZE;
	bitnum = pin % FGPIO_SINGLE_REG_SIZE;

	clear_bit(bitnum, (long unsigned *) &fgpio->dir[offset]);

	return 0;
}

static int st_fakegpio_get_direction(struct gpio_chip *gc, unsigned pin)
{
	struct st_fakegpio *fgpio = container_of(gc, struct st_fakegpio, chip);
	unsigned int offset, bitnum;

	if (pin > gc->ngpio)
		return -EINVAL;

	offset = pin / FGPIO_SINGLE_REG_SIZE;
	bitnum = pin % FGPIO_SINGLE_REG_SIZE;

	return !!(fgpio->dir[offset] & BIT(bitnum));
}

static int st_fakegpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct st_fakegpio *fgpio;
	unsigned int cells;
	int ret;

	fgpio = devm_kzalloc(&pdev->dev, sizeof(struct st_fakegpio),
			     GFP_KERNEL);
	if (!fgpio)
		return -ENOMEM;

	/* Get register details from DT */
	fgpio->gpio_regmap =
		syscon_regmap_lookup_by_phandle(np, "st,syscfg");

	if (IS_ERR(fgpio->gpio_regmap)) {
		dev_info(&pdev->dev, "No syscon phandle specified\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_index(np, "st,syscfg", FGPIO_REG_INDEX,
					 &fgpio->ctrl_reg);
	if (ret) {
		dev_err(&pdev->dev, "Reg offset required as syscon args\n");
		return ret;
	}

	ret = of_property_read_u32_index(np, "st,syscfg",
					 FGPIO_SIZE_INDEX,
					 &fgpio->ctrl_reg_size);
	if (ret) {
		dev_err(&pdev->dev, "Reg size required as syscon args\n");
		return ret;
	}

	/* Fill-in private gpiochip struct */
	fgpio->chip.label = "fake gpio";
	fgpio->chip.owner = THIS_MODULE;
	fgpio->chip.get	= st_fakegpio_get;
	fgpio->chip.set	= st_fakegpio_set;
	fgpio->chip.direction_input = st_fakegpio_direction_input;
	fgpio->chip.direction_output = st_fakegpio_direction_output;
	fgpio->chip.get_direction = st_fakegpio_get_direction;
	fgpio->chip.ngpio = fgpio->ctrl_reg_size * FGPIO_PER_BYTE;
	fgpio->chip.can_sleep = 0;
	fgpio->chip.dev	= &pdev->dev;
	fgpio->chip.base = -1;
	fgpio->chip.of_node = np;

	/* Allocate direction */
	cells = (fgpio->chip.ngpio / FGPIO_SINGLE_REG_SIZE) + 1;
	fgpio->dir = devm_kzalloc(&pdev->dev, cells * sizeof(unsigned int),
				  GFP_KERNEL);
	if (!fgpio->dir)
		return -ENOMEM;

	ret = gpiochip_add(&fgpio->chip);
	if (ret) {
		dev_err(&pdev->dev, "unable to add gpiochip: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, fgpio);

	return 0;
}

static int st_fakegpio_remove(struct platform_device *pdev)
{
	struct st_fakegpio *fgpio = platform_get_drvdata(pdev);
	int ret;

	ret = gpiochip_remove(&fgpio->chip);
	if (ret < 0) {
		dev_err(fgpio->chip.dev,
			"unable to remove gpiochip: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct of_device_id st_fakegpio_match[] = {
	{ .compatible = "st,fake-gpio", },
	{},
};
MODULE_DEVICE_TABLE(of, st_fakegpio_match);

static struct platform_driver st_fakegpio_driver = {
	.driver = {
		.name = "ST fake gpio",
		.owner = THIS_MODULE,
		.of_match_table = st_fakegpio_match,
	},
	.probe = st_fakegpio_probe,
	.remove = st_fakegpio_remove,
};

module_platform_driver(st_fakegpio_driver);

MODULE_AUTHOR("Seraphin Bonnaffe <seraphin.bonnaffe@st.com>");
MODULE_DESCRIPTION("STMicroelectronics Fake GPIO driver");
MODULE_LICENSE("GPL v2");
