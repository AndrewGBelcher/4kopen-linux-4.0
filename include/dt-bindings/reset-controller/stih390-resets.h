/*
 * This header provides constants for the reset controller
 * based peripheral powerdown requests on the STMicroelectronics
 * STIH390 SoC.
 */
#ifndef _DT_BINDINGS_RESET_CONTROLLER_STIH390
#define _DT_BINDINGS_RESET_CONTROLLER_STIH390

/* Sysconf registers */

/* FC0 BACKBONE */
#define SYSCFG_0000	0x0		/* A9 software reset */

/* FC1 VSAFE */
#define SYSCFG_1000	0x0		/* reset of ARMM4 sub-system */
#define SYSCFG_1010	0x10000		/* reset of Wake up peripherals */
#define SYSCFG_1030	0x30000		/* reset of 10bank PIO */
#define SYSCFG_1060	0x70000		/* reset of thermal sensor logic */

/* FC2 WIFI */
#define SYSCFG_2000	0x0		/* GMAC_W soft reset control */
#define SYSCFG_2010	0x10000		/* WIFI soft reset control */

/* FC3 VDEC */
#define SYSCFG_3000	0x0		/* Controls C8VDEC_TOP_0 IP resets */
#define SYSCFG_3001	0x10000		/* Controls PP IP  resets */
#define SYSCFG_3002	0x20000		/* Controls JPGDEG  IP resets */
#define SYSCFG_3003	0x30000		/* Controls ST231_DMU0 IP resets */
#define SYSCFG_3004	0x40000		/* Controls MailBox IP resets */

/* FC6 DISPLAY */
#define SYSCFG_6000	0x0		/* HQVDP_0 resets */
#define SYSCFG_6001	0x10000		/* VDP AUX resets */
#define SYSCFG_6002	0x20000		/* Compo resets */
#define SYSCFG_6003	0x30000		/* HDTVOUT resets  */
#define SYSCFG_6004	0x40000		/* FDMA_0 resets */
#define SYSCFG_6005	0x50000		/* FDMA_1 resets */
#define SYSCFG_6006	0x60000		/* Uniperipheral players 1 resets */
#define SYSCFG_6007	0x70000		/* Uniperipheral players 2 resets */
#define SYSCFG_6008	0x80000		/* Uniperipheral players 3 resets */
#define SYSCFG_6009	0x90000		/* Uniperipheral readers resets */
#define SYSCFG_6010	0xA0000		/* BDISP resets */
#define SYSCFG_6011	0xB0000		/* HDMI RX resets */
#define SYSCFG_6012	0xC0000		/* HDMI Rx PHY resets */
#define SYSCFG_6013	0xD0000		/* DVP resets */
#define SYSCFG_6014	0xE0000		/* HDMI Tx PHY resets */

/* FC7 HSIF */
#define SYSCFG_7000	0x0		/* Control USB3_0 hard resets */
#define SYSCFG_7010	0x10000		/* Control USB2_0 hard resets */
#define SYSCFG_7060	0x60000		/* GMAC_S_0 soft reset control */
#define SYSCFG_7411	0x13002c	/* GMAC0 CTRL */
#define SYSCFG_7456	0x1300e0	/* GMAC0 POWER_DOWN_STATUS  */
#define SYSCFG_7478	0x130138	/* High Speed Links powerdown status */

/* FC9 ST231SS */
#define SYSCFG_9000	0x0		/* Control ST231 SECURE rese */
#define SYSCFG_9010	0x10000		/* Control ST231 GP 0 reset */
#define SYSCFG_9020	0x20000		/* Control ST231 GP 1 reset */

/* FC10 GPU */
#define SYSCFG_10000	0x0		/* MALI400 IP main hard reset */
#define SYSCFG_10010	0x10000		/* Thermal Sensor main hard rerset */

/* FC11 BOOTDEV */
#define SYSCFG_11003	0x20000		/* Control flashss hard resets */
#define SYSCFG_11004	0x30000		/* Control flash piomux hard resets */
#define SYSCFG_11005	0x40000		/* flash bank pio hard resets */
#define SYSCFG_11006	0x50000		/* Control SD3.0/SDIO hard resets */
#define SYSCFG_11008	0x70000		/* Control flashss nand clock enable */
#define SYSCFG_11009	0x80000		/* Control flashss spi clock enable */
#define SYSCFG_11010	0x90000		/* Control flashss emmc clock enable */

/* FC13 TS */
#define SYSCFG_13000	0x0		/*  Controls STFE_0 IP resets*/
#define SYSCFG_13008	0x80000		/* Controls stbe_0 ip clock gating */

