/*
 * clk-flexgen.c
 *
 * Copyright (C) ST-Microelectronics SA 2013
 * Author:  Maxime Coquelin <maxime.coquelin@st.com> for ST-Microelectronics.
 * License terms:  GNU General Public License (GPL), version 2  */

#include <linux/clk-provider.h>
#include "vclk_algos.h"
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clkgen.h"

#define FLEXGEN_MAX_OUTPUTS 48
#define FLEXGEN_MAX_PARENT 4

struct clkgen_data_parent_clk {
	u32 input_id;
	struct clk *clkp;
	bool sync;
};

struct clkgen_data_clk {
	struct clk *clkp;
	bool sync;
};

struct clkgen_data {
	unsigned long clk_flags;
	bool clk_mode;
	unsigned long beat_parents;
	struct clkgen_data_parent_clk parent_clks[FLEXGEN_MAX_PARENT];
	struct clkgen_field beatcnt[FLEXGEN_MAX_PARENT];
	struct clkgen_data_clk clks[FLEXGEN_MAX_OUTPUTS];
};

struct flexgen {
	struct clk_hw hw;

	/* Crossbar */
	struct clk_mux mux;
	/* Pre-divisor's gate */
	struct clk_gate pgate;
	/* Pre-divisor */
	struct clk_divider pdiv;
	/* Final divisor's gate */
	struct clk_gate fgate;
	/* Final divisor */
	struct clk_divider fdiv;
	/* Asynchronous mode control */
	struct clk_gate sync;
	/* hw control flags */
	bool control_mode;
	/* Beatcnt divisor */
	unsigned long beat_parents;
	struct clk_divider beatdiv[FLEXGEN_MAX_PARENT];
	struct clkgen_data_parent_clk *parents_clks;
	struct clkgen_data_clk *clks;
};

#define to_flexgen(_hw) container_of(_hw, struct flexgen, hw)
#define to_clk_gate(_hw) container_of(_hw, struct clk_gate, hw)

static int flexgen_enable(struct clk_hw *hw)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *pgate_hw = &flexgen->pgate.hw;
	struct clk_hw *fgate_hw = &flexgen->fgate.hw;

	pgate_hw->clk = hw->clk;
	fgate_hw->clk = hw->clk;

	clk_gate_ops.enable(pgate_hw);

	clk_gate_ops.enable(fgate_hw);

	pr_debug("%s: flexgen output enabled\n", __clk_get_name(hw->clk));
	return 0;
}

static void flexgen_disable(struct clk_hw *hw)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *fgate_hw = &flexgen->fgate.hw;

	/* disable only the final gate */
	fgate_hw->clk = hw->clk;

	clk_gate_ops.disable(fgate_hw);

	pr_debug("%s: flexgen output disabled\n", __clk_get_name(hw->clk));
}

static int flexgen_is_enabled(struct clk_hw *hw)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *fgate_hw = &flexgen->fgate.hw;

	fgate_hw->clk = hw->clk;

	if (!clk_gate_ops.is_enabled(fgate_hw))
		return 0;

	return 1;
}

static unsigned long flexgen_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *pdiv_hw = &flexgen->pdiv.hw;
	struct clk_hw *fdiv_hw = &flexgen->fdiv.hw;
	unsigned long mid_rate;
	struct clk_hw *sync_hw = &flexgen->sync.hw;
	struct clk_gate *config = to_clk_gate(sync_hw);
	bool control = flexgen->control_mode;
	u32 reg;

	pdiv_hw->clk = hw->clk;
	fdiv_hw->clk = hw->clk;

	if (control) {
		reg = readl(config->reg);
		if (((reg & BIT(config->bit_idx)) >> config->bit_idx) !=
		    !control) {
			reg &= ~BIT(config->bit_idx);
			reg |= !control << config->bit_idx;
			writel(reg, config->reg);
		}
	}

	mid_rate = clk_divider_ops.recalc_rate(pdiv_hw, parent_rate);

	return clk_divider_ops.recalc_rate(fdiv_hw, mid_rate);
}

static bool flexgen_set_parent_max_beatcnt(struct clk_hw *hw,
		struct clk *clk_parent, bool new_parent)
{
	struct flexgen *flexgen = to_flexgen(hw);
	int i, j;
	u8 beatdiv = 0;
	bool beatdiv_update = false;
	const struct clk_ops *div_ops = &clk_divider_ops;

