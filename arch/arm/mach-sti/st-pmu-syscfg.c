/*
 * Copyright (C) 2013 STMicroelectronics
 *
 * PMU syscfg driver, used in STMicroelectronics devices.
 *
 * Author: Christophe Kerello <christophe.kerello@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

/* DUAL CORES platforms configuration is described below */
/* bit2: pmuirq(0) enable */
/* bit3: pmuirq(1) enable */
/* bit[16..14]: pmuirq(0) selection */
/* bit[19..17]: pmuirq(1) selection */
#define ST_PMU_DUAL_SYSCFG_MASK	0xfc0ff
#define ST_PMU_DUAL_SYSCFG_VAL	0xd400c

/* STiH415 */
#define STIH415_SYSCFG_642	0xa8

/* STiH416 */
#define STIH416_SYSCFG_7543	0x87c

/* STiH407 */
#define STIH407_SYSCFG_5102	0x198

/* STiD127 */
#define STID127_SYSCFG_734	0x88

/* QUAD CORES platforms configuration is described below */
/* SYSTEM_CONFIG5102 */
/* bit2: pmuirq(0) enable */
/* bit3: pmuirq(1) enable */
/* bit8: pmuirq(0) enable */
/* bit9: pmuirq(1) enable */
/* bit[23..20]: pmuirq(0) selection */
/* bit[27..24]: pmuirq(1) selection */
/* SYSTEM_CONFIG5103 */
/* bit[11..8]: pmuirq(3) selection */
/* bit[15..12]: pmuirq(4) selection */
#define ST_PMU_QUAD_SYSCFG_MASK_1	0xff0fff
#define ST_PMU_QUAD_SYSCFG_VAL_1	0x650030c
#define ST_PMU_QUAD_SYSCFG_MASK_2	0xff00
#define ST_PMU_QUAD_SYSCFG_VAL_2	0xba00
/* STiH418 */
#define STIH418_SYSCFG_5102	0x198
#define STIH418_SYSCFG_5103	0x19C

struct st_pmu_syscfg_soc_cfg {
	unsigned int syscfg;
	unsigned int mask;
	unsigned int val;
};

struct st_pmu_syscfg_dev {
	struct regmap *regmap;
	struct st_pmu_syscfg_soc_cfg *soc_cfg;
};

static int st_pmu_syscfg_enable_irq(struct device *dev)
{
	struct platform_device *pdev =
		container_of(dev, struct platform_device, dev);
	struct st_pmu_syscfg_dev *st_pmu_syscfg = platform_get_drvdata(pdev);

	while ((st_pmu_syscfg->soc_cfg != NULL) &&
			(st_pmu_syscfg->soc_cfg->syscfg != 0)) {
		regmap_update_bits(st_pmu_syscfg->regmap,
				st_pmu_syscfg->soc_cfg->syscfg,
				st_pmu_syscfg->soc_cfg->mask,
				st_pmu_syscfg->soc_cfg->val);
		st_pmu_syscfg->soc_cfg++;
	}
	return 0;
}

static struct st_pmu_syscfg_soc_cfg st_pmu_syscfg_stih415_cfg[] = {
	{
		STIH415_SYSCFG_642,
		ST_PMU_DUAL_SYSCFG_MASK,
		ST_PMU_DUAL_SYSCFG_VAL
	},
	{	0, 0, 0}
};

static struct st_pmu_syscfg_soc_cfg st_pmu_syscfg_stih416_cfg[] = {
	{
		STIH416_SYSCFG_7543,
		ST_PMU_DUAL_SYSCFG_MASK,
		ST_PMU_DUAL_SYSCFG_VAL
	},
	{	0, 0, 0}
};

static struct st_pmu_syscfg_soc_cfg st_pmu_syscfg_stih407_cfg[] = {
	{
		STIH407_SYSCFG_5102,
		ST_PMU_DUAL_SYSCFG_MASK,
		ST_PMU_DUAL_SYSCFG_VAL
	},
	{	0, 0, 0}
};

