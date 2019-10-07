/*
 * This header provides constants clk index STMicroelectronics
 * STiH419 SoC.
 */
#ifndef _DT_BINDINGS_CLK_STIH419
#define _DT_BINDINGS_CLK_STIH419
#include "stih418-clks.h"

/* CLOCKGEN C0 */
#undef CLK_TP
#define CLK_PROC_GDP_COMPO	31
#define CLK_STBE		40
#define CLK_PROC_DVP		43
#define CLK_DVP_PROC		43

/* CLOCKGEN D3 */
#define CLK_XP70_TSIO		8

#endif
