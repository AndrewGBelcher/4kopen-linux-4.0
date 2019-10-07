/*
 * OS independant validation clock driver
 * PLL & FS parameters computation algorithms
 * 2014-15, F. Charpentier, UPD Functional Validation
 */

/* ----- Modification history (most recent first)----
23/jul/15 fabrice.charpentier@st.com
    PLL4600C28 revisited for better reliability (only integer mode so far)
01/jun/15 fabrice.charpentier@st.com
    FS660C28 enhanced & checked vs several CannesWifi cases (30 & 40Mhz input)
11/may/15 fabrice.charpentier@st.com
    Added FS660C28 prelim support + FS660C32 checks
06/jan/15 fabrice.charpentier@st.com
    Resync with boot firmware algos + vclk_pll1600c45_get_params waring fix.
29/jan/14 fabrice.charpentier@st.com
    Added PLL4600C28 support. vclk_pll3200c32_get_rate() fix for IDF=0.
18/sep/13 fabrice.charpentier@st.com
    vclk_fs660c32_vco_get_rate() out of range checks.
03/jul/13 fabrice.charpentier@st.com
    vclk_fs216c65_get_params() bug fix; checks if output is 0
30/oct/12 fabrice.charpentier@st.com
    FS660C32 migrated to new API.
29/oct/12 fabrice.charpentier@st.com
    FS216C65 & FS432C65 migrated to new API. FS216 enhanced to support
    variable NSDIV.
21/sep/12 fabrice.charpentier@st.com, francesco.virlinzi@st.com
    New API for 800C65, 1200C32, 1600C45, 1600C65 & 3200C32 PLLs.
04/sep/12 fabrice.charpentier@st.com
    vclk_pll1600c65_get_rate(); now support IDF=0
28/aug/12 fabrice.charpentier@st.com
    PLL1600C45: get_rate() & get_phi_rate() now using 64bit div.
21/aug/12 fabrice.charpentier@st.com
    PLL800C65 revisited for better precision & win32 compilation support.
    Using 64bit div now.
07/aug/12 fabrice.charpentier@st.com
    FS660C32 enhancement. Better result for low freqs.
    FS660C32 & FS432C65 NSDIV search loop & order change.
    vclk_pll1200c32_get_rate(): added support for "odf==0" & "idf==0".
30/jul/12 fabrice.charpentier@st.com
    vclk_pll3200c32_get_params() & vclk_pll1600c45_get_params()
      charge pump computation bug fixes.
28/jun/12 fabrice.charpentier@st.com
    vclk_fs432c65_get_params() bug fix for 64bits division under Linux
28/jun/12 fabrice.charpentier@st.com
    vclk_fs216c65_get_params() bug fix for 64bits division under Linux.
19/jun/12 Ravinder SINGH
    vclk_pll1600c45_get_phi_params() fix.
30/apr/12 fabrice.charpentier@st.com
    FS660C32 fine tuning to get better result.
26/apr/12 fabrice.charpentier@st.com
    FS216 & FS432 fine tuning to get better result.
24/apr/12 fabrice.charpentier@st.com
    FS216, FS432 & FS660: changed sdiv search order from highest to lowest
      as recommended by Anand K.
18/apr/12 fabrice.charpentier@st.com
    Added FS432C65 algo.
13/apr/12 fabrice.charpentier@st.com
    FS216C65 MD order changed to recommended -16 -> -1 then -17.
05/apr/12 fabrice.charpentier@st.com
    FS216C65 fully revisited to have 1 algo only for Linux & OS21.
28/mar/12 fabrice.charpentier@st.com
    FS660C32 algos merged from Liege required improvements.
25/nov/11 fabrice.charpentier@st.com
    Functions rename to support several algos for a same PLL/FS.
28/oct/11 fabrice.charpentier@st.com
    Added PLL1600 CMOS045 support for Lille
27/oct/11 fabrice.charpentier@st.com
    PLL1200 functions revisited. API changed.
27/jul/11 fabrice.charpentier@st.com
    FS660 algo enhancement.
14/mar/11 fabrice.charpentier@st.com
    Added PLL1200 functions.
07/mar/11 fabrice.charpentier@st.com
    vclk_pll3200c32_get_params() revisited.
11/mar/10 fabrice.charpentier@st.com
    vclk_pll800c65_get_params() fully revisited.
10/dec/09 francesco.virlinzi@st.com
    vclk_pll1600c65_get_params() now same code for OS21 & Linux.
13/oct/09 fabrice.charpentier@st.com
    vclk_fs216c65_get_rate() API changed. Now returns error code.
30/sep/09 fabrice.charpentier@st.com
    Introducing vclk_pll800c65_get_rate() & vclk_pll1600c65_get_rate() to
      replace vclk_pll800_freq() & vclk_pll1600c65_freq().
*/

#include "vclk_algos.h"
#include <linux/types.h>
#include <linux/math64.h>

/*
 * Prototypes for local functions
 */

#define P15 (uint64_t)(1 << 15) /* 2 power 15 */
#define P16 (uint64_t)(1 << 16) /* 2 power 16 */
#define P17 (uint64_t)(1 << 17) /* 2 power 17 */
#define P20 (uint64_t)(1 << 20) /* 2 power 20 */

/*
 * PLL800
 */

/* ========================================================================
   Name:    vclk_pll800c65_get_params()
   Description: Freq to parameters computation for PLL800 CMOS65
   Input:       input & output freqs (Hz)
   Output:      updated *mdiv, *ndiv & *pdiv (register values)
   Return:      0=OK, -1=ERROR
   ======================================================================== */

/*
 * PLL800 in FS mode computation algo
 *
 *           2 * NDIV * Fin
 * Fout = -----------------     using FS MODE
 *            MDIV * PDIV
 *
 * Rules:
 *   6.25Mhz <= output <= 800Mhz
 *   FS mode means 3 <= NDIV <= 255
 *   1 <= MDIV <= 255
 *   PDIV: 1, 2, 4, 8, 16 or 32
 *   1Mhz <= INFIN (input/MDIV) <= 50Mhz
 *   200Mhz <= FVCO (input*2*N/M) <= 800Mhz
 *   For better long term jitter select M minimum && P maximum
 */

static int vclk_pll800c65_get_params(unsigned long input,
			unsigned long output, struct vclk_pll *pll)
{
	unsigned long m, n, p, pi, infin;
	unsigned long deviation = 0xffffffff;
	unsigned long new_freq;
	long new_deviation;
	uint64_t res;
	struct vclk_pll temp_pll = {
		.type = pll->type,
	};

	/* Output clock range: 6.25Mhz to 800Mhz */
	if (!pll->no_limits)
		if (output < 6250000 || output > 800000000)
			return -1;

	for (m = 1; (m < 255) && deviation; m++) {
		infin = input / m; /* 1Mhz <= INFIN <= 50Mhz */
		if (infin < 1000000 || infin > 50000000)
			continue;

		for (pi = 5; pi <= 5 && deviation; pi--) {
			p = 1 << pi;
			res = (uint64_t)m * (uint64_t)p * (uint64_t)output;
			n = (unsigned long)div64_u64(res, input * 2);

			/* Checks */
			if (n < 3)
				break;
			if (n > 255)
				continue;

			/* 200Mhz <= FVCO <= 800Mhz */
			res = div64_u64(res, m);
			if (res > 800000000)
				continue;
			if (res < 200000000)
				break;

			temp_pll.mdiv = m;
			temp_pll.ndiv = n;
			temp_pll.pdiv = pi;
			vclk_pll_get_rate(input, &temp_pll, &new_freq);
			new_deviation = new_freq - output;
			if (new_deviation < 0)
				new_deviation = -new_deviation;
			if (new_deviation < deviation) {
				pll->mdiv = m;
				pll->ndiv = n;
				pll->pdiv = pi;
				deviation = new_deviation;
			}
		}
	}

	if (deviation == 0xffffffff) /* No solution found */
		return -1;
	return 0;
}

