/*
 * Copyright (C) 2015 STMicroelectronics (R&D) Limited
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <dt-bindings/reset-controller/stid325-resets.h>

#include "reset-syscfg.h"

static const char syscfg_fc8_net[] = "st,stid325-fc8-networking-syscfg";
static const char syscfg_fc11_bootdev[] = "st,stid325-fc11-bootdev-syscfg";
static const char syscfg_fc7_hsif[] = "st,stid325-fc7-hsif-syscfg";
static const char syscfg_fc1_lpm[] = "st,stid325-fc1-lpm_config";

/*
 * Powerdown is controlled by clk_en bit in the CLK_RST system config register.
 * This means that there is no powerdown_req but the powerdown_ack can be
 * available from the specific area for some IPs.
 * By default clk_en is set.
 */
#define STID325_PDN_FC7(_bit, clk_rst_reg, specific_status_reg, _stat) \
	_SYSCFG_RST_CH(syscfg_fc7_hsif, clk_rst_reg, _bit, \
		       specific_status_reg, _stat)
#define STID325_PDN_FC11(_bit, clk_rst_reg, specific_status_reg, _stat) \
	_SYSCFG_RST_CH(syscfg_fc11_bootdev, clk_rst_reg, _bit, \
		       specific_status_reg, _stat)

#define SYSCFG_7478	0x130138	/* POWER_DOWN_STATUS */

static const struct syscfg_reset_channel_data stid325_powerdowns[] = {

	/* FC7 HSIF */
	[STID325_FC7_USB3_POWERDOWN_REQ] =
	    STID325_PDN_FC7(16, 0x0, SYSCFG_7478, 6),
	/* PCIe0 SATA0 combo */
	[STID325_FC7_SATA_0_POWERDOWN_REQ] =
	    STID325_PDN_FC7(17, 0x20000, SYSCFG_7478, 0),
	[STID325_FC7_PCIE_0_POWERDOWN_REQ] =
	    STID325_PDN_FC7(16, 0x20000, SYSCFG_7478, 1),
	/* PCIe 1 and 2 */
	[STID325_FC7_PCIE_1_POWERDOWN_REQ] =
	    STID325_PDN_FC7(16, 0x30000, SYSCFG_7478, 3),
	[STID325_FC7_PCIE_2_POWERDOWN_REQ] =
	    STID325_PDN_FC7(16, 0x40000, SYSCFG_7478, 5),

	/* FC8 Networking */
	[STID325_FC8_CLKEN_FP3_0_CLK_EN] = SYSCFG_SRST(syscfg_fc8_net, 0x0, 16),
	[STID325_FC8_CLKEN_TELSS_0_CLK_EN] =
	    SYSCFG_SRST(syscfg_fc8_net, 0x20000, 16),

	/* FC11 Bootdev */
	[STID325_FC11_FLASH_CLKEN_NAND_CLK_EN] =
	    STID325_PDN_FC11(16, 0x20000, SYSCFG_7478, 4),
	[STID325_FC11_SDEMMC0_CLKEN_CLK_EN] =
	    SYSCFG_SRST(syscfg_fc11_bootdev, 0x50000, 16),
	[STID325_FC11_SDEMMC1_CLKEN_CLK_EN] =
	    SYSCFG_SRST(syscfg_fc11_bootdev, 0x60000, 16),
};

/* Reset definitions. */
#define LPM_CONFIG_1    0x4	/* Softreset IRB & SBC UART ... */

