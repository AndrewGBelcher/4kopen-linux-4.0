/*
 * Copyright (C) 2014 STMicroelectronics
 *
 * CPU idle - ST SoCs
 *
 * Author : Olivier Bideau <olivier.bideau@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * The cpu idle currently implements one idle state :
 * #1 wait-for-interrupt
 *
 */

#include <linux/cpuidle.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <asm/cpuidle.h>

static struct cpuidle_driver st_idle_driver = {
	.name = "st_idle",
	.states = {
		ARM_CPUIDLE_WFI_STATE,
	},
	.state_count = 1,
};

static int st_cpuidle_probe(struct platform_device *pdev)
{
	return cpuidle_register(&st_idle_driver, NULL);
}

int st_cpuidle_remove(struct platform_device *pdev)
{
	cpuidle_unregister(&st_idle_driver);
	return 0;
}

static struct of_device_id st_cpuidle_of_match[] = {
	{
		.compatible = "st,cpuidle",
	},
};

static struct platform_driver st_cpuidle_driver = {
	.probe = st_cpuidle_probe,
	.remove = st_cpuidle_remove,
	.driver = {
		   .name = "st_cpuidle",
		   .owner = THIS_MODULE,
		   .of_match_table = st_cpuidle_of_match,
		   },
};

module_platform_driver(st_cpuidle_driver);

MODULE_AUTHOR("Olivier Bideau <olivier.bideau@st.com>");
MODULE_DESCRIPTION("ST cpu idle driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:st-cpuidle");