/* ========================================================================
   Name:    vclk_pll800c65_get_rate()
   Description: Convert input/mdiv/ndiv/pvid values to frequency for PLL800
   Params:      'input' freq (Hz), mdiv/ndiv/pvid values
   Output:      '*rate' updated
   Return:      Error code.
   ======================================================================== */

static int vclk_pll800c65_get_rate(unsigned long input, struct vclk_pll *pll,
			unsigned long *rate)
{
	uint64_t res;
	unsigned long mdiv = pll->mdiv;

	if (!mdiv)
		mdiv++;	/* mdiv=0 or 1 => MDIV=1 */

	res = (uint64_t)2 * (uint64_t)input * (uint64_t)pll->ndiv;
	*rate = (unsigned long)div64_u64(res, mdiv * (1 << pll->pdiv));

	return 0;
}

/*
 * PLL1200
 */

/* ========================================================================
   Name:    vclk_pll1200c32_get_rate()
   Description: Convert input/idf/ldf/odf values to PHI output freq.
		WARNING: Assuming NOT BYPASS.
   Params:      'input' freq (Hz), idf/ldf/odf REGISTERS values
   Output:      '*rate' updated with value of PHI output (FVCO/ODF).
   Return:      Error code.
   ======================================================================== */

static int vclk_pll1200c32_get_rate(unsigned long input, struct vclk_pll *pll,
			unsigned long *rate)
{
	unsigned long idf = pll->idf, odf = pll->odf;

	if (!idf) /* idf==0 means 1 */
		idf = 1;
	if (!odf) /* odf==0 means 1 */
		odf = 1;

	/* Note: input is divided by 1000 to avoid overflow */
	*rate = (((input / 1000) * pll->ldf) / (odf * idf)) * 1000;

	return 0;
}

/* ========================================================================
   Name:    vclk_pll1200c32_get_params()
   Description: PHI freq to parameters computation for PLL1200.
   Input:       input=input freq (Hz),output=output freq (Hz)
		WARNING: Output freq is given for PHI (FVCO/ODF).
   Output:      updated *idf, *ldf, & *odf
   Return:      0=OK, -1=ERROR
   ======================================================================== */

/* PLL output structure
 *   FVCO >> Divider (ODF) >> PHI
 *
 * PHI = (INFF * LDF) / (ODF * IDF) when BYPASS = L
 *
 * Rules:
 *   9.6Mhz <= input (INFF) <= 350Mhz
 *   600Mhz <= FVCO <= 1200Mhz
 *   9.52Mhz <= PHI output <= 1200Mhz
 *   1 <= i (register value for IDF) <= 7
 *   8 <= l (register value for LDF) <= 127
 *   1 <= odf (register value for ODF) <= 63
 */

static int vclk_pll1200c32_get_params(unsigned long input, unsigned long output,
			struct vclk_pll *pll)
{
	unsigned long i, l, o; /* IDF, LDF, ODF values */
	unsigned long deviation = 0xffffffff;
	unsigned long new_freq;
	long new_deviation;

	/* Output clock range: 9.52Mhz to 1200Mhz */
	if (!pll->no_limits)
		if (output < 9520000 || output > 1200000000)
			return -1;

	/* Computing Output Division Factor */
	if (output < 600000000) {
		o = 600000000 / output;
		if (600000000 % output)
			o = o + 1;
	} else {
		o = 1;
	}

	input /= 1000;
	output /= 1000;

	for (i = 1; (i <= 7) && deviation; i++) {
		l = i * output * o / input;

		/* Checks */
		if (l < 8)
			continue;
		if (l > 127)
			break;

		new_freq = (input * l) / (i * o);
		new_deviation = new_freq - output;
		if (new_deviation < 0)
			new_deviation = -new_deviation;
		if (!new_deviation || new_deviation < deviation) {
			pll->idf = i;
			pll->ldf = l;
			pll->odf = o;
			deviation = new_deviation;
		}
	}

	if (deviation == 0xffffffff) /* No solution found */
		return -1;
	return 0;
}

/*
 * PLL1600
 * WARNING: 2 types currently supported; CMOS065 & CMOS045
 */

/* ========================================================================
   Name:    vclk_pll1600c45_get_rate()
   Description: Convert input/idf/ndiv REGISTERS values to FVCO frequency
   Params:      'input' freq (Hz), idf/ndiv REGISTERS values
   Output:      '*rate' updated with value of FVCO output.
   Return:      Error code.
   ======================================================================== */

static int vclk_pll1600c45_get_rate(unsigned long input, struct vclk_pll *pll,
			unsigned long *rate)
{
	unsigned long idf = pll->idf;
	uint64_t res;

	if (!idf)
		idf = 1;

	/* FVCO = (INFF*LDF) / (IDF)
	   LDF = 2*NDIV (if FRAC_CONTROL=L)
	   => FVCO = INFF * 2 * NDIV / IDF */

	res = (uint64_t)input * (uint64_t)2 * (uint64_t)pll->ndiv;
	*rate = (unsigned long)div64_u64(res, idf);

	return 0;
}

/* ========================================================================
   Name:    vclk_pll1600c45_get_params(), PL1600 CMOS45
   Description: FVCO output freq to parameters computation function.
   Input:       input,output=input/output freqs (Hz)
   Output:      updated *idf, *ndiv and *cp
   Return:      0=OK, -1=ERROR
   ======================================================================== */

/*
 * Spec used: CMOS045_PLL_PG_1600X_A_SSCG_FR_LSHOD25_7M4X0Y2Z_SPECS_1.1.pdf
 *
 * Rules:
 *   4Mhz <= input (INFF) <= 350Mhz
 *   800Mhz <= VCO freq (FVCO) <= 1800Mhz
 *   6.35Mhz <= output (PHI) <= 900Mhz
 *   1 <= IDF (Input Div Factor) <= 7
 *   8 <= NDIV (Loop Div Factor) <= 225
 *   1 <= ODF (Output Div Factor) <= 63
 *
 * PHI = (INFF*LDF) / (2*IDF*ODF)
 * FVCO = (INFF*LDF) / (IDF)
 * LDF = 2*NDIV (if FRAC_CONTROL=L)
 * => FVCO = INFF * 2 * NDIV / IDF
 */

static int vclk_pll1600c45_get_params(unsigned long input, unsigned long output,
		struct vclk_pll *pll)
{
	unsigned long i, n;	/* IDF, NDIV values */
	unsigned long deviation = 0xffffffff, new_deviation;
	unsigned long new_freq;
	/* Charge pump table: highest ndiv value for cp=7 to 27 */
	static const unsigned char cp_table[] = {
		71, 79, 87, 95, 103, 111, 119, 127, 135, 143,
		151, 159, 167, 175, 183, 191, 199, 207, 215,
		223, 225
	};

	/* Output clock range: 800Mhz to 1800Mhz */
	if (!pll->no_limits)
		if (output < 800000000 || output > 1800000000)
			return -1;

	input /= 1000;
	output /= 1000;

	for (i = 1; (i <= 7) && deviation; i++) {
		n = (i * output) / (2 * input);

		/* Checks */
		if (n < 8)
			continue;
		if (n > 225)
			break;

		new_freq = (input * 2 * n) / i;
		if (new_freq >= output)
			new_deviation = new_freq - output;
		else
			new_deviation = output - new_freq;
		if (!new_deviation || new_deviation < deviation) {
			pll->idf    = i;
			pll->ndiv   = n;
			deviation = new_deviation;
		}
	}

	if (deviation == 0xffffffff) /* No solution found */
		return -1;