	for (i = 0; i < flexgen->beat_parents; i++) {
		struct clk_hw *beatdiv_hw;
		u8 beatdiv_max = 0;
		unsigned long prate = __clk_get_rate(clk_parent);

		if (!flexgen->parents_clks[i].sync)
			continue;

		if (clk_parent != flexgen->parents_clks[i].clkp)
			continue;

		beatdiv_hw = &flexgen->beatdiv[i].hw;
		beatdiv = prate / div_ops->recalc_rate(beatdiv_hw, prate);

		/* Search lowest div */
		beatdiv_max = 1;
		for (j = 0; j < FLEXGEN_MAX_OUTPUTS; j++) {
			struct clk *clkp = flexgen->clks[j].clkp;
			unsigned long nrate;
			u8 div;

			if (!flexgen->clks[j].sync)
				continue;

			if (clk_parent != __clk_get_parent(clkp))
				continue;

			nrate = flexgen_recalc_rate(__clk_get_hw(clkp), prate);
			div = vclk_best_div(prate, nrate);
			if (div > beatdiv_max)
				beatdiv_max = div;
		}

		if (beatdiv_max != beatdiv) {
			unsigned long rate = 1;

			div_ops->set_rate(beatdiv_hw, rate, rate * beatdiv_max);
			beatdiv_update = true;
		}
		break;

	}

	/* Not a semi-sync parent, then leave */
	if (i == flexgen->beat_parents)
		return false;

	if (beatdiv_update) {
		for (i = 0; i < FLEXGEN_MAX_OUTPUTS; i++) {
			struct clk *clkp = flexgen->clks[i].clkp;

			if (flexgen->clks[i].sync &&
				(clk_parent ==
				__clk_get_parent(clkp)) &&
				flexgen_is_enabled(__clk_get_hw(clkp))) {
					flexgen_disable(__clk_get_hw(clkp));
					flexgen_enable(__clk_get_hw(clkp));
			}
		}
	} else {
		if (new_parent && flexgen_is_enabled(hw)) {
			flexgen_disable(hw);
			flexgen_enable(hw);
		}
	}

	return beatdiv_update;
}

static u8 flexgen_get_parent(struct clk_hw *hw)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *mux_hw = &flexgen->mux.hw;

	mux_hw->clk = hw->clk;

	return clk_mux_ops.get_parent(mux_hw);
}

static int flexgen_set_parent(struct clk_hw *hw, u8 index)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *mux_hw = &flexgen->mux.hw;
	struct clk *clk_parent = NULL;
	int ret, i;
	u8 pid;

	mux_hw->clk = hw->clk;

	pid = clk_mux_ops.get_parent(mux_hw);

	ret = clk_mux_ops.set_parent(mux_hw, index);
	if (ret)
		return ret;

	if (!flexgen->control_mode)
		return ret;

	/* find old parent's clock pointer */
	for (i = 0; i < flexgen->beat_parents; i++)
		if (pid == flexgen->parents_clks[i].input_id) {
			clk_parent = flexgen->parents_clks[i].clkp;
			break;
		}

	if (clk_parent)
		flexgen_set_parent_max_beatcnt(hw, clk_parent, false);

	flexgen_set_parent_max_beatcnt(hw, __clk_get_parent(hw->clk), true);

	return ret;
}

static long flexgen_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *prate)
{
	unsigned long div;

	/* Round div according to exact prate and wished rate */
	div = vclk_best_div(*prate, rate);

	if (__clk_get_flags(hw->clk) & CLK_SET_RATE_PARENT) {
		*prate = rate * div;
		return rate;
	} else {
		return *prate / div;
	}
}