static struct st_pmu_syscfg_soc_cfg st_pmu_syscfg_stih418_cfg[] = {
	{
		STIH418_SYSCFG_5102,
		ST_PMU_QUAD_SYSCFG_MASK_1,
		ST_PMU_QUAD_SYSCFG_VAL_1
	},
	{
		STIH418_SYSCFG_5103,
		ST_PMU_QUAD_SYSCFG_MASK_2,
		ST_PMU_QUAD_SYSCFG_VAL_2
	},
	{	0, 0, 0}
};

static struct st_pmu_syscfg_soc_cfg st_pmu_syscfg_stid127_cfg[] = {
	{
		STID127_SYSCFG_734,
		ST_PMU_DUAL_SYSCFG_MASK,
		ST_PMU_DUAL_SYSCFG_VAL
	},
	{	0, 0, 0}
};

static struct of_device_id st_pmu_syscfg_of_match[] = {
	{
		.compatible = "st,stih415-pmu-syscfg",
		.data = (void *) st_pmu_syscfg_stih415_cfg,
	},
	{
		.compatible = "st,stih416-pmu-syscfg",
		.data = (void *) st_pmu_syscfg_stih416_cfg,
	},
	{
		.compatible = "st,stih407-pmu-syscfg",
		.data = (void *) st_pmu_syscfg_stih407_cfg,
	},
	{
		.compatible = "st,stih418-pmu-syscfg",
		.data = (void *) st_pmu_syscfg_stih418_cfg,
	},
	{
		.compatible = "st,stid127-pmu-syscfg",
		.data = (void *) st_pmu_syscfg_stid127_cfg,
	},
	{}
};

static int st_pmu_syscfg_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct st_pmu_syscfg_dev *st_pmu_syscfg;
	int ret;

	if (!np) {
		dev_err(dev, "Device tree node not found.\n");
		return -ENODEV;
	}

	st_pmu_syscfg = devm_kzalloc(dev, sizeof(struct st_pmu_syscfg_dev),
					GFP_KERNEL);
	if (!st_pmu_syscfg)
		return -ENOMEM;

	match = of_match_device(st_pmu_syscfg_of_match, dev);
	if (!match)
		return -ENODEV;

	st_pmu_syscfg->soc_cfg = (struct st_pmu_syscfg_soc_cfg *)match->data;

	st_pmu_syscfg->regmap = syscon_regmap_lookup_by_phandle(np,
					"st,syscfg");
	if (IS_ERR(st_pmu_syscfg->regmap)) {
		dev_err(dev, "No syscfg phandle specified\n");
		return PTR_ERR(st_pmu_syscfg->regmap);
	}

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "Failed to add pmu device\n");
		return ret;
	}

	platform_set_drvdata(pdev, st_pmu_syscfg);

	return st_pmu_syscfg_enable_irq(dev);
}

#ifdef CONFIG_PM_SLEEP
static int st_pmu_syscfg_resume(struct device *dev)
{
	/* restore syscfg register */
	return st_pmu_syscfg_enable_irq(dev);
}

static SIMPLE_DEV_PM_OPS(st_pmu_syscfg_pm_ops, NULL, st_pmu_syscfg_resume);
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver st_pmu_syscfg_driver = {
	.probe = st_pmu_syscfg_probe,
	.driver = {
		.name = "st_pmu_config",
#ifdef CONFIG_PM_SLEEP
		.pm = &st_pmu_syscfg_pm_ops,
#endif /* CONFIG_PM_SLEEP */
		.of_match_table = st_pmu_syscfg_of_match,
	},
};

static int __init st_pmu_syscfg_init(void)
{
	return platform_driver_register(&st_pmu_syscfg_driver);
}

device_initcall(st_pmu_syscfg_init);

MODULE_AUTHOR("Christophe Kerello <christophe.kerello@st.com>");
MODULE_DESCRIPTION("STMicroelectronics PMU syscfg driver");
MODULE_LICENSE("GPL v2");