	/* Computing recommended charge pump value */
	for (pll->cp = 7; pll->ndiv > cp_table[pll->cp - 7]; (pll->cp)++)
		;

	return 0;
}

/* ========================================================================
   Name:    vclk_pll1600c45_get_phi_rate()
   Description: Convert input/idf/ndiv/odf REGISTERS values to frequency
   Params:      'input' freq (Hz), idf/ndiv/odf REGISTERS values
   Output:      '*rate' updated with value of PHI output.
   Return:      Error code.
   ======================================================================== */

static int vclk_pll1600c45_get_phi_rate(unsigned long input,
		struct vclk_pll *pll, unsigned long *rate)
{
	unsigned long idf = pll->idf, odf = pll->odf;
	uint64_t res;

	if (!idf)
		idf = 1;
	if (!odf)
		odf = 1;

	/* PHI = (INFF*LDF) / (2*IDF*ODF)
	   LDF = 2*NDIV (if FRAC_CONTROL=L)
	   => PHI = (INFF*NDIV) / (IDF*ODF) */

	res = (uint64_t)input * (uint64_t)pll->ndiv;
	*rate = (unsigned long)div64_u64(res, idf * odf);

	return 0;
}

/* ========================================================================
   Name:    vclk_pll1600c45_get_phi_params()
   Description: PLL1600 C45 PHI freq computation function
   Input:       input,output=input/output freqs (Hz)
   Output:      updated *idf, *ndiv, *odf and *cp
   Return:      0=OK, -1=ERROR
   ======================================================================== */

static int vclk_pll1600c45_get_phi_params(unsigned long input,
		unsigned long output, struct vclk_pll *pll)
{
	unsigned long o; /* ODF value */

	/* Output clock range: 6.35Mhz to 900Mhz */
	if (!pll->no_limits)
		if (output < 6350000 || output > 900000000)
			return -1;

	/* Computing Output Division Factor */
	if (output < 400000000) {
		o = 400000000 / output;
		if (400000000 % output)
			o = o + 1;
	} else {
		o = 1;
	}
	pll->odf = o;

	/* Computing FVCO freq*/
	output = 2 * output * o;

	return vclk_pll1600c45_get_params(input, output, pll);
}

/* ========================================================================
   Name:    vclk_pll1600c65_get_rate()
   Description: Convert input/mdiv/ndiv values to frequency for PLL1600
   Params:      'input' freq (Hz), mdiv/ndiv values
		Info: mdiv also called rdiv, ndiv also called ddiv
   Output:      '*rate' updated with value of HS output.
   Return:      Error code.
   ======================================================================== */

static int vclk_pll1600c65_get_rate(unsigned long input, struct vclk_pll *pll,
			unsigned long *rate)
{
	unsigned long mdiv = pll->mdiv;

	if (!mdiv)
		mdiv = 1; /* BIN to FORMULA conversion */

	/* Note: input is divided by 1000 to avoid overflow */
	*rate = ((2 * (input/1000) * pll->ndiv) / mdiv) * 1000;

	return 0;
}

/* ========================================================================
   Name:    vclk_pll1600c65_get_params()
   Description: Freq to parameters computation for PLL1600 CMOS65
   Input:       input,output=input/output freqs (Hz)
   Output:      updated *mdiv (rdiv) & *ndiv (ddiv)
   Return:      0=OK, -1=ERROR
   ======================================================================== */

/*
 * Spec used: PLL_PG_1600x_CMOS065LP_SPECS_1.4.pdf
 *
 * Rules:
 *   600Mhz <= output (FVCO) <= 1800Mhz
 *   1 <= M (also called R) <= 7
 *   4 <= N <= 255
 *   4Mhz <= PFDIN (input/M) <= 75Mhz
 *
 * FVCO = (INFF*LDF) / IDF
 * LDF = 2*NDIV
 * => FVCO = (INFF*2*NDIV) / IDF
 */

static int vclk_pll1600c65_get_params(unsigned long input, unsigned long output,
			struct vclk_pll *pll)
{
	unsigned long m, n, pfdin;
	unsigned long deviation = 0xffffffff;
	unsigned long new_freq;
	long new_deviation;

	/* FVCO output clock range: 600Mhz to 1800Mhz */
	if (!pll->no_limits)
		if (output < 600000000 || output > 1800000000)
			return -1;

	input /= 1000;
	output /= 1000;

	for (m = 1; (m <= 7) && deviation; m++) {
		n = m * output / (input * 2);

		/* Checks */
		if (n < 4)
			continue;
		if (n > 255)
			break;
		pfdin = input / m; /* 4Mhz <= PFDIN <= 75Mhz */
		if (pfdin < 4000 || pfdin > 75000)
			continue;

		new_freq = (input * 2 * n) / m;
		new_deviation = new_freq - output;
		if (new_deviation < 0)
			new_deviation = -new_deviation;
		if (!new_deviation || new_deviation < deviation) {
			pll->mdiv = m;
			pll->ndiv = n;
			deviation = new_deviation;
		}
	}

	if (deviation == 0xffffffff) /* No solution found */
		return -1;
	return 0;
}

/*
 * PLL3200 C28
 */

/* ========================================================================
   Name:    vclk_pll3200c28_get_rate()
   Description: Convert parameters to FVCOby2 frequency for PLL3200C28
   Params:      'input' freq (Hz), idf/ndiv values
   Output:      '*rate' updated with value of FVCOby2 output (PHIx / 1).
   Return:      Error code.
   ======================================================================== */

static int vclk_pll3200c28_get_rate(unsigned long input, struct vclk_pll *pll,
			unsigned long *rate)
{
	uint64_t res;

	if (!pll->idf)
		pll->idf = 1;

	/*
	 * INTEGER mode
	 *   FVCOby2 = (input*4*NDIV) / (2*IDF)
	 * FRACTIONAL mode
	 *   FVCOby2 = (input*4*(NDIV+FRACT/P16)) / (2*IDF) => DITHER OFF
	 *   FVCOby2 = (input*4*(NDIV+FRACT/P16+1/P17)) / (2*IDF) => DITHER ON
	 */

	if (!pll->mode) { /* INTEGER mode */
		res = (uint64_t)2 * input * pll->ndiv;
		*rate = div64_u64(res, pll->idf);
	} else { /* FRACTIONAL mode (DITHER OFF ONLY) */
		res = ((uint64_t)(P16 * pll->ndiv) + pll->frac) * 4 * input;
		*rate = div64_u64(res, (pll->idf * P17));
	}

	return 0;
}

/* ========================================================================
   Name:    vclk_pll3200c28_get_params()
   Description: Freq to parameters computation for PLL3200 CMOS28
   Inputs:
     input = input freq (Hz)
     output = FVCOBy2 freq (Hz)
     pll->no_limits = Set if freq range check to be disabled
     pll->idf = 0xff if algo to choose, other=frozen value
   Output:      updated *idf & *ndiv, plus *cp value (charge pump)
   Return:      0=OK, -1=ERROR
   ======================================================================== */

/* PLL output structure
 * VCO >> /2 >> FVCOBY2
 *                 |> Divider (ODF0) >> PHI0
 *                 |> Divider (ODF1) >> PHI1
 *                 |> Divider (ODF2) >> PHI2
 *                 |> Divider (ODF3) >> PHI3
 *
 *
 * Rules:
 *   4Mhz <= input <= 350Mhz
 *   800Mhz <= output (FVCOby2) <= 1600Mhz
 *   1 <= i (register value for IDF) <= 7
 *   8 <= n (register value for NDIV) <= 200
 */