static bool flexgen_set_max_beatcnt(struct clk_hw *hw, unsigned long rate,
				unsigned long prate)
{
	struct flexgen *flexgen = to_flexgen(hw);
	unsigned long final_div = 0;
	int i, j;
	u8 beatdiv = 0;
	bool beatdiv_update = false;
	const struct clk_ops *div_ops = &clk_divider_ops;

	final_div = vclk_best_div(prate, rate);

	for (i = 0; i < flexgen->beat_parents; i++) {
		struct clk *hw_clkp;
		struct clk_hw *beatdiv_hw;
		u8 beatdiv_max = 0;

		if (!flexgen->parents_clks[i].sync)
			continue;

		hw_clkp = __clk_get_parent(hw->clk);
		if (hw_clkp != flexgen->parents_clks[i].clkp)
			continue;

		beatdiv_hw = &flexgen->beatdiv[i].hw;
		beatdiv = prate / div_ops->recalc_rate(beatdiv_hw, prate);

		if (final_div > beatdiv) {
			div_ops->set_rate(beatdiv_hw, rate, rate * final_div);
			beatdiv_update = true;
			break;
		}

		if (final_div == beatdiv)
			break;

		/* if (final_div < beatdiv) */
		beatdiv_max = final_div;
		for (j = 0; j < FLEXGEN_MAX_OUTPUTS; j++) {
			struct clk *clkp = flexgen->clks[j].clkp;
			unsigned long nrate;
			u8 div;

			if (hw->clk == clkp)
				continue;

			if (!flexgen->clks[j].sync)
				continue;

			if (hw_clkp != __clk_get_parent(clkp))
				continue;

			nrate = flexgen_recalc_rate(__clk_get_hw(clkp), prate);
			div = vclk_best_div(prate, nrate);
			if (div > beatdiv_max)
				beatdiv_max = div;
		}

		if (beatdiv_max != beatdiv) {
			div_ops->set_rate(beatdiv_hw, rate, rate * beatdiv_max);
			beatdiv_update = true;
		}
		break;

	}

	return beatdiv_update;
}

static int flexgen_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *pdiv_hw = &flexgen->pdiv.hw;
	struct clk_hw *fdiv_hw = &flexgen->fdiv.hw;
	struct clk_hw *sync_hw = &flexgen->sync.hw;
	struct clk_gate *config = to_clk_gate(sync_hw);
	bool control = flexgen->control_mode;
	unsigned long pre_div = 0, final_div = 0;
	unsigned long fmax = BIT(flexgen->fdiv.width);
	unsigned long pmax = BIT(flexgen->pdiv.width);
	int ret = 0, i;
	bool beatdiv_update = false;
	u32 reg;

	if (rate == flexgen_recalc_rate(hw, parent_rate))
		return ret;

	pdiv_hw->clk = hw->clk;
	fdiv_hw->clk = hw->clk;

	final_div = vclk_best_div(parent_rate, rate);
	pre_div = 1; /* bypass prediv */

	/* Computing predivider & final divider ratios */
	if (final_div > fmax) {
		unsigned long f, p, div = final_div;

		final_div = fmax;
		for (f = fmax; f; f--) {
			p = div / f;
			if (!p)
				continue;
			if (p > pmax) {
				if (abs(div - (f * pmax)) <
				    abs(div - (final_div * pre_div))) {
					final_div = f;
					pre_div = pmax;
				}
				break;
			}
			if (abs(div - (f * p)) <
			    abs(div - (final_div * pre_div))) {
				/* Best case */
				final_div = f;
				pre_div = p;
			}
		}
	}

	if (control) {
		reg = readl(config->reg);
		reg &= ~BIT(config->bit_idx);
		reg |= !control << config->bit_idx;
		writel(reg, config->reg);

		beatdiv_update = flexgen_set_max_beatcnt(hw, rate,
							 rate * final_div);
	}

	/*
	 * Prediv is mainly targeted for low freq results, while final_div is
	 * the first to use for divs < 64.
	 * The other way could lead to 'duty cycle' issues.
	 */
	clk_divider_ops.set_rate(pdiv_hw, rate, rate * pre_div);
	ret = clk_divider_ops.set_rate(fdiv_hw, rate, rate * final_div);

	pr_debug("%s: requested div=%lu => prediv=%lu, findiv=%lu, sync=%d\n",
		__clk_get_name(hw->clk), vclk_best_div(parent_rate, rate),
		pre_div, final_div, control);

	if (!control)
		return ret;

	if (beatdiv_update) {
		for (i = 0; i < FLEXGEN_MAX_OUTPUTS; i++) {
			struct clk *clkp = flexgen->clks[i].clkp;

			if (flexgen->clks[i].sync &&
			    (__clk_get_parent(hw->clk) ==
			    __clk_get_parent(clkp)) &&
			    flexgen_is_enabled(__clk_get_hw(clkp))) {
					flexgen_disable(__clk_get_hw(clkp));
					flexgen_enable(__clk_get_hw(clkp));
			}
		}
	} else {
		u8 pid;
		struct clk_hw *mux_hw = &flexgen->mux.hw;
		struct clk *clk_parent = NULL;

		mux_hw->clk = hw->clk;
		pid = clk_mux_ops.get_parent(mux_hw);

		for (i = 0; i < flexgen->beat_parents; i++)
			if (pid == flexgen->parents_clks[i].input_id) {
				clk_parent = flexgen->parents_clks[i].clkp;
				break;
			}

		if (clk_parent && flexgen->parents_clks[i].sync
		    && flexgen_is_enabled(hw)) {
				flexgen_disable(hw);
				flexgen_enable(hw);
		}
	}

	return ret;
}