static const struct syscfg_reset_channel_data stid325_softresets[] = {

	/* FC7: HSIF USB3 */
	[STID325_FC7_USB3_PHY_USB2_CALIB_RESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, 0x0, 3),
	[STID325_FC7_USB3_PHY_USB2_RESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, 0x0, 2),
	[STID325_FC7_USB3_RESET] = SYSCFG_SRST(syscfg_fc7_hsif, 0x0, 0),

	/* FC7: PCI0/SATA0 */
	[STID325_FC7_PCIE_SATA_0_MIPHY_RESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, 0x20000, 4),
	[STID325_FC7_SATA_0_PMALIVE_RESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, 0x20000, 3),
	[STID325_FC7_SATA_0_RESET] = SYSCFG_SRST(syscfg_fc7_hsif, 0x20000, 2),
	[STID325_FC7_PCIE_0_RESET] = SYSCFG_SRST(syscfg_fc7_hsif, 0x20000, 1),
	[STID325_FC7_PCIE_SATA_0_HARDRESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, 0x20000, 0),

	/* FC7: PCI 1 and 2 */
	[STID325_FC7_PCIE_1_MIPHY_RESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, 0x30000, 2),
	[STID325_FC7_PCIE_1_SOFTRESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, 0x30000, 1),
	[STID325_FC7_PCIE_1_HARDRESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, 0x30000, 0),

	[STID325_FC7_PCIE_2_MIPHY_RESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, 0x40000, 2),
	[STID325_FC7_PCIE_2_SOFTRESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, 0x40000, 1),
	[STID325_FC7_PCIE_2_HARDRESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, 0x40000, 0),

	/* FC8: Networking */
	[STID325_FC8_FP3_SOFTRESET] = SYSCFG_SRST(syscfg_fc8_net, 0x0, 0),
	[STID325_FC8_TELSS_SOFTRESET] = SYSCFG_SRST(syscfg_fc8_net, 0x20000, 0),

	/* FC11 Bootdev */

	/* FLASH NAND RESET */
	[STID325_FC11_FLASH_PHY_RESET] =
	    SYSCFG_SRST(syscfg_fc11_bootdev, 0x20000, 1),
	[STID325_FC11_FLASH_SUBSYSTEM_RESET] =
	    SYSCFG_SRST(syscfg_fc11_bootdev, 0x20000, 0),

	/* MMC0 RESET */
	[STID325_FC11_SDEMMC0_PHY_RESET] =
	    SYSCFG_SRST(syscfg_fc11_bootdev, 0x50000, 1),
	[STID325_FC11_SDEMMC0_HARD_RESET] =
	    SYSCFG_SRST(syscfg_fc11_bootdev, 0x50000, 0),

	/* MMC1 RESET */
	[STID325_FC11_SDEMMC1_PHY_RESET] =
	    SYSCFG_SRST(syscfg_fc11_bootdev, 0x60000, 1),
	[STID325_FC11_SDEMMC1_HARD_RESET] =
	    SYSCFG_SRST(syscfg_fc11_bootdev, 0x60000, 0),

	/* FC1 WAKEUP SS (LPM CONFIG) */
	[STID325_FC1_IRB_SOFTRESET] =
	    SYSCFG_SRST(syscfg_fc1_lpm, LPM_CONFIG_1, 6),
	[STID325_FC1_KEYSCAN_SOFTRESET] =
	    SYSCFG_SRST(syscfg_fc1_lpm, LPM_CONFIG_1, 8),
	[STID325_FC1_UART0_SOFTRESET] =
	    SYSCFG_SRST(syscfg_fc1_lpm, LPM_CONFIG_1, 11),
	[STID325_FC1_UART1_SOFTRESET] =
	    SYSCFG_SRST(syscfg_fc1_lpm, LPM_CONFIG_1, 12),
};

static struct syscfg_reset_controller_data stid325_powerdown_controller = {
	.wait_for_ack = true,
	.nr_channels = ARRAY_SIZE(stid325_powerdowns),
	.channels = stid325_powerdowns,
};

static struct syscfg_reset_controller_data stid325_softreset_controller = {
	.wait_for_ack = false,
	.active_low = true,
	.nr_channels = ARRAY_SIZE(stid325_softresets),
	.channels = stid325_softresets,
};

static struct of_device_id stid325_reset_match[] = {
	{.compatible = "st,stid325-powerdown",
	 .data = &stid325_powerdown_controller,},
	{.compatible = "st,stid325-softreset",
	 .data = &stid325_softreset_controller,},
	{},
};

static struct platform_driver stid325_reset_driver = {
	.probe = syscfg_reset_probe,
	.driver = {
		   .name = "reset-stid325",
		   .owner = THIS_MODULE,
		   .of_match_table = stid325_reset_match,
		   },
};

static int __init stid325_reset_init(void)
{
	return platform_driver_register(&stid325_reset_driver);
}

arch_initcall(stid325_reset_init);