static int vclk_pll3200c28_get_params(unsigned long input, unsigned long output,
			struct vclk_pll *pll)
{
	unsigned long i, n, n2; /* IDF & NDIV formula values */
	unsigned long deviation = 0xffffffff;
	unsigned long new_freq, new_deviation;
	/* Charge pump table: highest ndiv value for cp=6 to 25 */
	static const unsigned char cp_table[] = {
		48, 56, 64, 72, 80, 88, 96, 104, 112, 120,
		128, 136, 144, 152, 160, 168, 176, 184, 192, 200
	};
	struct vclk_pll pll_tmp = {
		.type = VCLK_PLL3200C28,
		.mode = pll->mode
	};
	int i_loops;

	/* Output clock range: 800Mhz to 1600Mhz */
	if (!pll->no_limits)
		if (output < 800000000 || output > 1600000000)
			return -1;

	/* IDF can be frozen to given value
	   0xff => algo to compute
	   else => value to use
	 */
	if (pll->idf != 0xff) {
		i = pll->idf;
		i_loops = 1; /* Frozen value => just try once */
	} else {
		i = 1;
		i_loops = 8;
	}

	if (!pll->mode) { /* INTEGER */
		for (; (i <= 7) && deviation && i_loops; i++, i_loops--) {
			n = div64_u64(((uint64_t)i * output), (2 * input));

			/* Checks */
			if (n < 8)
				continue;
			if (n > 200)
				break;

			pll_tmp.idf = i;
			/* Checking n & n+1 to cover division rounding */
			n2 = n + 1;
			for (; (n <= n2) && deviation; n++) {
				pll_tmp.ndiv = n;
				vclk_pll3200c28_get_rate(input, &pll_tmp,
							 &new_freq);
				if (new_freq > output)
					new_deviation = new_freq - output;
				else
					new_deviation = output - new_freq;
				if (!new_deviation || new_deviation <
				    deviation) {
					pll->idf  = i;
					pll->ndiv = n;
					deviation = new_deviation;
				}
			}
		}
	} else { /* FRACTIONAL */
		uint64_t f, f2;

		for (; (i <= 7) && deviation && i_loops; i++, i_loops--) {
			for (n = 200; (n >= 8) && deviation; n--) {

				f = ((uint64_t)output * i);
				f2 = ((uint64_t)n * 2 * input);
				if (f2 > f)
					continue; /* Invalid */
				f -= f2;
				f *= P15;
				f = div64_u64(f, input);

				if (f > 65535)
					continue; /* Invalid */

				pll_tmp.idf = i;
				pll_tmp.ndiv = n;
				/* Checking f & f+1 to cover division
				   rounding */
				f2 = f + 1;
				for (; (f <= f2) && deviation; f++) {
					pll_tmp.frac = f;
					vclk_pll3200c28_get_rate(input,
								 &pll_tmp,
								 &new_freq);
					if (new_freq > output)
						new_deviation = new_freq -
								output;
					else
						new_deviation = output -
								new_freq;
					if (!new_deviation || new_deviation <
					    deviation) {
						pll->idf  = i;
						pll->ndiv = n;
						pll->frac = f;
						deviation = new_deviation;
					}
				}
			}
		}
	}

	if (deviation == 0xffffffff) /* No solution found */
		return -1;

	/* Computing recommended charge pump value */
	for (pll->cp = 6; pll->ndiv > cp_table[pll->cp - 6]; (pll->cp)++)
		;

	return 0;
}

/*
 * PLL3200 C32
 */

/* ========================================================================
   Name:    vclk_pll3200c32_get_rate()
   Description: Convert input/idf/ndiv values to FVCOby2 frequency for PLL3200
   Params:      'input' freq (Hz), idf/ndiv values
   Output:      '*rate' updated with value of FVCOby2 output (PHIx / 1).
   Return:      Error code.
   ======================================================================== */

static int vclk_pll3200c32_get_rate(unsigned long input, struct vclk_pll *pll,
			unsigned long *rate)
{
	if (!pll->idf)
		pll->idf = 1;

	/* Note: input is divided to avoid overflow */
	*rate = ((2 * (input/1000) * pll->ndiv) / pll->idf) * 1000;

	return 0;
}

/* ========================================================================
   Name:    vclk_pll3200c32_get_params()
   Description: Freq to parameters computation for PLL3200 CMOS32
   Input:       input=input freq (Hz), output=FVCOBY2 freq (Hz)
   Output:      updated *idf & *ndiv, plus *cp value (charge pump)
   Return:      0=OK, -1=ERROR
   ======================================================================== */

/* PLL output structure
 * VCO >> /2 >> FVCOBY2
 *                 |> Divider (ODF0) >> PHI0
 *                 |> Divider (ODF1) >> PHI1
 *                 |> Divider (ODF2) >> PHI2
 *                 |> Divider (ODF3) >> PHI3
 *
 * FVCOby2 output = (input*4*NDIV) / (2*IDF) (assuming FRAC_CONTROL==L)
 *
 * Rules:
 *   4Mhz <= input <= 350Mhz
 *   800Mhz <= output (FVCOby2) <= 1600Mhz
 *   1 <= i (register value for IDF) <= 7
 *   8 <= n (register value for NDIV) <= 200
 */

static int vclk_pll3200c32_get_params(unsigned long input, unsigned long output,
			struct vclk_pll *pll)
{
	unsigned long i, n;
	unsigned long deviation = 0xffffffff;
	unsigned long new_freq;
	long new_deviation;
	/* Charge pump table: highest ndiv value for cp=6 to 25 */
	static const unsigned char cp_table[] = {
		48, 56, 64, 72, 80, 88, 96, 104, 112, 120,
		128, 136, 144, 152, 160, 168, 176, 184, 192
	};

	/* Output clock range: 800Mhz to 1600Mhz */
	if (!pll->no_limits)
		if (output < 800000000 || output > 1600000000)
			return -1;

	input /= 1000;
	output /= 1000;

	for (i = 1; (i <= 7) && deviation; i++) {
		n = i * output / (2 * input);

		/* Checks */
		if (n < 8)
			continue;
		if (n > 200)
			break;

		new_freq = (input * 2 * n) / i;
		new_deviation = new_freq - output;
		if (new_deviation < 0)
			new_deviation = -new_deviation;
		if (!new_deviation || new_deviation < deviation) {
			pll->idf  = i;
			pll->ndiv = n;
			deviation = new_deviation;
		}
	}

	if (deviation == 0xffffffff) /* No solution found */
		return -1;

	/* Computing recommended charge pump value */
	for (pll->cp = 6; pll->ndiv > cp_table[pll->cp-6]; (pll->cp)++)
		;

	return 0;
}

/*
 * PLL4600 C28
 */

/* ========================================================================
   Name:    vclk_pll4600c28_get_rate()
   Description: Convert input/idf/ndiv values to FVCOby2 frequency
   Params:      'input' freq (Hz), idf/ndiv values
   Output:      '*rate' updated with value of FVCOby2 output (PHIx / 1).
   Return:      Error code.
   ======================================================================== */

static int vclk_pll4600c28_get_rate(unsigned long input, struct vclk_pll *pll,
			unsigned long *rate)
{
	uint64_t val;

	if (!pll->idf)
		pll->idf = 1;

	val = (uint64_t)input * 2 * pll->ndiv;
	*rate = div64_u64(val, pll->idf);

	return 0;
}

/* ========================================================================
   Name:    vclk_pll4600c28_get_params()
   Description: Freq to parameters computation for PLL4600 CMOS28
   Input:       input=input freq (Hz), output=FVCOBY2 freq (Hz)
   Output:      updated pll->idf & pll->ndiv
   Return:      0=OK, -1=ERROR
   ======================================================================== */