static const struct clk_ops flexgen_ops = {
	.enable = flexgen_enable,
	.disable = flexgen_disable,
	.is_enabled = flexgen_is_enabled,
	.get_parent = flexgen_get_parent,
	.set_parent = flexgen_set_parent,
	.round_rate = flexgen_round_rate,
	.recalc_rate = flexgen_recalc_rate,
	.set_rate = flexgen_set_rate,
};

static struct
clk *clk_register_flexgen(const char *name,
			  const char **parent_names, u8 num_parents,
			  void __iomem *reg, spinlock_t *lock, u32 idx,
			  unsigned long flexgen_flags,
			  struct clkgen_data *data) {
	struct flexgen *fgxbar;
	struct clk *clk;
	struct clk_init_data init;
	u32  xbar_shift, i;
	void __iomem *xbar_reg, *fdiv_reg;

	fgxbar = kzalloc(sizeof(struct flexgen), GFP_KERNEL);
	if (!fgxbar)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &flexgen_ops;
	init.flags = CLK_IS_BASIC | CLK_GET_RATE_NOCACHE | flexgen_flags;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	xbar_reg = reg + 0x18 + (idx & ~0x3);
	xbar_shift = (idx % 4) * 0x8;
	fdiv_reg = reg + 0x164 + idx * 4;

	/* Crossbar element config */
	fgxbar->mux.lock = lock;
	fgxbar->mux.mask = BIT(6) - 1;
	fgxbar->mux.reg = xbar_reg;
	fgxbar->mux.shift = xbar_shift;
	fgxbar->mux.table = NULL;


	/* Pre-divider's gate config (in xbar register)*/
	fgxbar->pgate.lock = lock;
	fgxbar->pgate.reg = xbar_reg;
	fgxbar->pgate.bit_idx = xbar_shift + 6;

	/* Pre-divider config */
	fgxbar->pdiv.lock = lock;
	fgxbar->pdiv.reg = reg + 0x58 + idx * 4;
	fgxbar->pdiv.width = 10;

	/* Final divider's gate config */
	fgxbar->fgate.lock = lock;
	fgxbar->fgate.reg = fdiv_reg;
	fgxbar->fgate.bit_idx = 6;

	/* Final divider config */
	fgxbar->fdiv.lock = lock;
	fgxbar->fdiv.reg = fdiv_reg;
	fgxbar->fdiv.width = 6;

	/* Final divider sync config */
	fgxbar->sync.lock = lock;
	fgxbar->sync.reg = fdiv_reg;
	fgxbar->sync.bit_idx = 7;

	fgxbar->control_mode = (data) ? data->clks[idx].sync : false;

	if (fgxbar->control_mode) {
		/* beatcnt divider config */
		fgxbar->beat_parents = data->beat_parents;
		for (i = 0; i < data->beat_parents; i++) {
			fgxbar->beatdiv[i].lock = lock;
			fgxbar->beatdiv[i].reg = reg + data->beatcnt[i].offset;
			fgxbar->beatdiv[i].width = 8;
			fgxbar->beatdiv[i].shift = 8 * i;
		}
		fgxbar->parents_clks = data->parent_clks;
		fgxbar->clks = data->clks;
	}

	fgxbar->hw.init = &init;

	clk = clk_register(NULL, &fgxbar->hw);
	if (IS_ERR(clk))
		kfree(fgxbar);
	else
		pr_debug("%s: parent %s rate %u\n",  __clk_get_name(clk),
			 __clk_get_name(clk_get_parent(clk)),
			 (unsigned int)clk_get_rate(clk));
	return clk;
}

