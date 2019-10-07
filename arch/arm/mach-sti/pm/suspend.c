/*
 * -------------------------------------------------------------------------
 * Copyright (C) 2014  STMicroelectronics
 * Author:	Sudeep Biswas		<sudeep.biswas@st.com>
 *		Francesco M. Virlinzi	<francesco.virlinzi@st.com>
 *		Laurent MEUNIER		<laurent.meunier@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License V.2 ONLY.  See linux/COPYING for more information.
 * ------------------------------------------------------------------------- */

#include <linux/irq.h>
#include <linux/io.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>

#include "suspend.h"
#include "suspend_internal.h"
#include "poke_table.h"

#define L2CC_WAY_MASK			0x10000
#define L2CC_AUX_CNTRL_REG_OFFSET	0x104
#define WAY_MASK_SIXTEEN_WAYS		0xffff
#define WAY_MASK_EIGHT_WAYS		0xff
#define WAY_SIZE_MASK			0x7
#define WAY_SIZE_SHIFT			17
#define MIN_WAY_SIZE			16384

/*
 * Using PM_SUSPEND_MAX + 1 ensures that the ID given to DCPS
 * will never conflict with suspend state IDs given by kernel
 */
#define DCPS_STATE			(PM_SUSPEND_MAX + 1)

/* List of functions implementing the suspend_ops */
static int sti_suspend_valid(suspend_state_t state);
static int sti_suspend_begin(suspend_state_t state);
static void sti_suspend_prepare(void);
static int sti_suspend_enter(suspend_state_t state);

/* States definitions */
/*
 * Currently three low power suspend modes
 * are supported: freeze, standby and mem.
 * freeze support is in-build in kernel and no
 * support from platform power code is required.
 * standby and mem require platfrom code support.
 * They require explicit callback support.
 * It is to be noted that standby is mapped to HPS
 * and mem is mapped to CPS.
 */
struct sti_hw_state_desc sti_hw_pltf_states[MAX_SOC_LOW_POWER_STATES],
	sti_hw_states[MAX_SOC_LOW_POWER_STATES] = {
	{
		.target_state = PM_SUSPEND_STANDBY,
		.desc = "HOST PASSIVE STANDBY",
		.setup = sti_hps_setup,
		.enter = sti_hps_enter,
		.clock_state = CLOCK_REDUCED,
		.ddr_state = DDR_SR,
		.dcps_enter = NULL,
	},
	{
		.target_state = PM_SUSPEND_MEM,
		.desc =	"CONTROLLER PASSIVE STANDBY",
		.setup = sti_cps_setup,
		.enter = sti_cps_enter,
		.clock_state = CLOCKS_OFF,
		.ddr_state = DDR_SR,
		/*
		 * sti_cps_dcps_enter is the function provided by CPS
		 * implementation for entering DCPS. Suspend
		 * framework calls this function to enter DCPS if a
		 * DCPS state is not found.
		 */
		.dcps_enter = sti_cps_dcps_enter,
	},
	{
		.target_state = DCPS_STATE,
		.desc = "DCPS DDR OFF",
		.setup = sti_dcps_ddr_off_setup,
		.enter = sti_dcps_ddr_off_enter,
		.clock_state = CLOCKS_OFF,
		.ddr_state = DDR_OFF,
	},
};

struct sti_platform_suspend sti_suspend = {
	.hw_states = sti_hw_pltf_states,
	.hw_state_nr = 0,
	.index = -1,
	.ops.valid = sti_suspend_valid,
	.ops.begin = sti_suspend_begin,
	.ops.enter = sti_suspend_enter,
};

static const unsigned long suspend_end_table[4] = { OP_END_POKES, 0, 0, 0 };

enum tbl_level {
	TBL_ENTER,
	TBL_EXIT
};

/*
 * The below object is defined for STiH407 family SoCs.
 * This will differ for other SoCs such as ORLY, etc.
 * Support added for only STiH407 family SoCs
 */