/* PLL output structure
 * FVCO >> /2 >> FVCOBY2 (no output)
 *                 |> Divider (ODF) >> PHI
 *
 * FVCOby2 output = (input * 2 * NDIV) / IDF (assuming FRAC_CONTROL==L)
 *
 * Rules:
 *   4Mhz <= INFF input <= 350Mhz
 *   4Mhz <= INFIN (INFF / IDF) <= 50Mhz
 *   1.2Ghz <= FVCOby2 output (PHI w ODF=1) <= 3000Mhz
 *   1 <= i (register/dec value for IDF) <= 7
 *   8 <= n (register/dec value for NDIV) <= 246
 */

static int vclk_pll4600c28_get_params(unsigned long input, unsigned long output,
			struct vclk_pll *pll)
{
	unsigned long i, infin, n, n2;
	unsigned long deviation = 0xffffffff;
	unsigned long new_freq, new_deviation;
	struct vclk_pll pll_tmp = {
		.type = VCLK_PLL4600C28,
		.mode = pll->mode
	};

	/* Output clock range: 1.2Ghz to 3Ghz */
	if (!pll->no_limits)
		if (output < 1200000000u || output > 3000000000u)
			return -1;

	/* For better jitter, IDF should be smallest
	   and NDIV must be maximum */
	for (i = 1; (i <= 7) && deviation; i++) {
		/* INFIN checks */
		infin = input / i;
		if (!pll->no_limits)
			if ((infin < 4000000) || (infin > 50000000))
				continue;   /* Invalid case */

		n = div64_u64(((uint64_t)i * output), (2 * input));

		/* Checks */
		if (n < 8)
			continue;
		if (n > 246)
			break;

		pll_tmp.idf = i;
		/* Checking n & n+1 to cover division rounding */
		n2 = n + 1;
		for (; (n <= n2) && deviation; n++) {
			pll_tmp.ndiv = n;
			vclk_pll4600c28_get_rate(input, &pll_tmp, &new_freq);
			if (new_freq > output)
				new_deviation = new_freq - output;
			else
				new_deviation = output - new_freq;
			if (!new_deviation || new_deviation < deviation) {
				pll->idf  = i;
				pll->ndiv = n;
				deviation = new_deviation;
			}
		}
	}

	if (deviation == 0xffffffff) /* No solution found */
		return -1;

	return 0;
}

/*
 * FS216
 */

#define FS216_SCALING_FACTOR    4096LL

 /* ========================================================================
   Name:    vclk_fs216c65_get_rate()
   Description: Parameters to freq computation for frequency synthesizers.
   ======================================================================== */

static int vclk_fs216c65_get_rate(unsigned long input,
		struct vclk_fs *fs, unsigned long *rate)
{
	uint64_t res;
	unsigned long ns; /* nsdiv3 value: 3=0.bin, 1=1.bin */
	unsigned long nd = 8; /* ndiv stuck at 0 => val = 8 */
	unsigned long s; /* sdiv value = 1 << (sdiv_bin + 1) */
	long m;	/* md value (-17 to -1) */

	/* BIN to VAL */
	m = fs->mdiv - 32;
	s = 1 << (fs->sdiv + 1);
	ns = (fs->nsdiv ? 1 : 3);

	res = (uint64_t)(s * ns * P15 * (uint64_t)(m + 33));
	res = res - (s * ns * fs->pe);
	*rate = div64_u64(P15 * nd * input * 32, res);

	return 0;
}

/* ========================================================================
   Name:    vclk_fs216c65_get_params()
   Description: Freq to parameters computation for FS216 C65
   Input:       input=input freq (Hz), output=output freq (Hz)
   Output:      updated 'struct vclk_fs *fs'
   Return:      0=OK, -1=ERROR
   ======================================================================== */

static int vclk_fs216c65_get_params(unsigned long input,
		unsigned long output, struct vclk_fs *fs)
{
	unsigned long nd = 8; /* ndiv value: bin stuck at 0 => value = 8 */
	unsigned long ns; /* nsdiv3 value: 3=0.bin, 1=1.bin */
	unsigned long sd; /* sdiv value = 1 << (sdiv_bin_value + 1) */
	long m;	/* md value (-17 to -1) */
	uint64_t p, p2;	/* pe value */
	int si;
	unsigned long new_freq, new_deviation;
	/* initial condition to say: "infinite deviation" */
	unsigned long deviation = 0xffffffff;
	int stop, ns_search_loop;
	struct vclk_fs fs_tmp = {
		.type = VCLK_FS216C65,
	};

	/* Checks */
	if (!output)
		return -1;

	/*
	 * fs->nsdiv is a register value ('BIN') which is translated
	 * to a decimal value according to following rules.
	 * In case nsdiv is hardwired, it must be set to 0xff before calling.
	 *
	 * fs->nsdiv      ns.dec
	 *   0xff     computed by this algo
	 *     0      3
	 *     1      1
	 */
	if (fs->nsdiv != 0xff) {
		ns = (fs->nsdiv ? 1 : 3);
		ns_search_loop = 1;
	} else {
		ns = 3;
		ns_search_loop = 2;
	}

	for (; (ns < 4) && ns_search_loop; ns -= 2, ns_search_loop--)
		for (si = 7; (si >= 0) && deviation; si--) {
			sd = (1 << (si + 1));
			/* Recommended search order: -16 to -1, then -17 */
			for (m = -16, stop = 0; !stop && deviation; m++) {
				if (!m) {
					m = -17; /* 0 is -17 */
					stop = 1;
				}
				p = P15 * 32 * nd * input *
					FS216_SCALING_FACTOR;
				p = div64_u64(p, sd * ns * output *
					      FS216_SCALING_FACTOR);
				p2 = P15 * (uint64_t)(m + 33);
				if (p2 < p)
					continue; /* p must be >= 0 */
				p = p2 - p;

				if (p > 32768LL)
					/* Already too high. Let's move to
					   next sdiv */
					break;

				fs_tmp.mdiv = (unsigned long)(m + 32);
				/* pe fine tuning: ± 2 around computed
				   pe value */
				if (p > 2)
					p2 = p - 2;
				else
					p2 = 0;
				for (; p2 < 32768ll && (p2 < (p + 2)); p2++) {
					fs_tmp.pe = p2;
					fs_tmp.sdiv = si;
					fs_tmp.nsdiv = (ns == 1) ? 1 : 0;
					vclk_fs216c65_get_rate(input, &fs_tmp,
							       &new_freq);

					if (new_freq < output)
						new_deviation = output -
								new_freq;
					else
						new_deviation = new_freq -
								output;
					/* Check if this is a better solution */
					if (new_deviation < deviation) {
						fs->mdiv = (unsigned long)(m +
									   32);
						fs->pe = (unsigned long)p2;
						fs->sdiv = si;
						fs->nsdiv = (ns == 1) ? 1 : 0;
						deviation = new_deviation;
					}
				}
			}
		}

	if (deviation == 0xffffffff) /* No solution found */
		return -1;

	return 0;
}

/*
 * FS432 C65
 * Based on "C65_4FS432_25_um.pdf" spec
 */

/* ========================================================================
   Name:    vclk_fs432c65_get_rate()
   Description: Parameters to freq computation for frequency synthesizers.
   ======================================================================== */

static int vclk_fs432c65_get_rate(unsigned long input, struct vclk_fs *fs,
		unsigned long *rate)
{
	uint64_t res;
	unsigned long nd = 16; /* ndiv value; stuck at 0 (30Mhz input) */
	long m;	/* md value (-17 to -1) */
	unsigned long sd; /* sdiv value = 1 << (sdiv_bin + 1) */
	unsigned long ns; /* nsdiv3 value */

	/* BIN to VAL */
	m = fs->mdiv - 32;
	sd = 1 << (fs->sdiv + 1);
	ns = (fs->nsdiv ? 1 : 3);

	res = (uint64_t)(sd * ns * P15 * (uint64_t)(m + 33));
	res = res - (sd * ns * fs->pe);
	*rate = div64_u64(P15 * nd * input * 32, res);

	return 0;
}

