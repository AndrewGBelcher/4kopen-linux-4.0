/*
 * Copyright (C) 2014 STMicroelectronics (R&D) Limited
 * Author: Alexandre Torgue <alexandre.torgue@st.com>
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

#include <dt-bindings/reset-controller/stih390-resets.h>

#include "reset-syscfg.h"

static const char syscfg_fc0_backbone[] = "st,stih390-syscfg-fc0-backbone";
static const char syscfg_fc1_vsafe[] = "st,stih390-syscfg-fc1-vsafe";
static const char syscfg_fc2_wifi[] = "st,stih390-syscfg-fc2-wifi";
static const char syscfg_fc3_vdec[] = "st,stih390-syscfg-fc3-vdec";
static const char syscfg_fc6_display[] = "st,stih390-syscfg-fc6-display";
static const char syscfg_fc7_hsif[] = "st,stih390-syscfg-fc7-hsif";
static const char syscfg_fc9_st231ss[] = "st,stih390-syscfg-fc9-st231ss";
static const char syscfg_fc10_gpu[] = "st,stih390-syscfg-fc10-gpu";
static const char syscfg_fc11_bootdev[] = "st,stih390-syscfg-fc11-bootdev";
static const char syscfg_fc13_ts[] = "st,stih390-syscfg-fc13-ts";

/*
 * Powerdown is controlled by clk_en bit in the CLK_RST system config register.
 * This means that there is no powerdown_req but the powerdown_ack can be
 * available from the specific area for some IPs.
 * By default clk_en is set.
 */
#define STIH390_PDN_FC7(_bit, clk_rst_reg, specific_status_reg, _stat) \
	_SYSCFG_RST_CH(syscfg_fc7_hsif, clk_rst_reg, _bit, \
		       specific_status_reg, _stat)

/* PowerDown definitions */
static const struct syscfg_reset_channel_data stih390_powerdowns[] = {
	/* FC7 HSIF */
	[STIH390_FC7_USB3_0_POWERDOWN_REQ] =
	    STIH390_PDN_FC7(0, SYSCFG_7000, SYSCFG_7478, 8),
	[STIH390_FC7_USB2_0_POWERDOWN_REQ] =
	    STIH390_PDN_FC7(0, SYSCFG_7010, SYSCFG_7478, 6),
	[STIH390_FC7_GMAC0_POWERDOWN_REQ] =
	    STIH390_PDN_FC7(0, SYSCFG_7411, SYSCFG_7456, 2),
};

