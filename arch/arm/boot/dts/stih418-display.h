/*
 * Copyright (C) 2013 STMicroelectronics Limited.
 * Author(s): Giuseppe Condorelli <giuseppe.condorelli@st.com>
 *            Carmelo Amoroso <carmelo.amoroso@st.com>
 *            Giuseppe Cavallaro <peppe.cavallaro@st.com>
 *            Nunzio Raciti <nunzio.raciti@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef STIH418_DISPLAY_H_
#define STIH418_DISPLAY_H_

/* display definitions */

#define OUTPUT_IDX_MAIN 0
#define OUTPUT_IDX_HDMI 1
#define OUTPUT_IDX_AUX  2
#define OUTPUT_IDX_DVO  3
#define OUTPUT_COUNT    4

#define PLANE_IDX_GDP1       0
#define PLANE_IDX_GDP2       1
#define PLANE_IDX_GDP3       2
#define PLANE_IDX_GDP4       3
#define PLANE_IDX_VID_MAIN_1 4
#define PLANE_IDX_VID_MAIN_2 5
#define PLANE_IDX_VBI_MAIN   6
#define PLANE_IDX_VBI_AUX    7
#define PLANE_COUNT          8

#define SOURCE_COUNT PLANE_COUNT

#define STMCORE_PLANE_VIDEO     1 /* (1L<<0) */
#define STMCORE_PLANE_GFX       2 /* (1L<<1) */
#define STMCORE_PLANE_PREFERRED 4 /* (1L<<2) */
#define STMCORE_PLANE_SHARED    8 /* (1L<<3) */
#define STMCORE_PLANE_MEM_VIDEO 16 /* (1L<<4) */
#define STMCORE_PLANE_MEM_SYS   32 /* (1L<<5) */
#define STMCORE_PLANE_MEM_ANY   48 /*(STMCORE_PLANE_MEM_VIDEO|STMCORE_PLANE_MEM_SYS)*/

#define STiH418_REGISTER_BASE     0x08D00000
#define STiH418_DENC_BASE         0x0

#define SDTP_UNPACED            0
#define SDTP_HDMI1_IFRAME       1
#define SDTP_DENC1_TELETEXT     2
#define SDTP_DENC2_TELETEXT     3
#define SDTP_LAST_ENTRY         3

#endif /* STIH418_DISPLAY_H_ */