/* ========================================================================
   Name:    vclk_fs432c65_get_params()
   Description: Freq to parameters computation for FS432 C65
   Input:       input=input freq (Hz), output=output freq (Hz)
   Output:      updated *md, *pe, *sdiv, & *nsdiv3
   Return:      0=OK, -1=ERROR
   ======================================================================== */

#define FS432_SCALING_FACTOR    4096LL

static int vclk_fs432c65_get_params(unsigned long input,
		unsigned long output, struct vclk_fs *fs)
{
	unsigned long nd = 16; /* ndiv value; stuck at 0 (30Mhz input) */
	unsigned long ns; /* nsdiv3 value */
	unsigned long sd; /* sdiv value = 1 << (sdiv_bin_value + 1) */
	long m;	/* md value (-17 to -1) */
	uint64_t p, p2;	/* pe value */
	int si;
	unsigned long new_freq, new_deviation;
	/* initial condition to say: "infinite deviation" */
	unsigned long deviation = 0xffffffff;
	int stop, ns_search_loop;
	struct vclk_fs fs_tmp = {
		.type = VCLK_FS432C65,
	};

	/*
	 * *nsdiv3 is a register value ('BIN') which is translated
	 * to a decimal value according to following rules.
	 * In case nsdiv is hardwired, it must be set to 0xff before calling.
	 *
	 *    *nsdiv   ns.dec
	 *    ff       computed by this algo
	 *    0        3
	 *    1        1
	 */
	if (fs->nsdiv != 0xff) {
		ns = (fs->nsdiv ? 1 : 3);
		ns_search_loop = 1;
	} else {
		ns = 3;
		ns_search_loop = 2;
	}

	for (; (ns < 4) && ns_search_loop; ns -= 2, ns_search_loop--)
		for (si = 7; (si >= 0) && deviation; si--) {
			sd = (1 << (si + 1));
			/* Recommended search order: -16 to -1, then -17
			 * (if 24Mhz<input<27Mhz ONLY)
			 */
			for (m = -16, stop = 0; !stop && deviation; m++) {
				if (!m) {
					/* -17 forbidden with 30Mhz */
					if (input > 27000000)
						break;
					m = -17; /* 0 is -17 */
					stop = 1;
				}
				p = P15 * 32 * nd * input *
					FS432_SCALING_FACTOR;
				p = div64_u64(p, sd * ns * output *
							  FS432_SCALING_FACTOR);
				p2 = P15 * (uint64_t)(m + 33);
				if (p2 < p)
					continue; /* p must be >= 0 */

				p = p2 - p;

				if (p > 32768LL)
					/* Already too high. Let's move to
					   next sdiv */
					break;

				fs_tmp.mdiv = (unsigned long)(m + 32);
				fs_tmp.pe = (unsigned long)p;
				fs_tmp.sdiv = si;
				fs_tmp.nsdiv = (ns == 1) ? 1 : 0;
				if (vclk_fs432c65_get_rate(input, &fs_tmp,
							   &new_freq) != 0)
					continue;

				if (new_freq < output)
					new_deviation = output - new_freq;
				else
					new_deviation = new_freq - output;
				/* Check if this is a better solution */
				if (new_deviation < deviation) {
					fs->mdiv = (unsigned long)(m + 32);
					fs->pe = (unsigned long)p;
					fs->sdiv = si;
					fs->nsdiv = (ns == 1) ? 1 : 0;
					deviation = new_deviation;
				}
			}
		}

	if (deviation == 0xffffffff) /* No solution found */
		return -1;

	return 0;
}

/*
	FS660 C28
	Based on "MDSpecs_C28SOI_4FS_660MHZ_LR_EG_5U1X2T8X.v1.2.pdf" spec.

	This FSYN embeds a programmable PLL which then serve the 4 digital
	blocks
	For this reason the PLL660 is programmed separately from digital parts.

	clkin
		=> PLL660
			=> DIG660_0 => clkout0
			=> DIG660_1 => clkout1
			=> DIG660_2 => clkout2
			=> DIG660_3 => clkout3
*/

/* ========================================================================
   Name:    vclk_fs660c28_vco_get_rate()
   Description: Compute VCO frequency of FS660C28 embedded PLL (PLL660)
   Input: clkin + ndiv & pdiv registers values
   Output: updated *rate (Hz)
   Returns: 0=OK, -1=can't compute with given input+ndiv
   ======================================================================== */

static int vclk_fs660c28_vco_get_rate(unsigned long input,
		struct vclk_fs *fs, unsigned long *rate)
{
	unsigned long nd = fs->ndiv + 8; /* ndiv value */
	unsigned long pdiv = 1;	/* Frozen. Not configurable so far */

	/* ndiv: 0x2 to 0xf => formula 10 to 23 */
	if ((fs->ndiv < 2) || (fs->ndiv > 15))
		return -1;	/* Invalid NDIV */

	*rate = (input * nd) / pdiv;

	return 0;
}

/* ========================================================================
   Name:    vclk_fs660c28_vco_get_params()
   Description: Compute FS660 C28 params for embedded PLL660
   Input:       input=input freq (Hz), output=output freq (Hz)
   Output:      updated *ndiv (register value). Note that PDIV is frozen to 1.
   Return:      0=OK, -1=ERROR
   ======================================================================== */

static int vclk_fs660c28_vco_get_params(unsigned long input,
		unsigned long output, struct vclk_fs *fs)
{
	/* Formula
	 * VCO frequency = (fin x ndiv) / pdiv
	 * ndiv = VCOfreq * pdiv / fin
	 */
	unsigned long pdiv = 1, n;

	/* Output clock range: 384Mhz to 660Mhz with 2% margin */
	if (!fs->no_limits)
		if (output < 376320000 || output > 673200000)
			return -1;

	if (input > 40800000) /* 40Mhz max + 2% */
		/* This means that PDIV would be 2 instead of 1.
		   Not supported today. */
		return -1;

	n = output * pdiv / input;
	if (n < 10)
		n = 10;
	fs->ndiv = n - 8; /* Converting formula value to reg value */

	return 0;
}

/* ========================================================================
   Name   : vclk_fs660c28_dig_get_rate()
   Desc   : Parameters to freq computation for frequency synthesizers.
   Inputs : input=VCO frequency, nsdiv, md, pe, & sdiv 'BIN' values.
   Outputs: *rate updated
   Returns: 0=OK, -1=ERROR
   ======================================================================== */

static int vclk_fs660c28_dig_get_rate(unsigned long input,
		struct vclk_fs *fs, unsigned long *rate)
{
	/* sdiv value = 1 << sdiv_reg_value */
	unsigned long s = (1 << fs->sdiv);
	unsigned long ns;  /* nsdiv value (1 or 3) */
	uint64_t res;

	/*
	 * 'nsdiv' is a register value ('BIN') which is translated
	 * to a decimal value according to following rules.
	 *
	 *     nsdiv      ns.dec
	 *       0        3
	 *       1        1
	 */
	ns = (fs->nsdiv == 1) ? 1 : 3;

	res = ((uint64_t)P20 * (32 + fs->mdiv) + 32 * fs->pe) * s * ns;
	*rate = (unsigned long)div64_u64(input * P20 * 32, res);
	return 0;
}

/*	==========================================================
	Name  : vclk_fs660c28_dig_get_params()
	Desc  : Compute params for digital part of FS660
	Input : fvco=VCO freq, output=requested freq (Hz), *nsdiv
		   (0/1 if silicon frozen, or 0xff if to be computed).
	Output: updated *nsdiv, *md, *pe & *sdiv registers values.
	Return: 0=OK, -1=ERROR
	========================================================== */