static const struct sti_low_power_syscfg_info ddr3_syscfg_info = {
	.ddr3_cfg_offset = 0x160,
	.ddr3_stat_offset = 0x920,
};

static const struct sti_low_power_syscfg_info ddr3_syscfg_info_new = {
	.ddr3_cfg_offset = 0x338,
	.ddr3_stat_offset = 0x948,
};

static struct of_device_id suspend_of_match[] = {
	/* For STiH407 family SoCs */
	{
		/* Supporting 407 family SoCs */
		.compatible = "st,stih407-ddr-controller",
		.data = &ddr3_syscfg_info,
	},
	{
		.compatible = "st,stih407-ddr-controller_new",
		.data = &ddr3_syscfg_info_new,
	},
	/* Add compatible and data here and in the DT to support other SoCs */
	{}
};

static int sti_suspend_valid(suspend_state_t state)
{
	int i;

	for (i = 0; i < sti_suspend.hw_state_nr; i++) {
		/*  check if the right target state */
		if (state == sti_suspend.hw_states[i].target_state) {
			pr_info("sti pm: support mode: %s\n",
				sti_suspend.hw_states[i].desc);
			return true;
		}
	}

	/*  if not found, then not valid must return false */
	return false;
}

static int sti_suspend_begin(suspend_state_t state)
{
	int i;

	sti_suspend.index = -1;

	for (i = 0; i < sti_suspend.hw_state_nr; i++) {
		/*  check if the right target state */
		if (state == sti_suspend.hw_states[i].target_state) {
			sti_suspend.index = i;
			return 0;
		}
	}

	return -EINVAL;
}

static void sti_copy_suspend_table(struct sti_suspend_table *table,
				   void **__va_add,
				   enum tbl_level level)
{
	unsigned long const *tbl_ptr;
	unsigned long tbl_len_byte;

	if (level == TBL_ENTER) {
		tbl_ptr = table->enter;
		tbl_len_byte = table->enter_size;
	} else {
		tbl_ptr = table->exit;
		tbl_len_byte = table->exit_size;
	}

	/*
	 * if table->base_address is zero, this means no patching
	 * of poke table. Poke table is already set.
	 */
	if (!table->base_address)
		memcpy(*__va_add, tbl_ptr, tbl_len_byte);
	else
		patch_poke_table_copy(*__va_add, tbl_ptr,
				      table->base_address,
				      tbl_len_byte/sizeof(long));

	*__va_add += tbl_len_byte;
}

static void sti_insert_poke_table_end(void **__va_add)
{
	memcpy(*__va_add, suspend_end_table,
	       ARRAY_SIZE(suspend_end_table) * sizeof(long));

	*__va_add += ARRAY_SIZE(suspend_end_table) * sizeof(long);
}

/*
 * enter_dcps is responsible for shutdown of the system
 * Execution will never return from enter_dcps.
 * There are two options : Switch off the host domain
 * with DDR powered off and Switch off the host domain
 * with DDR in self refresh. In both cases
 * SBC will be powered, and will snoop for a wake-up
 * event. If a wake-up is received, the system is rebooted.
 * enter_dcps will first check for a DCPS state
 * implementation, if found, it will enter the
 * state (that should switch off the DDR). Otherwise
 * if DCPS state is not found, it falls back to
 * the suspend states. If a suspend state implements
 * DCPS entry, the path is chosen for execution.
 */
