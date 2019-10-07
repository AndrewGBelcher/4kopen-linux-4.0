/*
 * OS independant Validation clock driver
 * PLL & FS parameters computation algorithms
 * 2012-15, F. Charpentier, UPD Functional Validation
 */

#ifndef __VCLK_ALGOS_H
#define __VCLK_ALGOS_H

#ifdef __cplusplus
extern "C" {
#endif

enum vclk_pll_type {
	VCLK_PLL800C65,
	VCLK_PLL1200C32,
	VCLK_PLL1600C45,
	VCLK_PLL1600C45PHI,
	VCLK_PLL1600C65,
	VCLK_PLL3200C28,    /* Supports FRACTIONAL mode */
	VCLK_PLL3200C32,
	VCLK_PLL4600C28,
};

struct vclk_pll {
	enum vclk_pll_type type;

	unsigned short mdiv;
	unsigned short ndiv;
	unsigned short pdiv;

	unsigned short odf;
	unsigned short idf;
	unsigned short ldf;
	unsigned short cp;
	unsigned short frac;    /* FRAC_INPUT */

	char no_limits;         /* Disable out of range check */
	char mode;              /* 0=integer (default), 1=fractional */
};

enum vclk_fs_type {
	VCLK_FS216C65,
	VCLK_FS432C65,
	VCLK_FS660C28VCO,   /* VCO out */
	VCLK_FS660C28,      /* DIG out */
	VCLK_FS660C32VCO,   /* VCO out */
	VCLK_FS660C32       /* DIG out */
};

struct vclk_fs {
	enum vclk_fs_type type;

	unsigned long ndiv;
	unsigned long mdiv;
	unsigned long pe;
	unsigned long sdiv;
	unsigned long nsdiv;

	char no_limits;      /* Disable out of range check */
};

/* ========================================================================
   Name:    vclk_pll_get_params()
   Description: Freq to parameters computation for PLLs
   Input:       input=input freq (Hz), output=output freq (Hz)
   Output:      updated 'struct vclk_pll *pll'
   Return:      0=OK, -1=ERROR
   ======================================================================== */

int vclk_pll_get_params(unsigned long input, unsigned long output,
	struct vclk_pll *pll);

int vclk_pll_get_rate(unsigned long input, struct vclk_pll *pll,
	unsigned long *output);

/* ========================================================================
   Name:    vclk_fs_get_params()
   Description: Freq to parameters computation for FSs
   Input:       input=input freq (Hz), output=output freq (Hz)
   Output:      updated 'struct vclk_fs *fs'
   Return:      0=OK, -1=ERROR
   ======================================================================== */

int vclk_fs_get_params(unsigned long input, unsigned long output,
	struct vclk_fs *fs);

int vclk_fs_get_rate(unsigned long input, struct vclk_fs *fs,
	unsigned long *output);

/* ========================================================================
   Name:        clk_best_div
   Description: Returned closest div factor
   Returns:     Best div factor
   ======================================================================== */

static inline unsigned long
vclk_best_div(unsigned long parent_rate, unsigned long rate)
{
	unsigned long div;

	if (rate > parent_rate)
		div = 1;
	else
		div = parent_rate / rate +
			((rate > (2 * (parent_rate % rate))) ? 0 : 1);
	return div;
}

#ifdef __cplusplus
}
#endif

#endif /* #ifndef __VCLK_ALGOS_H */