static const char ** __init flexgen_get_parents(struct device_node *np,
						       int *num_parents)
{
	const char **parents;
	int nparents, i;

	nparents = of_count_phandle_with_args(np, "clocks", "#clock-cells");
	if (WARN_ON(nparents <= 0))
		return NULL;

	parents = kzalloc(nparents * sizeof(const char *), GFP_KERNEL);
	if (!parents)
		return NULL;

	for (i = 0; i < nparents; i++)
		parents[i] = of_clk_get_parent_name(np, i);

	*num_parents = nparents;
	return parents;
}

static struct clkgen_data clkgen_d0 = {
	.clk_flags = CLK_SET_RATE_PARENT,
	.clk_mode = 0,
};

static struct clkgen_data clkgen_d2 = {
	.clk_flags = CLK_SET_RATE_PARENT,
	.clk_mode = 1,
	.beat_parents = 4,
	.parent_clks = { {0, NULL, 1},
			 {1, NULL, 1},
			 {6, NULL, 1},
			 {7, NULL, 0}, },
	.beatcnt = { CLKGEN_FIELD(0x26c, 0xff, 0),
		     CLKGEN_FIELD(0x26c, 0xff, 8),
		     CLKGEN_FIELD(0x26c, 0xff, 16),
		     CLKGEN_FIELD(0x26c, 0xff, 24) },
	.clks = { {NULL, 1},
		  {NULL, 1},
		  {NULL, 1},
		  {NULL, 1},
		  {NULL, 1},
		  {NULL, 1},
		  {NULL, 1},
		  {NULL, 1},
		  {NULL, 1},
		  {NULL, 0},
		  {NULL, 0},
		  {NULL, 1},
		  {NULL, 0},
		  {NULL, 1},
		  {NULL, 0},
		  {NULL, 0}, },
};

static struct clkgen_data clkgen_d2_h418 = {
	.clk_flags = CLK_SET_RATE_PARENT,
	.clk_mode = 1,
	.beat_parents = 4,
	.parent_clks = { {0, NULL, 1},
			 {1, NULL, 1},
			 {6, NULL, 1},
			 {7, NULL, 0}, },
	.beatcnt = { CLKGEN_FIELD(0x26c, 0xff, 0),
		     CLKGEN_FIELD(0x26c, 0xff, 8),
		     CLKGEN_FIELD(0x26c, 0xff, 16),
		     CLKGEN_FIELD(0x26c, 0xff, 24) },
	.clks = { {NULL, 1}, /* CLK_PIX_MAIN_DISP */
		  {NULL, 0},
		  {NULL, 0},
		  {NULL, 0},
		  {NULL, 0},
		  {NULL, 1}, /* CLK_TMDS_HDMI_DIV2 */
		  {NULL, 1}, /* CLK_PIX_AUX_DISP */
		  {NULL, 1}, /* CLK_DENC */
		  {NULL, 1}, /* CLK_PIX_HDDAC */
		  {NULL, 1}, /* CLK_HDDAC */
		  {NULL, 1}, /* CLK_SDDAC */
		  {NULL, 1}, /* CLK_PIX_DVO */
		  {NULL, 1}, /* CLK_DVO */
		  {NULL, 1}, /* CLK_PIX_HDMI */
		  {NULL, 1}, /* CLK_TMDS_HDMI */
		  {NULL, 1}, }, /* CLK_REF_HDMIPHY */
};

static struct of_device_id flexgen_of_match[] = {
	{
		.compatible = "st,stih407-clkgend0",
		.data = &clkgen_d0,
	},
	{
		.compatible = "st,stih407-clkgend2",
		.data = &clkgen_d2,
	},
	{
		.compatible = "st,stih418-clkgend2",
		.data = &clkgen_d2_h418,
	},
	{}
};

struct clkgen_flex_conf {
	unsigned long flex_flags[FLEXGEN_MAX_OUTPUTS];
};

static struct clkgen_flex_conf st_flexgen_conf_407_c0 = {
	.flex_flags = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, CLK_IGNORE_UNUSED,
			CLK_IGNORE_UNUSED, CLK_IGNORE_UNUSED },
};