void enter_dcps(void)
{
	int i = 0;
	int alt_state_index = -1;

	/* search for the DCPS state */
	for (; i < sti_suspend.hw_state_nr; i++) {
		if (sti_suspend.hw_states[i].target_state ==
			DCPS_STATE) {
			sti_suspend.index = i;
			break;
		}

		/* in parallel search if suspend states implement DCPS entry */
		if (sti_suspend.hw_states[i].dcps_enter &&
				alt_state_index == -1)
				alt_state_index = i;
	}

	/*
	 * DCPS state not found, fallback to a suspend state
	 * that implements DCPS entry, if multiple implements,
	 * chosen is the first one !
	 */
	if (i == sti_suspend.hw_state_nr) {
		if (alt_state_index == -1)
			/* fallback failed, DCPS cant be entered, just relax */
			goto relax;

		sti_suspend.index = alt_state_index;
		/* enter the first suspend state that implements DCPS entry */
		sti_suspend.hw_states[sti_suspend.index].
			dcps_enter(&sti_suspend.hw_states[sti_suspend.index]);
		/* relax till power dies */
		goto relax;
	}

	/* DCPS state found, enter the state */
	sti_suspend.hw_states[sti_suspend.index].
		enter(&sti_suspend.hw_states[sti_suspend.index]);

relax:
	while (1)
		cpu_relax();
}

static int sti_get_lockable_size(struct sti_hw_state_desc *state)
{
	struct sti_suspend_table *table;
	int buffer_size = 0;

	buffer_size = sti_suspend_on_buffer_sz + sti_pokeloop_sz;

	list_for_each_entry(table, &state->state_tables, node)
		buffer_size += table->enter_size + table->exit_size;

	buffer_size += 2 * sizeof(struct poke_operation);

	return buffer_size;
}

static void sti_prepare(struct sti_hw_state_desc *state)
{
	void *__iomem __va_buf = state->buffer_data.sti_buffer_code;
	unsigned int sz;

	memcpy(state->buffer_data.sti_buffer_code, sti_suspend_on_buffer,
	       sti_suspend_on_buffer_sz);

	__va_buf += sti_suspend_on_buffer_sz;
	__va_buf = (void *)L2CC_CACHE_ALIGN((u32)__va_buf);

	state->buffer_data.sti_locking_code = __va_buf;

	memcpy(__va_buf, sti_suspend_lock_code, sti_suspend_lock_code_sz);

	sz = sti_get_lockable_size(state);
	state->buffer_data.sz = L2CC_CACHE_ALIGN(sz);
}

static void sti_suspend_prepare(void)
{
	int index;
	struct sti_suspend_table *table;
	void *__va_buf;

	index = sti_suspend.index;

	pr_info("sti pm: Copying poke table & poke loop to buffer\n");
	__va_buf = sti_suspend.hw_states[index].cache_buffer;

	/* copy the __pokeloop code in buffer*/
	sti_suspend.hw_states[index].buffer_data.pokeloop = __va_buf;

	memcpy(__va_buf, sti_pokeloop, sti_pokeloop_sz);
	__va_buf += sti_pokeloop_sz;

	/* copy the entry_tables in buffer */
	sti_suspend.hw_states[index].buffer_data.table_enter = __va_buf;

	list_for_each_entry(table,
			    &sti_suspend.hw_states[index].state_tables,
			    node) {
		if (!table->enter)
			continue;

		sti_copy_suspend_table(table, &__va_buf, TBL_ENTER);
	}

	sti_insert_poke_table_end(&__va_buf);

	/* copy the exit_data_table in buffer */
	sti_suspend.hw_states[index].buffer_data.table_exit = __va_buf;

	list_for_each_entry_reverse(table,
				    &sti_suspend.hw_states[index].
				    state_tables,
				    node) {
		if (!table->exit)
			continue;

		sti_copy_suspend_table(table, &__va_buf, TBL_EXIT);
	}

	sti_insert_poke_table_end(&__va_buf);

	sti_suspend.hw_states[index].buffer_data.sti_buffer_code = __va_buf;

	/*
	 * call prepare() for target state specific code to be
	 * copied to buffer
	 */
	pr_info("sti pm: entering prepare:%s\n",
		sti_suspend.hw_states[index].desc);

	sti_prepare(&sti_suspend.hw_states[index]);
}

static int sti_suspend_enter(suspend_state_t state)
{
	if (sti_suspend.index == -1 ||
	    state != sti_suspend.hw_states[sti_suspend.index].target_state)
			return -EINVAL;

	pr_info("sti pm: entering suspend:%s\n",
		sti_suspend.hw_states[sti_suspend.index].desc);

	if (!sti_suspend.hw_states[sti_suspend.index].enter) {
		pr_err("sti pm: state enter() is NULL\n");
		return -EFAULT;
	}

	return sti_suspend.hw_states[sti_suspend.index].
			enter(&sti_suspend.hw_states[sti_suspend.index]);
}