/* FC15 ARMCores */
#define SYSCFG_15000	0x0		/* Net axi interconnect clock gating */
#define SYSCFG_15001	0x10000		/* APB/AXI clock gating*/
#define SYSCFG_15002	0x20000		/* ATB clock gating */
#define SYSCFG_15003	0x30000		/* Traceclkin clock gating */
#define SYSCFG_15004	0x40000		/* STBus clock gating */

/* Reset defines */

/* FC0 BACKBONE */
#define STIH390_FC0_A9_SOFTRESET		0
/* FC1 VSAFE */
#define STIH390_FC1_CM4_HRESET			1
#define STIH390_FC1_CM4_SYSRESET		2
#define STIH390_FC1_WAKEUP_0_RESET		3
#define STIH390_FC1_PIO_0_RESET			4
#define STIH390_FC1_THS_1_RESET			5
/* FC2 WIFI */
#define STIH390_FC2_GMAC_W_0_SOFTRESET		6
#define STIH390_FC2_WIFI_TOP_WRAPPER_SS_SOFTRESET 7
/* FC3 VDEC */
#define STIH390_FC3_C8VDEC_TOP_0_SOFTRESET	8
#define STIH390_FC3_C8HAD_PP_SUPERTOP_0_SOFTRESET 9
#define STIH390_FC3_C8JPG_TOP_0_SOFTRESET	10
#define STIH390_FC3_ST231_DMU0_SOFTRESET	11
#define STIH390_FC3_MBX_DMU0_SOFTRESET		12
/* FC6 DISPLAY */
#define STIH390_FC6_BDISP_0_SOFTRESET		13
#define STIH390_FC6_HQVDP_0_SOFTRESET		14
#define STIH390_FC6_VDP_AUX_0_SOFTRESET		15
#define STIH390_FC6_COMPO_0_SOFTRESET		16
#define STIH390_FC6_HDTVOUT_0_SOFTRESET		17
#define STIH390_FC6_HDMI_TX_PHY_SOFTRESET	18
#define STIH390_FC6_DVPIN_0_SOFTRESET		19
#define STIH390_FC6_HDMI_RX_SOFTRESET		20
#define STIH390_FC6_FDMA_0_SOFTRESET		21
#define STIH390_FC6_UPLAYER_1_SOFTRESET		22
#define STIH390_FC6_UREADER_1_SOFTRESET		23
#define STIH390_FC6_FDMA_1_SOFTRESET		24
#define STIH390_FC6_UPLAYER_2_SOFTRESET		25
#define STIH390_FC6_UPLAYER_3_SOFTRESET		26
/* FC7 HSIF */
#define STIH390_FC7_USB3_PHY_USB2_CALIB_RESET	27
#define STIH390_FC7_USB3_PHY_USB2_RESET		28
#define STIH390_FC7_USB3_RESET			29
#define STIH390_FC7_USB2_PHY_CALIB_RESET	30
#define STIH390_FC7_USB2_PHY_RESET		31
#define STIH390_FC7_USB2_RESET			32
#define STIH390_FC7_GMAC_SOFTRESET		33
/* FC9 ST231SS */
#define STIH390_FC9_ST231_SEC_SOFTRESET		34
#define STIH390_FC9_ST231_GP0_SOFTRESET		35
#define STIH390_FC9_ST231_GP1_SOFTRESET		36
/* FC10 GPU */
#define STIH390_FC10_MALI400_0_HARDRESET	37
#define STIH390_FC10_THS_GLUE_GPU_HARDRESET	38
/* FC11 Bootdev */
#define STIH390_FC11_FLASHSS_BOOT_0_HARDRESET	39
#define STIH390_FC11_FLASH_PHY_0_SOFTRESET	40
#define STIH390_FC11_FLASH_PIOMUX_0_SOFTRESET	41
#define STIH390_FC11_FLASH_BANK_PIO_0_SOFTRESET	42
#define STIH390_FC11_SDEMMC_0_SOFTRESET		43
/* FC13 TS */
#define STIH390_FC13_STFE_0_SOFTRESET		44

/* PowerDown defines */

/* FC7 HSIF */
#define STIH390_FC7_USB3_0_POWERDOWN_REQ	0
#define STIH390_FC7_USB2_0_POWERDOWN_REQ	1
#define STIH390_FC7_GMAC0_POWERDOWN_REQ		2

#endif /* _DT_BINDINGS_RESET_CONTROLLER_STIH390 */