static struct clkgen_flex_conf st_flexgen_conf_407_d2 = {
	.flex_flags = { CLK_IGNORE_UNUSED, CLK_IGNORE_UNUSED, CLK_IGNORE_UNUSED,
			CLK_IGNORE_UNUSED, CLK_IGNORE_UNUSED, CLK_IGNORE_UNUSED,
			CLK_IGNORE_UNUSED, CLK_IGNORE_UNUSED, CLK_IGNORE_UNUSED,
			CLK_IGNORE_UNUSED, CLK_IGNORE_UNUSED, CLK_IGNORE_UNUSED,
			CLK_IGNORE_UNUSED, CLK_IGNORE_UNUSED, CLK_IGNORE_UNUSED,
			CLK_IGNORE_UNUSED },
};

static struct clkgen_flex_conf st_flexgen_conf_418_c0 = {
	.flex_flags = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, CLK_IGNORE_UNUSED,
			CLK_IGNORE_UNUSED, CLK_IGNORE_UNUSED, 0, 0, 0, 0, 0,
			0, 0, CLK_IGNORE_UNUSED },
};

static struct of_device_id flexconf_of_match[] = {
	{
		.compatible = "st,stih407-flexgen-conf-c0",
		.data = &st_flexgen_conf_407_c0,
	},
	{
		.compatible = "st,stih407-flexgen-conf-d2",
		.data = &st_flexgen_conf_407_d2,
	},
	{
		.compatible = "st,stih418-flexgen-conf-c0",
		.data = &st_flexgen_conf_418_c0,
	},
	{}
};

static void __init st_of_flexgen_setup(struct device_node *np)
{
	struct device_node *pnode;
	void __iomem *reg;
	struct clk_onecell_data *clk_data = NULL;
	const char **parents;
	int num_parents, i;
	spinlock_t *rlock;
	const struct of_device_id *match;
	struct clkgen_data *data = NULL;
	struct clkgen_flex_conf *conf_data = NULL;
	unsigned long clk_flags = 0;

	rlock = kzalloc(sizeof(spinlock_t), GFP_KERNEL);
	if (!rlock) {
		pr_err("%s: Failed to allocate lock\n", __func__);
		return;
	}

	spin_lock_init(rlock);

	pnode = of_get_parent(np);
	if (!pnode)
		return;

	reg = of_iomap(pnode, 0);
	if (!reg)
		return;

	match = of_match_node(flexconf_of_match, np);
	if (match)
		conf_data = (struct clkgen_flex_conf *)match->data;

	parents = flexgen_get_parents(np, &num_parents);
	if (!parents)
		return;

	match = of_match_node(flexgen_of_match, np);
	if (match) {
		data = (struct clkgen_data *)match->data;
		clk_flags = data->clk_flags;
	}

	if (data && data->clk_mode)
		/* get parent clks */
		for (i = 0; i < data->beat_parents; i++) {
			u32 pid = data->parent_clks[i].input_id;

			if (!data->parent_clks[i].sync)
				continue;

			if (pid >= num_parents)
				goto err;

			data->parent_clks[i].clkp = __clk_lookup(parents[pid]);
			if (IS_ERR(data->parent_clks[i].clkp))
				goto err;
		}

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		goto err;

	clk_data->clk_num = of_property_count_strings(np ,
			"clock-output-names");
	if (clk_data->clk_num <= 0) {
		pr_err("%s: Failed to get number of output clocks (%d)",
		       __func__, clk_data->clk_num);
		goto err;
	}

	clk_data->clks = kzalloc(clk_data->clk_num * sizeof(struct clk *),
			GFP_KERNEL);
	if (!clk_data->clks)
		goto err;

	for (i = 0; i < clk_data->clk_num; i++) {
		struct clk *clk;
		const char *clk_name;
		unsigned long flags = ((conf_data) ?
				       conf_data->flex_flags[i] : 0);

		if (of_property_read_string_index(np, "clock-output-names",
						  i, &clk_name)) {
			break;
		}

		/*
		 * If we read an empty clock name then the output is unused
		 */
		if (*clk_name == '\0')
			continue;

		clk = clk_register_flexgen(clk_name, parents, num_parents,
					   reg, rlock, i, clk_flags | flags,
					   data);

		if (IS_ERR(clk))
			goto err;

		clk_data->clks[i] = clk;
		if (data && data->clks[i].sync)
			data->clks[i].clkp = clk;
	}

	kfree(parents);
	of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);

	return;

