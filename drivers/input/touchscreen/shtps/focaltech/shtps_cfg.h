/*
 * FocalTech ft8707 TouchScreen driver.
 *
 * Copyright (c) 2016  Focal tech Ltd.
 * Copyright (c) 2016, Sharp. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __SHTPS_CFG_H__
#define __SHTPS_CFG_H__
/* --------------------------------------------------------------------------- */

#if 0 /** For build test */
	#undef CONFIG_SHARP_TPS_FOCALTECH_FT8707

	#define CONFIG_SHARP_TPS_FOCALTECH_FT8707
#endif

#include <linux/input/shtps_dev.h>
/* --------------------------------------------------------------------------- */
#if defined(CONFIG_SHARP_TPS_FOCALTECH_FT8707)
	#include "ft8707/shtps_cfg_ft8707.h"

#else
	#include "ft8707/shtps_cfg_ft8707.h"

#endif

/* --------------------------------------------------------------------------- */
#endif /* __SHTPS_CFG_H__ */