/* Reset definitions. */
static const struct syscfg_reset_channel_data stih390_softresets[] = {

	/* FC0 BACKBONE */
	[STIH390_FC0_A9_SOFTRESET]
	     SYSCFG_SRST(syscfg_fc0_backbone, SYSCFG_0000, 0),

	/* FC1 VSAFE */
	[STIH390_FC1_CM4_HRESET] =
	     SYSCFG_SRST(syscfg_fc1_vsafe, SYSCFG_1000, 0),
	[STIH390_FC1_CM4_SYSRESET] =
	     SYSCFG_SRST(syscfg_fc1_vsafe, SYSCFG_1000, 1),
	[STIH390_FC1_WAKEUP_0_RESET] =
	     SYSCFG_SRST(syscfg_fc1_vsafe, SYSCFG_1010, 0),
	[STIH390_FC1_PIO_0_RESET] =
	     SYSCFG_SRST(syscfg_fc1_vsafe, SYSCFG_1030, 0),
	[STIH390_FC1_THS_1_RESET] =
	     SYSCFG_SRST(syscfg_fc1_vsafe, SYSCFG_1060, 0),

	/* FC2 WIFI */
	[STIH390_FC2_GMAC_W_0_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc2_wifi, SYSCFG_2000, 0),
	[STIH390_FC2_WIFI_TOP_WRAPPER_SS_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc2_wifi, SYSCFG_2010, 0),

	/* FC3 VDEC */
	[STIH390_FC3_C8VDEC_TOP_0_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc3_vdec, SYSCFG_3000, 0),
	[STIH390_FC3_C8HAD_PP_SUPERTOP_0_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc3_vdec, SYSCFG_3001, 0),
	[STIH390_FC3_C8JPG_TOP_0_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc3_vdec, SYSCFG_3002, 0),
	[STIH390_FC3_ST231_DMU0_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc3_vdec, SYSCFG_3003, 0),
	[STIH390_FC3_MBX_DMU0_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc3_vdec, SYSCFG_3004, 0),

	/* FC 4,5 RESERVED */

	/* FC6 DISPLAY */
	[STIH390_FC6_BDISP_0_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc6_display, SYSCFG_6000, 0),
	[STIH390_FC6_HQVDP_0_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc6_display, SYSCFG_6001, 0),
	[STIH390_FC6_VDP_AUX_0_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc6_display, SYSCFG_6002, 0),
	[STIH390_FC6_COMPO_0_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc6_display, SYSCFG_6003, 0),
	[STIH390_FC6_HDTVOUT_0_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc6_display, SYSCFG_6004, 0),
	[STIH390_FC6_HDMI_TX_PHY_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc6_display, SYSCFG_6005, 0),
	[STIH390_FC6_DVPIN_0_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc6_display, SYSCFG_6006, 0),
	[STIH390_FC6_HDMI_RX_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc6_display, SYSCFG_6007, 0),
	[STIH390_FC6_FDMA_0_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc6_display, SYSCFG_6008, 0),
	[STIH390_FC6_UPLAYER_1_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc6_display, SYSCFG_6009, 0),
	[STIH390_FC6_UREADER_1_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc6_display, SYSCFG_6010, 0),
	[STIH390_FC6_FDMA_1_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc6_display, SYSCFG_6011, 0),
	[STIH390_FC6_UPLAYER_2_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc6_display, SYSCFG_6012, 0),
	[STIH390_FC6_UPLAYER_3_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc6_display, SYSCFG_6013, 0),

	/* FC7 HSIF */
	[STIH390_FC7_USB3_PHY_USB2_CALIB_RESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, SYSCFG_7000, 3),
	[STIH390_FC7_USB3_PHY_USB2_RESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, SYSCFG_7000, 2),
	[STIH390_FC7_USB3_RESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, SYSCFG_7000, 0),
	[STIH390_FC7_USB2_PHY_CALIB_RESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, SYSCFG_7010, 2),
	[STIH390_FC7_USB2_PHY_RESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, SYSCFG_7010, 1),
	[STIH390_FC7_USB2_RESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, SYSCFG_7010, 0),
	[STIH390_FC7_GMAC_SOFTRESET] =
	    SYSCFG_SRST(syscfg_fc7_hsif, SYSCFG_7060, 0),

	/* FC 8 RESERVED */

	/* FC9 ST231SS */
	[STIH390_FC9_ST231_SEC_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc9_st231ss, SYSCFG_9000, 0),
	[STIH390_FC9_ST231_GP0_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc9_st231ss, SYSCFG_9010, 0),
	[STIH390_FC9_ST231_GP1_SOFTRESET] =
	     SYSCFG_SRST(syscfg_fc9_st231ss, SYSCFG_9020, 0),

	/* FC10 GPU */
	[STIH390_FC10_MALI400_0_HARDRESET] =
	     SYSCFG_SRST(syscfg_fc10_gpu, SYSCFG_10000, 0),
	[STIH390_FC10_THS_GLUE_GPU_HARDRESET] =
	     SYSCFG_SRST(syscfg_fc10_gpu, SYSCFG_10010, 0),

	/* FC11 BOOTDEV */
	[STIH390_FC11_FLASHSS_BOOT_0_HARDRESET] =
	    SYSCFG_SRST(syscfg_fc11_bootdev, SYSCFG_11003, 0),
	[STIH390_FC11_FLASH_PHY_0_SOFTRESET] =
	    SYSCFG_SRST(syscfg_fc11_bootdev, SYSCFG_11003, 1),
	[STIH390_FC11_FLASH_PIOMUX_0_SOFTRESET] =
	    SYSCFG_SRST(syscfg_fc11_bootdev, SYSCFG_11004, 0),
	[STIH390_FC11_FLASH_BANK_PIO_0_SOFTRESET] =
	    SYSCFG_SRST(syscfg_fc11_bootdev, SYSCFG_11005, 0),
	[STIH390_FC11_SDEMMC_0_SOFTRESET] =
	    SYSCFG_SRST(syscfg_fc11_bootdev, SYSCFG_11006, 0),

	/* FC12 RESERVED */

	/* FC13 TS */
	[STIH390_FC13_STFE_0_SOFTRESET] =
	    SYSCFG_SRST(syscfg_fc13_ts, SYSCFG_13000, 0),

	/* FC14 RESERVED */

	/* FC15 A9 : NO softreset for FC15*/
};

static struct syscfg_reset_controller_data stih390_powerdown_controller = {
	.wait_for_ack = true,
	.nr_channels = ARRAY_SIZE(stih390_powerdowns),
	.channels = stih390_powerdowns,
};

static struct syscfg_reset_controller_data stih390_softreset_controller = {
	.wait_for_ack = false,
	.active_low = true,
	.nr_channels = ARRAY_SIZE(stih390_softresets),
	.channels = stih390_softresets,
};

static struct of_device_id stih390_reset_match[] = {
	{.compatible = "st,stih390-powerdown",
	 .data = &stih390_powerdown_controller,},
	{.compatible = "st,stih390-softreset",
	 .data = &stih390_softreset_controller,},
	{},
};

static struct platform_driver stih390_reset_driver = {
	.probe = syscfg_reset_probe,
	.driver = {
		   .name = "reset-stih390",
		   .owner = THIS_MODULE,
		   .of_match_table = stih390_reset_match,
		   },
};

static int __init stih390_reset_init(void)
{
	return platform_driver_register(&stih390_reset_driver);
}

arch_initcall(stih390_reset_init);