static int vclk_fs660c28_dig_get_params(unsigned long fvco,
			unsigned long output, struct vclk_fs *fs)
{
	int si; /* sdiv_reg (8 downto 0) */
	unsigned long ns; /* nsdiv value (1 or 3) */
	unsigned long s; /* sdiv value = 1 << sdiv_reg */
	unsigned long m; /* md value */
	unsigned long new_freq, new_deviation;
	/* initial condition to say: "infinite deviation" */
	unsigned long deviation = 0xffffffff;
	uint64_t p, p2; /* pe value */
	int ns_search_loop; /* How many ns search trials */
	struct vclk_fs fs_tmp = {
		.type = VCLK_FS660C28
	};

	/*
	 * *nsdiv is a register value ('BIN') which is translated
	 * to a decimal value according to following rules.
	 * In case nsdiv is hardwired, it must be set to 0xff before calling.
	 *
	 *   nsdiv      ns.dec
	 *      ff        computed by this algo
	 *       0        3
	 *       1        1
	 */
	if (fs->nsdiv != 0xff) {
		ns = (fs->nsdiv ? 1 : 3);
		ns_search_loop = 1;
	} else {
		ns = 3;
		ns_search_loop = 2;
	}

	for (; (ns < 4) && ns_search_loop && deviation;
	     ns -= 2, ns_search_loop--)
		for (si = 8; (si >= 0) && deviation; si--) {
			s = (1 << si);
			for (m = 0; (m < 32) && deviation; m++) {
				/* printf("vco=%lu ns=%u s=%d m=%d\n", fvco,
					  ns, s, m); */
				p = (uint64_t)fvco * P20 * 32;
				/* printf("  fvco * P20 * 32=%"
					  PRIu64 "\n", p); */
				p2 = (32 + m) * ((uint64_t)s * ns * output) *
					P20;
				/* printf("  (32 + m) * (s * ns * output) *
					  P20=%" PRIu64 "\n", p2); */
				if (p2 > p)
					continue; /* Invalid */
				p -= p2;
				p2 = (uint64_t)s * ns * output * 32;
				p = div64_u64(p, p2);
				/* printf("  fvco=%lu ns=%lu s=%lu m=%lu => p=%"
					  PRIu64 "\n", fvco, ns, s, m, p); */

				if (p > 32767LL)
					continue;

				fs_tmp.mdiv = m;
				fs_tmp.sdiv = si;
				fs_tmp.nsdiv = (ns == 1) ? 1 : 0;
				/* Checking p & p+1 to cover division
				   rounding */
				p2 = p + 1;
				for (; (p <= p2) && deviation; p++) {
					fs_tmp.pe = (unsigned long)p;
					if (vclk_fs660c28_dig_get_rate(fvco,
						&fs_tmp, &new_freq) != 0)
						continue;
					if (new_freq < output)
						new_deviation = output -
								new_freq;
					else
						new_deviation = new_freq -
								output;
					/* if (new_deviation == deviation)
						printf("SAME out=%lu\n\n",
						       new_freq); */
					if (new_deviation < deviation) {
						/* printf("BEST out=%lu\n\n",
							  new_freq); */
						fs->mdiv = m;
						fs->pe = (unsigned long)p;
						fs->sdiv = si;
						fs->nsdiv = (ns == 1) ? 1 : 0;
						deviation = new_deviation;
					}
				}
			}
		}

	if (deviation == 0xffffffff) /* No solution found */
		return -1;

	return 0;
}

/*
	FS660 C32
	Based on C32_4FS_660MHZ_LR_EG_5U1X2T8X_um spec.

	This FSYN embeds a programmable PLL which then
	serve the 4 digital blocks
	For this reason the PLL660 is programmed
	separately from digital parts.

	clkin
		=> PLL660
			=> DIG660_0 => clkout0
			=> DIG660_1 => clkout1
			=> DIG660_2 => clkout2
			=> DIG660_3 => clkout3
*/

/* We use Fixed-point arithmetic in order to avoid "float" functions.*/
#define SCALING_FACTOR  2048LL

/* ========================================================================
   Name:    vclk_fs660c32_vco_get_rate()
   Description: Compute VCO frequency of FS660 embedded PLL (PLL660)
   Input: ndiv & pdiv registers values
   Output: updated *rate (Hz)
   Returns: 0=OK, -1=can't compute with given input+ndiv
   ======================================================================== */

static int vclk_fs660c32_vco_get_rate(unsigned long input, struct vclk_fs *fs,
			unsigned long *rate)
{
	unsigned long nd = fs->ndiv + 16; /* ndiv value */
	unsigned long pdiv = 1;	/* Frozen. Not configurable so far */

	/* ndiv: 0x0 to 0x7 => formula 16 to 23 */
	if (fs->ndiv > 7)
		return -1;	/* Invalid NDIV */

	*rate = (input * nd) / pdiv;

	return 0;
}

/* ========================================================================
   Name:    vclk_fs660c32_vco_get_params()
   Description: Compute params for embeded PLL660
   Input:       input=input freq (Hz), output=output freq (Hz)
   Output:      updated *ndiv (register value). Note that PDIV is frozen to 1.
   Return:      0=OK, -1=ERROR
   ======================================================================== */

static int vclk_fs660c32_vco_get_params(unsigned long input,
			unsigned long output, struct vclk_fs *fs)
{
	/* Formula
	 * VCO frequency = (fin x ndiv) / pdiv
	 * ndiv = VCOfreq * pdiv / fin
	 */
	unsigned long pdiv = 1, n;

	/* Output clock range: 384Mhz to 660Mhz */
	if (!fs->no_limits)
		if (output < 384000000 || output > 660000000)
			return -1;

	if (input > 40000000)
		/* This means that PDIV would be 2 instead of 1.
		   Not supported today. */
		return -1;

	input /= 1000;
	output /= 1000;

	n = output * pdiv / input;
	if (n < 16)
		n = 16;
	fs->ndiv = n - 16; /* Converting formula value to reg value */

	return 0;
}

/* ========================================================================
   Name:    vclk_fs660c32_dig_get_rate()
   Description: Parameters to freq computation for frequency synthesizers.
   Inputs:  input=VCO frequency, nsdiv, md, pe, & sdiv 'BIN' values.
   Outputs: *rate updated
   ======================================================================== */

static int vclk_fs660c32_dig_get_rate(unsigned long input, struct vclk_fs *fs,
			  unsigned long *rate)
{
	/* sdiv value = 1 << sdiv_reg_value */
	unsigned long s = (1 << fs->sdiv);
	unsigned long ns;  /* nsdiv value (1 or 3) */
	uint64_t res;

	/*
	 * 'nsdiv' is a register value ('BIN') which is translated
	 * to a decimal value according to following rules.
	 *
	 *     nsdiv      ns.dec
	 *       0        3
	 *       1        1
	 */
	ns = (fs->nsdiv == 1) ? 1 : 3;

	res = (P20 * (32 + fs->mdiv) + 32 * fs->pe) * s * ns;
	*rate = (unsigned long)div64_u64(input * P20 * 32, res);
	return 0;
}

/* ========================================================================
   Name:    vclk_fs660c32_dig_get_params()
   Description: Compute params for digital part of FS660
   Input:       input=VCO freq, output=requested freq (Hz), *nsdiv
			(0/1 if silicon frozen, or 0xff if to be computed).
   Output:      updated *nsdiv, *md, *pe & *sdiv registers values.
   Return:      0=OK, -1=ERROR
   ======================================================================== */

