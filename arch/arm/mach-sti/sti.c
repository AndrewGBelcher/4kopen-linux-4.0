/*
 * Copyright (C) 2013 STMicroelectronics Limited
 * Author: Srinivas Kandagatla <srinivas.kandagatla@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License Version 2.0 only.  See linux/COPYING for more information.
 */
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/clk-provider.h>
#include <linux/of_gpio.h>
#include "sti.h"

#define ST_GPIO_PINS_PER_BANK	(8)
#define TSOUT1_BYTECLK_GPIO (5 * ST_GPIO_PINS_PER_BANK + 0)

/* On stid127-b2112 PIO5[0] (TSOUT1_BYTECLK) goes to CN10 (TSAERROR_TSAERROR).
 * On b2105, CN29 (NIM3) it is wired to SIS2_ERROR.
 * This patch is to force TSOUT1_BYTECLK to low for NIM3 connection with
 * Cannes
 */
void __init sti_init_machine_late(void)
{
	if (of_machine_is_compatible("st,stid127"))
		gpio_request_one(TSOUT1_BYTECLK_GPIO,
				GPIOF_OUT_INIT_LOW, "tsout1_byteclk");
}

void __init sti_init_machine(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static int sti_register_cpufreqcpu0(void)
{
	struct platform_device_info devinfo = { .name = "cpufreq-cpu0", };

	platform_device_register_full(&devinfo);
	return 0;

}
late_initcall(sti_register_cpufreqcpu0);