/*
 * sti_gic_set_wake is called whenever any driver
 * that is capable of wakeup calls enable_irq_wakeup().
 * This simply checks if valid IRQ number is given and
 * does not require to do anything else. If irq number
 * is invalid then error is returned.
 */
static int sti_gic_set_wake(struct irq_data *d, unsigned int on)
{
	if (d->irq <= MAX_GIC_SPI_INT)
		return 0;

	return -ENXIO;
}

static int __init sti_suspend_setup(void)
{
	int i;
	int ret = 0;
	struct device_node *np, *child, *np_ref;
	unsigned long ddr_pctl_addresses_va[MAX_DDR_PCTL_NUMBER];
	unsigned long ddr_pctl_addresses_pa[MAX_DDR_PCTL_NUMBER];
	unsigned int nr_ddr_pctl = 0;
	const struct of_device_id *match = NULL;
	struct sti_ddr3_low_power_info lp_info[MAX_DDR_PCTL_NUMBER];
	void __iomem *cleaner[(2 * MAX_DDR_PCTL_NUMBER) + 1];
	int cleaner_index = 0;
	unsigned int lock_code_sz, sz;
	bool register_suspend_ops = false;
	bool ddr_sr_ack_pctl = false;

	np = of_find_compatible_node(NULL, NULL, "arm,pl310-cache");
	if (IS_ERR_OR_NULL(np)) {
		pr_err("sti pm cps: L2 Cache controller entry not found in DT\n");
		return -ENODEV;
	}

	sti_suspend.l2cachebase = of_iomap(np, 0);
	if (!sti_suspend.l2cachebase) {
		of_node_put(np);
		return -ENODEV;
	}

	cleaner[cleaner_index++] = sti_suspend.l2cachebase;

	of_node_put(np);

	/* Read Cache type register to know the way mask */
	sti_suspend.l2waymask = readl_relaxed(sti_suspend.l2cachebase +
						L2CC_AUX_CNTRL_REG_OFFSET);

	if (sti_suspend.l2waymask & L2CC_WAY_MASK)
		sti_suspend.l2waymask = WAY_MASK_SIXTEEN_WAYS; /* 16 ways */
	else
		sti_suspend.l2waymask = WAY_MASK_EIGHT_WAYS; /* 8 ways */

	np = of_find_node_by_name(NULL, "ddr-pctl-controller");
	if (IS_ERR_OR_NULL(np))
		goto err;

	for_each_child_of_node(np, child) {
		struct resource res;

		if (of_address_to_resource(child, 0, &res)) {
			of_node_put(np);
			goto err;
		}

		ddr_pctl_addresses_pa[nr_ddr_pctl] = res.start;

		ddr_pctl_addresses_va[nr_ddr_pctl] = (unsigned int)
				ioremap(res.start, resource_size(&res));

		if (!ddr_pctl_addresses_va[nr_ddr_pctl]) {
			of_node_put(np);
			goto err;
		}
		cleaner[cleaner_index++] = (void *)
					ddr_pctl_addresses_va[nr_ddr_pctl];

		match = of_match_node(suspend_of_match, child);
		if (!match) {
			lp_info[nr_ddr_pctl].sysconf_base = 0;
			nr_ddr_pctl++;
			continue;
		}

		np_ref = of_parse_phandle(child, "st,syscfg", 0);
		if (IS_ERR_OR_NULL(np_ref)) {
			of_node_put(np);
			goto err;
		}

		lp_info[nr_ddr_pctl].sysconf_base = (unsigned int)
							of_iomap(np_ref, 0);

		if (!lp_info[nr_ddr_pctl].sysconf_base) {
			of_node_put(np_ref);
			of_node_put(np);
			goto err;
		}

		cleaner[cleaner_index++] = (void *)
					lp_info[nr_ddr_pctl].sysconf_base;

		of_node_put(np_ref);

		lp_info[nr_ddr_pctl].sysconf_info =
				*((struct sti_low_power_syscfg_info *)
				match->data);

		nr_ddr_pctl++;
	}

	ddr_sr_ack_pctl = of_property_read_bool(np, "st,ddr_sr_ack_pctl");

	of_node_put(np);

	for (i = 0; i < MAX_SOC_LOW_POWER_STATES; i++) {
		struct sti_hw_state_desc *hw_state = sti_suspend.hw_states;
		struct page *page;

		hw_state += sti_suspend.hw_state_nr;
		*hw_state = sti_hw_states[i];

		ret = hw_state->setup(hw_state, ddr_pctl_addresses_va,
				      nr_ddr_pctl,
				      lp_info,
				      ddr_sr_ack_pctl);
		if (ret)
			continue;

		sz = sti_get_lockable_size(hw_state);

		lock_code_sz = L2CC_CACHE_ALIGN(sz) + sti_suspend_lock_code_sz;
		if (lock_code_sz > PAGE_SIZE ||
		    lock_code_sz > sti_get_l2cc_way_size()) {
			pr_err("sti : lockablecode + locking code > pagesize/way");
			continue;
		}

		page = alloc_page(GFP_KERNEL);
		hw_state->cache_buffer = (unsigned char *)vmap(&page, 1, VM_MAP,
						       PAGE_KERNEL_EXEC);
		if (!hw_state->cache_buffer) {
			pr_err("sti pm : cache buffer alloc failed in setup\n");
			continue;
		}

		sti_suspend.index = sti_suspend.hw_state_nr;
		sti_suspend_prepare();

		if (hw_state->target_state != DCPS_STATE)
			register_suspend_ops = true;

		sti_suspend.hw_state_nr++;
	}

	if (!sti_suspend.hw_state_nr)
		goto err;

	if (register_suspend_ops)
		suspend_set_ops(&sti_suspend.ops);

	 /*
	  * irqchip's irq_set_wake() is called whenever any driver
	  * that is capable of wakeup calls enable_irq_wakeup()
	  * If bsp code set this, its fine, otherwise it wont
	  * work. So for safety we set this from here too. It does
	  * nothing apart from checking the validity of the irq
	  * number that can possibly give a wakeup. This
	  * assignment and the corresponding function can be
	  * removed if you are sure that somebody set this before
	  * with a properly written function. Currently in 3.10 kernel
	  * nobody is setting this. Hence this is done by the power
	  * code.
	  */
	if (!gic_arch_extn.irq_set_wake)
		gic_arch_extn.irq_set_wake = sti_gic_set_wake;

	pm_power_off = enter_dcps;

	pr_info("sti pm: Suspend and PowerDown support registered\n");

	return 0;
err:
	for (i = 0; i < cleaner_index; i++)
		iounmap(cleaner[i]);

	return -ENODEV;
}

/*
 * helper function to calculate l2cc way size
 */
unsigned int sti_get_l2cc_way_size(void)
{
	u32 l2ccaux_reg;
	unsigned int way_size_code;

	u32 l2cc_way_size[] = {MIN_WAY_SIZE,
			       MIN_WAY_SIZE,
			       2 * MIN_WAY_SIZE,
			       4 * MIN_WAY_SIZE,
			       8 * MIN_WAY_SIZE,
			       16 * MIN_WAY_SIZE,
			       32 * MIN_WAY_SIZE,
			       32 * MIN_WAY_SIZE,
			};


	l2ccaux_reg = readl_relaxed(sti_suspend.l2cachebase +
						L2CC_AUX_CNTRL_REG_OFFSET);

	way_size_code = (l2ccaux_reg >> WAY_SIZE_SHIFT) & WAY_SIZE_MASK;

	return l2cc_way_size[way_size_code];
}

module_init(sti_suspend_setup);