static int vclk_fs660c32_dig_get_params(unsigned long input,
		unsigned long output, struct vclk_fs *fs)
{
	int si;	/* sdiv_reg (8 downto 0) */
	unsigned long ns; /* nsdiv value (1 or 3) */
	unsigned long s; /* sdiv value = 1 << sdiv_reg */
	unsigned long m; /* md value */
	unsigned long new_freq, new_deviation;
	/* initial condition to say: "infinite deviation" */
	unsigned long deviation = 0xffffffff;
	uint64_t p, p2;	/* pe value */
	int ns_search_loop;	/* How many ns search trials */
	struct vclk_fs fs_tmp = {
		.type = VCLK_FS660C32
	};

	/*
	 * *nsdiv is a register value ('BIN') which is translated
	 * to a decimal value according to following rules.
	 * In case nsdiv is hardwired, it must be set to 0xff before calling.
	 *
	 *    *nsdiv      ns.dec
	 *  ff    computed by this algo
	 *       0        3
	 *       1        1
	 */
	if (fs->nsdiv != 0xff) {
		ns = (fs->nsdiv ? 1 : 3);
		ns_search_loop = 1;
	} else {
		ns = 3;
		ns_search_loop = 2;
	}

	for (; (ns < 4) && ns_search_loop; ns -= 2, ns_search_loop--)
		for (si = 8; (si >= 0) && deviation; si--) {
			s = (1 << si);
			for (m = 0; (m < 32) && deviation; m++) {
				p = (uint64_t)input * SCALING_FACTOR;
				p = p - SCALING_FACTOR * ((uint64_t)s *
					(uint64_t)ns * (uint64_t)output) -
					((uint64_t)s * (uint64_t)ns *
					(uint64_t)output) *
					((uint64_t)m *
					(SCALING_FACTOR / 32LL));
				p = p * (P20 / SCALING_FACTOR);
				p = div64_u64(p, (uint64_t)((uint64_t)s *
					      (uint64_t)ns *
					      (uint64_t)output));

				if (p > 32767LL)
					continue;

				fs_tmp.mdiv = m;
				fs_tmp.pe = (unsigned long)p;
				fs_tmp.sdiv = si;
				fs_tmp.nsdiv = (ns == 1) ? 1 : 0;
				if (vclk_fs660c32_dig_get_rate(input, &fs_tmp,
							       &new_freq) != 0)
					continue;
				if (new_freq < output)
					new_deviation = output - new_freq;
				else
					new_deviation = new_freq - output;
				if (new_deviation < deviation) {
					fs->mdiv = m;
					fs->pe = (unsigned long)p;
					fs->sdiv = si;
					fs->nsdiv = (ns == 1) ? 1 : 0;
					deviation = new_deviation;
				}
			}
		}

	if (deviation == 0xffffffff) /* No solution found */
		return -1;

    /* pe fine tuning if deviation not 0: ± 2 around computed pe value */
	if (deviation) {
		fs_tmp.mdiv = fs->mdiv;
		fs_tmp.sdiv = fs->sdiv;
		fs_tmp.nsdiv = fs->nsdiv;
		if (fs->pe > 2)
			p2 = fs->pe - 2;
		else
			p2 = 0;
		for (; p2 < 32768ll && (p2 <= (fs->pe + 2)); p2++) {
			fs_tmp.pe = (unsigned long)p2;
			if (vclk_fs660c32_dig_get_rate(input, &fs_tmp,
						       &new_freq) != 0)
				continue;
			if (new_freq < output)
				new_deviation = output - new_freq;
			else
				new_deviation = new_freq - output;

			/* Check if this is a better solution */
			if (new_deviation < deviation) {
				fs->pe = (unsigned long)p2;
				deviation = new_deviation;
			}
		}
	}

	return 0;
}

/*
 * Exported functions
 */

/* ========================================================================
   Name:    vclk_pll_get_params
   Description: Generic freq to parameters computation function
   Returns:     0=OK
   ======================================================================== */

int vclk_pll_get_params(unsigned long input, unsigned long output,
						struct vclk_pll *pll)
{
	switch (pll->type) {
	case VCLK_PLL800C65:
		return vclk_pll800c65_get_params(input, output, pll);
	case VCLK_PLL1200C32:
		return vclk_pll1200c32_get_params(input, output, pll);
	case VCLK_PLL1600C45:
		return vclk_pll1600c45_get_params(input, output, pll);
	case VCLK_PLL1600C45PHI:
		return vclk_pll1600c45_get_phi_params(input, output, pll);
	case VCLK_PLL1600C65:
		return vclk_pll1600c65_get_params(input, output, pll);
	case VCLK_PLL3200C28:
		return vclk_pll3200c28_get_params(input, output, pll);
	case VCLK_PLL3200C32:
		return vclk_pll3200c32_get_params(input, output, pll);
	case VCLK_PLL4600C28:
		return vclk_pll4600c28_get_params(input, output, pll);
	default:
		break;
	}

	return -1;
}

/* ========================================================================
   Name:    vclk_pll_get_params
   Description: Generic parameters to freq computation function
   Returns:     0=OK
   ======================================================================== */

int vclk_pll_get_rate(unsigned long input, struct vclk_pll *pll,
					  unsigned long *output)
{
	switch (pll->type) {
	case VCLK_PLL800C65:
		return vclk_pll800c65_get_rate(input, pll, output);
	case VCLK_PLL1200C32:
		return vclk_pll1200c32_get_rate(input, pll, output);
	case VCLK_PLL1600C45:
		return vclk_pll1600c45_get_rate(input, pll, output);
	case VCLK_PLL1600C45PHI:
		return vclk_pll1600c45_get_phi_rate(input, pll, output);
	case VCLK_PLL1600C65:
		return vclk_pll1600c65_get_rate(input, pll, output);
	case VCLK_PLL3200C28:
		return vclk_pll3200c28_get_rate(input, pll, output);
	case VCLK_PLL3200C32:
		return vclk_pll3200c32_get_rate(input, pll, output);
	case VCLK_PLL4600C28:
		return vclk_pll4600c28_get_rate(input, pll, output);
	default:
		break;
	}

	return -1;
}

/* ========================================================================
   Name:    vclk_fs_get_params
   Description: Generic freq to parameters computation function
   Returns:     0=OK
   ======================================================================== */

int vclk_fs_get_params(unsigned long input, unsigned long output,
					   struct vclk_fs *fs)
{
	switch (fs->type) {
	case VCLK_FS216C65:
		return vclk_fs216c65_get_params(input, output, fs);
	case VCLK_FS432C65:
		return vclk_fs432c65_get_params(input, output, fs);
	case VCLK_FS660C28VCO:
		return vclk_fs660c28_vco_get_params(input, output, fs);
	case VCLK_FS660C28:
		return vclk_fs660c28_dig_get_params(input, output, fs);
	case VCLK_FS660C32VCO:
		return vclk_fs660c32_vco_get_params(input, output, fs);
	case VCLK_FS660C32:
		return vclk_fs660c32_dig_get_params(input, output, fs);
	default:
		break;
	}

	return -1;
}

/* ========================================================================
   Name:    vclk_fs_get_params
   Description: Generic parameters to freq computation function
   Returns:     0=OK
   ======================================================================== */

int vclk_fs_get_rate(unsigned long input, struct vclk_fs *fs,
					 unsigned long *output)
{
	switch (fs->type) {
	case VCLK_FS216C65:
		return vclk_fs216c65_get_rate(input, fs, output);
	case VCLK_FS432C65:
		return vclk_fs432c65_get_rate(input, fs, output);
	case VCLK_FS660C28VCO:
		return vclk_fs660c28_vco_get_rate(input, fs, output);
	case VCLK_FS660C28:
		return vclk_fs660c28_dig_get_rate(input, fs, output);
	case VCLK_FS660C32VCO:
		return vclk_fs660c32_vco_get_rate(input, fs, output);
	case VCLK_FS660C32:
		return vclk_fs660c32_dig_get_rate(input, fs, output);
	default:
		break;
	}

	return -1;
}