err:
	if (clk_data)
		kfree(clk_data->clks);
	kfree(clk_data);
	kfree(parents);
	return;
}
CLK_OF_DECLARE(flexgen, "st,flexgen", st_of_flexgen_setup);

/*
 * Implementation for the Flexgen MuxDiv reference clocks
 * Last 2 entries in "clock-output-names" are 'ext_refclkout0' and
 * 'ext_refclkout1'. Hence if "flexgen-ref" is used then
 * last 2 entries need to be defined (either by name or "")
 */
#define FLEXGEN_REF_MUX_OFFSET 0x0
#define FLEXGEN_REF_DIV_OFFSET 0xC

static void __init st_of_flexgen_ref_setup(struct device_node *np)
{
	void __iomem *reg;
	struct clk_onecell_data *clk_data = NULL;
	const char **parents;
	int num_parents, i;

	reg = of_iomap(np, 0);
	if (!reg)
		return;

	parents = flexgen_get_parents(np, &num_parents);
	if (!parents)
		return;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		goto err;

	clk_data->clk_num = of_property_count_strings(np ,
			"clock-output-names");
	if (clk_data->clk_num <= 0) {
		pr_err("%s: Failed to get number of output clocks (%d)",
		       __func__, clk_data->clk_num);
		goto err;
	}

	clk_data->clks = kzalloc(clk_data->clk_num * sizeof(struct clk *),
				 GFP_KERNEL);
	if (!clk_data->clks)
		goto err;

	for (i = 0; i < clk_data->clk_num; i++) {
		struct clk *clk;
		const char *clk_name;
		struct clk_divider *div;
		struct clk_mux *mux;

		if (of_property_read_string_index(np, "clock-output-names",
						  i, &clk_name)) {
			break;
		}

		/*
		 * If we read an empty clock name then the output is unused
		 */
		if (*clk_name == '\0')
			continue;

		if (i < (clk_data->clk_num-2)) {
			clk = clk_register_mux(NULL, clk_name, parents,
					       num_parents, 0,
					       reg + FLEXGEN_REF_MUX_OFFSET,
					       4 * i, 1, 0, NULL);

			if (IS_ERR(clk))
				goto err;
		} else {
			div = kzalloc(sizeof(struct clk_divider), GFP_KERNEL);
			if (!div)
				break;

			mux = kzalloc(sizeof(struct clk_mux), GFP_KERNEL);
			if (!mux) {
				kfree(div);
				break;
			}

			div->reg = reg + FLEXGEN_REF_DIV_OFFSET;
			div->shift = 4 * i;
			div->width = 4;

			mux->reg = reg + FLEXGEN_REF_MUX_OFFSET;
			mux->shift = 4 * i;
			mux->mask = 0x1;

			clk = clk_register_composite(NULL, clk_name, parents,
						     num_parents, &mux->hw,
						     &clk_mux_ops, &div->hw,
						     &clk_divider_ops,
						     NULL, NULL,
						     CLK_GET_RATE_NOCACHE);
			if (IS_ERR(clk)) {
				kfree(div);
				kfree(mux);
				goto err;
			}
		}

		pr_debug("%s: parent %s rate %u\n",
			 __clk_get_name(clk),
			 __clk_get_name(clk_get_parent(clk)),
			 (unsigned int)clk_get_rate(clk));

		clk_data->clks[i] = clk;
	}

	kfree(parents);
	of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);

	return;

err:
	for (i = 0; i < clk_data->clk_num; i++) {
		struct clk_composite *composite;
		struct clk_hw *hw;

		if (!clk_data->clks[i])
			continue;

		hw = __clk_get_hw(clk_data->clks[i]);

		if (i < (clk_data->clk_num-2)) {
			kfree(container_of(hw, struct clk_mux, hw));
		} else {
			composite = container_of(hw, struct clk_composite, hw);
			kfree(container_of(composite->rate_hw,
					   struct clk_divider, hw));
			kfree(container_of(composite->mux_hw,
					   struct clk_mux, hw));
		}

		kfree(clk_data->clks[i]);
	}
	if (clk_data)
		kfree(clk_data->clks);
	kfree(clk_data);
	kfree(parents);
}
CLK_OF_DECLARE(flexgen_ref, "st,flexgen-ref", st_of_flexgen_ref_setup);
