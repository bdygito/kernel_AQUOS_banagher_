/*
 * cyttsp5_mt_common.c
 * Parade TrueTouch(TM) Standard Product V5 Multi-Touch Reports Module.
 * For use with Parade touchscreen controllers.
 * Supported parts include:
 * CYTMA5XX
 * CYTMA448
 * CYTMA445A
 * CYTT21XXX
 * CYTT31XXX
 *
 * Copyright (C) 2015 Parade Technologies
 * Copyright (C) 2012-2015 Cypress Semiconductor
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contact Parade Technologies at www.paradetech.com <ttdrivers@paradetech.com>
 *
 */

#include "cyttsp5_regs.h"
#include <linux/input/touchevent_notifier.h>

#define CYTTSP5_MT_NAME "cyttsp5_mt"

#define MT_PARAM_SIGNAL(md, sig_ost) PARAM_SIGNAL(md->pdata->frmwrk, sig_ost)
#define MT_PARAM_MIN(md, sig_ost) PARAM_MIN(md->pdata->frmwrk, sig_ost)
#define MT_PARAM_MAX(md, sig_ost) PARAM_MAX(md->pdata->frmwrk, sig_ost)
#define MT_PARAM_FUZZ(md, sig_ost) PARAM_FUZZ(md->pdata->frmwrk, sig_ost)
#define MT_PARAM_FLAT(md, sig_ost) PARAM_FLAT(md->pdata->frmwrk, sig_ost)

#define CYTTSP5_MT_NOTIFIER_DIAG_TOUCH_ENABLE
#define CYTTSP5_MT_SIMPLE_ORIENTATION_ENABLE

#define CYTTSP5_MT_SYSFS_DEFINE(name, mode, show_func, store_func) \
static struct device_attribute cyttsp5_mt_sysfs_##name = \
	__ATTR(name, mode, show_func, store_func)

#if defined(CYTTSP5_MT_NOTIFIER_DIAG_TOUCH_ENABLE)
#define TOUCH_DIAGPOLL_TIME 100

struct touch_info{
	unsigned short	x;
	unsigned short	y;
	unsigned char	state;
	unsigned char	wx;
	unsigned char	wy;
	unsigned char	z;
};

struct touch_diag_info{
	u8							tm_mode;
	int							event;
	wait_queue_head_t			wait;
	struct touch_info			*fingers;
};
static struct touch_diag_info cyttsp5_mt_diag;

static ssize_t cyttsp5_mt_sysfs_poll_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int retval;

	retval = wait_event_interruptible_timeout(cyttsp5_mt_diag.wait,
			cyttsp5_mt_diag.event == 1,
			msecs_to_jiffies(TOUCH_DIAGPOLL_TIME));

	if(0 == retval){
		/* time out */
		return -1;
	}

	return 0;
}
CYTTSP5_MT_SYSFS_DEFINE(touch_poll, S_IRUGO,
			cyttsp5_mt_sysfs_poll_show,
			NULL);

static ssize_t cyttsp5_mt_sysfs_touch_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i;
	static char workbuf[512];
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp5_mt_data *md = &cd->md;
	struct cyttsp5_sysinfo *si = md->si;
	int max_tch = si->sensing_conf_data.max_tch;

	if (cyttsp5_mt_diag.fingers == NULL) {
		return 0;
	}

	for (i = 0; i < max_tch; i++) {
		sprintf(workbuf, "id=%d,state=%d,x=%d,y=%d,wx=%d,wy=%d,z=%d\n",
							i,
							cyttsp5_mt_diag.fingers[i].state,
							cyttsp5_mt_diag.fingers[i].x,
							cyttsp5_mt_diag.fingers[i].y,
							cyttsp5_mt_diag.fingers[i].wx,
							cyttsp5_mt_diag.fingers[i].wy,
							cyttsp5_mt_diag.fingers[i].z);
		strcat(buf, workbuf);
	}

	cyttsp5_mt_diag.event = 0;

	return( strlen(buf) );
}
CYTTSP5_MT_SYSFS_DEFINE(touch_data, S_IRUGO,
			cyttsp5_mt_sysfs_touch_data_show,
			NULL);

#endif

#if defined(CYPRESS_CYTTSP5_LPWG_ENABLE)
static ssize_t cyttsp5_mt_sysfs_wake_event_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp5_mt_data *md = &cd->md;

	return scnprintf(buf, PAGE_SIZE, "%d,%d\n", md->lpwg.double_tap_x, md->lpwg.double_tap_y);
}
CYTTSP5_MT_SYSFS_DEFINE(wake_event, S_IRUGO,
			cyttsp5_mt_sysfs_wake_event_show,
			NULL);
#endif /* CYPRESS_CYTTSP5_LPWG_ENABLE */

static struct device_attribute *cyttsp5_mt_attrs[] = {
#if defined(CYTTSP5_MT_NOTIFIER_DIAG_TOUCH_ENABLE)
	&cyttsp5_mt_sysfs_touch_poll,
	&cyttsp5_mt_sysfs_touch_data,
#endif
#if defined(CYPRESS_CYTTSP5_LPWG_ENABLE)
	&cyttsp5_mt_sysfs_wake_event,
#endif /* CYPRESS_CYTTSP5_LPWG_ENABLE */
};
static void cyttsp5_mt_lift_all(struct cyttsp5_mt_data *md)
{
	int max = md->si->tch_abs[CY_TCH_T].max;

	if (md->num_prv_rec != 0) {
		if (md->mt_function.report_slot_liftoff)
			md->mt_function.report_slot_liftoff(md, max);
		input_sync(md->input);
		md->num_prv_rec = 0;
		touchevent_notifier_call_chain(0, NULL);
	}

#if defined(CYTTSP5_MT_NOTIFIER_DIAG_TOUCH_ENABLE)
	if (cyttsp5_mt_diag.fingers) {
		int i;
		struct cyttsp5_sysinfo *si = md->si;
		int max_tch = si->sensing_conf_data.max_tch;

		for (i = 0; i < max_tch; i++) {
			cyttsp5_mt_diag.fingers[i].state = 0;
			cyttsp5_mt_diag.fingers[i].x = 0;
			cyttsp5_mt_diag.fingers[i].y = 0;
			cyttsp5_mt_diag.fingers[i].wx = 0;
			cyttsp5_mt_diag.fingers[i].wy = 0;
			cyttsp5_mt_diag.fingers[i].z = 0;
		}
	}
#endif
}

static void cyttsp5_get_touch_axis(struct cyttsp5_mt_data *md,
	int *axis, int size, int max, u8 *xy_data, int bofs)
{
	int nbyte;
	int next;

	for (nbyte = 0, *axis = 0, next = 0; nbyte < size; nbyte++) {
		parade_debug(md->dev, DEBUG_LEVEL_2,
			"%s: *axis=%02X(%d) size=%d max=%08X xy_data=%p xy_data[%d]=%02X(%d) bofs=%d\n",
			__func__, *axis, *axis, size, max, xy_data, next,
			xy_data[next], xy_data[next], bofs);
		*axis = *axis + ((xy_data[next] >> bofs) << (nbyte * 8));
		next++;
	}

	*axis &= max - 1;

	parade_debug(md->dev, DEBUG_LEVEL_2,
		"%s: *axis=%02X(%d) size=%d max=%08X xy_data=%p xy_data[%d]=%02X(%d)\n",
		__func__, *axis, *axis, size, max, xy_data, next,
		xy_data[next], xy_data[next]);
}

static void cyttsp5_get_touch_hdr(struct cyttsp5_mt_data *md,
	struct cyttsp5_touch *touch, u8 *xy_mode)
{
	struct device *dev = md->dev;
	struct cyttsp5_sysinfo *si = md->si;
	enum cyttsp5_tch_hdr hdr;

	for (hdr = CY_TCH_TIME; hdr < CY_TCH_NUM_HDR; hdr++) {
		if (!si->tch_hdr[hdr].report)
			continue;
		cyttsp5_get_touch_axis(md, &touch->hdr[hdr],
			si->tch_hdr[hdr].size,
			si->tch_hdr[hdr].max,
			xy_mode + si->tch_hdr[hdr].ofs,
			si->tch_hdr[hdr].bofs);
		parade_debug(dev, DEBUG_LEVEL_2, "%s: get %s=%04X(%d)\n",
			__func__, cyttsp5_tch_hdr_string[hdr],
			touch->hdr[hdr], touch->hdr[hdr]);
	}

	parade_debug(dev, DEBUG_LEVEL_1,
		"%s: time=%X tch_num=%d lo=%d noise=%d counter=%d\n",
		__func__,
		touch->hdr[CY_TCH_TIME],
		touch->hdr[CY_TCH_NUM],
		touch->hdr[CY_TCH_LO],
		touch->hdr[CY_TCH_NOISE],
		touch->hdr[CY_TCH_COUNTER]);
}

static void cyttsp5_get_touch_record(struct cyttsp5_mt_data *md,
	struct cyttsp5_touch *touch, u8 *xy_data)
{
	struct device *dev = md->dev;
	struct cyttsp5_sysinfo *si = md->si;
	enum cyttsp5_tch_abs abs;

	for (abs = CY_TCH_X; abs < CY_TCH_NUM_ABS; abs++) {
		if (!si->tch_abs[abs].report)
			continue;
		cyttsp5_get_touch_axis(md, &touch->abs[abs],
			si->tch_abs[abs].size,
			si->tch_abs[abs].max,
			xy_data + si->tch_abs[abs].ofs,
			si->tch_abs[abs].bofs);
		parade_debug(dev, DEBUG_LEVEL_2, "%s: get %s=%04X(%d)\n",
			__func__, cyttsp5_tch_abs_string[abs],
			touch->abs[abs], touch->abs[abs]);
	}
}

static void cyttsp5_mt_process_touch(struct cyttsp5_mt_data *md,
	struct cyttsp5_touch *touch)
{
	struct device *dev = md->dev;
	struct cyttsp5_sysinfo *si = md->si;
	int tmp;
	bool flipped;


	/* Orientation is signed */
	touch->abs[CY_TCH_OR] = (int8_t)touch->abs[CY_TCH_OR];

	if (md->pdata->flags & CY_MT_FLAG_FLIP) {
		tmp = touch->abs[CY_TCH_X];
		touch->abs[CY_TCH_X] = touch->abs[CY_TCH_Y];
		touch->abs[CY_TCH_Y] = tmp;
		if (touch->abs[CY_TCH_OR] > 0)
			touch->abs[CY_TCH_OR] =
				md->or_max - touch->abs[CY_TCH_OR];
		else
			touch->abs[CY_TCH_OR] =
				md->or_min - touch->abs[CY_TCH_OR];
		flipped = true;
	} else
		flipped = false;

	if (md->pdata->flags & CY_MT_FLAG_INV_X) {
		if (flipped)
			touch->abs[CY_TCH_X] = si->sensing_conf_data.res_y -
				touch->abs[CY_TCH_X];
		else
			touch->abs[CY_TCH_X] = si->sensing_conf_data.res_x -
				touch->abs[CY_TCH_X];
		touch->abs[CY_TCH_OR] *= -1;
	}
	if (md->pdata->flags & CY_MT_FLAG_INV_Y) {
		if (flipped)
			touch->abs[CY_TCH_Y] = si->sensing_conf_data.res_x -
				touch->abs[CY_TCH_Y];
		else
			touch->abs[CY_TCH_Y] = si->sensing_conf_data.res_y -
				touch->abs[CY_TCH_Y];
		touch->abs[CY_TCH_OR] *= -1;
	}

#if 0
	/* Convert MAJOR/MINOR from mm to resolution */
	tmp = touch->abs[CY_TCH_MAJ] * 100 * si->sensing_conf_data.res_x;
	touch->abs[CY_TCH_MAJ] = tmp / si->sensing_conf_data.len_x;
	tmp = touch->abs[CY_TCH_MIN] * 100 * si->sensing_conf_data.res_x;
	touch->abs[CY_TCH_MIN] = tmp / si->sensing_conf_data.len_x;
#endif

#if defined(CYTTSP5_MT_SIMPLE_ORIENTATION_ENABLE)
	touch->abs[CY_TCH_OR] -= 128;
	if (touch->abs[CY_TCH_OR] <= -128) {
		touch->abs[CY_TCH_OR] = 255 + touch->abs[CY_TCH_OR];
	}

	if (touch->abs[CY_TCH_OR] < 64 && touch->abs[CY_TCH_OR] > -64) {
		touch->abs[CY_TCH_OR] = 0;
	}
	else {
		touch->abs[CY_TCH_OR] = 1;
	}
#endif /* CYTTSP5_MT_SIMPLE_ORIENTATION_ENABLE */

	parade_debug(dev, DEBUG_LEVEL_2, "%s: flip=%s inv-x=%s inv-y=%s x=%04X(%d) y=%04X(%d)\n",
		__func__, flipped ? "true" : "false",
		md->pdata->flags & CY_MT_FLAG_INV_X ? "true" : "false",
		md->pdata->flags & CY_MT_FLAG_INV_Y ? "true" : "false",
		touch->abs[CY_TCH_X], touch->abs[CY_TCH_X],
		touch->abs[CY_TCH_Y], touch->abs[CY_TCH_Y]);
}

static void cyttsp5_report_event(struct cyttsp5_mt_data *md, int event,
		int value)
{
	int sig = MT_PARAM_SIGNAL(md, event);

	if (sig != CY_IGNORE_VALUE)
		input_report_abs(md->input, sig, value);
}

static void cyttsp5_get_mt_touches(struct cyttsp5_mt_data *md,
		struct cyttsp5_touch *tch, int num_cur_tch)
{
	struct device *dev = md->dev;
	struct cyttsp5_sysinfo *si = md->si;
	int sig;
	int i, j, t = 0;
#if 1
	unsigned long *ids = NULL;
#else
	DECLARE_BITMAP(ids, si->tch_abs[CY_TCH_T].max);
#endif
	int mt_sync_count = 0;
	u8 *tch_addr;

#if 1
	ids = kzalloc(BITS_TO_LONGS(si->tch_abs[CY_TCH_T].max) * sizeof(unsigned long), GFP_KERNEL);
	if (!ids) {
		dev_err(dev, "%s: ids kzalloc error\n", __func__);
		return;
	}
#endif
	bitmap_zero(ids, si->tch_abs[CY_TCH_T].max);
	memset(tch->abs, 0, sizeof(tch->abs));

	for (i = 0; i < num_cur_tch; i++) {
		tch_addr = si->xy_data + (i * si->desc.tch_record_size);
		cyttsp5_get_touch_record(md, tch, tch_addr);

		/*  Discard proximity event */
		if (tch->abs[CY_TCH_O] == CY_OBJ_PROXIMITY) {
			parade_debug(dev, DEBUG_LEVEL_2, "%s: Discarding proximity event\n",
					__func__);
			continue;
		}

		/* Validate track_id */
		t = tch->abs[CY_TCH_T];
		if (t < md->t_min || t > md->t_max) {
			dev_err(dev, "%s: tch=%d -> bad trk_id=%d max_id=%d\n",
				__func__, i, t, md->t_max);
			if (md->mt_function.input_sync)
				md->mt_function.input_sync(md->input);
			mt_sync_count++;
			continue;
		}

		/* Lift-off */
		if (tch->abs[CY_TCH_E] == CY_EV_LIFTOFF) {
			parade_debug(dev, DEBUG_LEVEL_1, "%s: t=%d e=%d lift-off\n",
				__func__, t, tch->abs[CY_TCH_E]);
			goto cyttsp5_get_mt_touches_pr_tch;
		}

		/* Process touch */
		cyttsp5_mt_process_touch(md, tch);

		/* use 0 based track id's */
		t -= md->t_min;

		sig = MT_PARAM_SIGNAL(md, CY_ABS_ID_OST);
		if (sig != CY_IGNORE_VALUE) {
			if (md->mt_function.input_report)
				md->mt_function.input_report(md->input, sig,
						t, tch->abs[CY_TCH_O]);
			__set_bit(t, ids);
		}

		/* If touch type is hover, send P as distance, reset P */
		if (tch->abs[CY_TCH_O] == CY_OBJ_HOVER) {
			/* CY_ABS_D_OST signal must be in touch framework */
			cyttsp5_report_event(md, CY_ABS_D_OST,
					tch->abs[CY_TCH_P]);
			tch->abs[CY_TCH_P] = 0;
		} else
			cyttsp5_report_event(md, CY_ABS_D_OST, 0);


		/* all devices: position and pressure fields */
		for (j = 0; j <= CY_ABS_W_OST; j++) {
			if (!si->tch_abs[j].report)
				continue;
#if defined(CYPRESS_CYTTSP5_GLOVE_ENABLE)
			if ((CY_TCH_X + j) == CY_TCH_P && tch->abs[CY_TCH_O] == CY_OBJ_GLOVE) {
				cyttsp5_report_event(md, CY_ABS_X_OST + j,
						tch->abs[CY_TCH_X + j] += 0x0100);
			}
			else {
				cyttsp5_report_event(md, CY_ABS_X_OST + j,
						tch->abs[CY_TCH_X + j]);
			}
#else
			cyttsp5_report_event(md, CY_ABS_X_OST + j,
					tch->abs[CY_TCH_X + j]);
#endif /* CYPRESS_CYTTSP5_GLOVE_ENABLE */
		}

		/* Get the extended touch fields */
		for (j = 0; j < CY_NUM_EXT_TCH_FIELDS; j++) {
			if (!si->tch_abs[CY_ABS_MAJ_OST + j].report)
				continue;
			cyttsp5_report_event(md, CY_ABS_MAJ_OST + j,
					tch->abs[CY_TCH_MAJ + j]);
		}
		if (md->mt_function.input_sync)
			md->mt_function.input_sync(md->input);
		mt_sync_count++;

cyttsp5_get_mt_touches_pr_tch:
		parade_debug(dev, DEBUG_LEVEL_1,
			"%s: t=%d x=%d y=%d z=%d M=%d m=%d o=%d e=%d obj=%d tip=%d\n",
			__func__, t,
			tch->abs[CY_TCH_X],
			tch->abs[CY_TCH_Y],
			tch->abs[CY_TCH_P],
			tch->abs[CY_TCH_MAJ],
			tch->abs[CY_TCH_MIN],
			tch->abs[CY_TCH_OR],
			tch->abs[CY_TCH_E],
			tch->abs[CY_TCH_O],
			tch->abs[CY_TCH_TIP]);

#if defined(CYTTSP5_MT_NOTIFIER_DIAG_TOUCH_ENABLE)
		if (cyttsp5_mt_diag.fingers) {
			int max_tch = si->sensing_conf_data.max_tch;
			if (t < max_tch) {
				if(tch->abs[CY_TCH_E] == CY_EV_LIFTOFF) {
					cyttsp5_mt_diag.fingers[t].state = 0;
					cyttsp5_mt_diag.fingers[t].x = 0;
					cyttsp5_mt_diag.fingers[t].y = 0;
					cyttsp5_mt_diag.fingers[t].wx = 0;
					cyttsp5_mt_diag.fingers[t].wy = 0;
					cyttsp5_mt_diag.fingers[t].z = 0;
				}
				else {
					cyttsp5_mt_diag.fingers[t].state = tch->abs[CY_TCH_E] == CY_EV_NO_EVENT ? CY_EV_MOVE : tch->abs[CY_TCH_E];
					cyttsp5_mt_diag.fingers[t].x = tch->abs[CY_TCH_X];
					cyttsp5_mt_diag.fingers[t].y = tch->abs[CY_TCH_Y];
					cyttsp5_mt_diag.fingers[t].wx = tch->abs[CY_TCH_MAJ];
					cyttsp5_mt_diag.fingers[t].wy = tch->abs[CY_TCH_MIN];
					cyttsp5_mt_diag.fingers[t].z = tch->abs[CY_TCH_P];
				}
			}
		}
#endif
	}

	if (md->mt_function.final_sync)
		md->mt_function.final_sync(md->input,
				si->tch_abs[CY_TCH_T].max, mt_sync_count, ids);

	if(md->num_prv_rec == 0 && num_cur_tch > 0){
		touchevent_notifier_call_chain(1, NULL);
	}

	md->num_prv_rec = num_cur_tch;

#if defined(CYTTSP5_MT_NOTIFIER_DIAG_TOUCH_ENABLE)
	cyttsp5_mt_diag.event = 1;
	wake_up_interruptible(&cyttsp5_mt_diag.wait);
#endif

#if 1
	kfree(ids);
#endif
}

/* read xy_data for all current touches */
static int cyttsp5_xy_worker(struct cyttsp5_mt_data *md)
{
	struct device *dev = md->dev;
	struct cyttsp5_sysinfo *si = md->si;
	int max_tch = si->sensing_conf_data.max_tch;
	struct cyttsp5_touch tch;
	u8 num_cur_tch;
	int rc = 0;

	cyttsp5_get_touch_hdr(md, &tch, si->xy_mode + 3);

	num_cur_tch = tch.hdr[CY_TCH_NUM];
	if (num_cur_tch > max_tch) {
		dev_err(dev, "%s: Num touch err detected (n=%d)\n",
			__func__, num_cur_tch);
		num_cur_tch = max_tch;
	}

	if (tch.hdr[CY_TCH_LO]) {
		parade_debug(dev, DEBUG_LEVEL_1, "%s: Large area detected\n",
			__func__);
		if (md->pdata->flags & CY_MT_FLAG_NO_TOUCH_ON_LO)
			num_cur_tch = 0;
	}

	if (num_cur_tch == 0 && md->num_prv_rec == 0)
		goto cyttsp5_xy_worker_exit;

	/* extract xy_data for all currently reported touches */
	parade_debug(dev, DEBUG_LEVEL_2, "%s: extract data num_cur_tch=%d\n",
		__func__, num_cur_tch);
	if (num_cur_tch)
		cyttsp5_get_mt_touches(md, &tch, num_cur_tch);
	else
		cyttsp5_mt_lift_all(md);

	rc = 0;

cyttsp5_xy_worker_exit:
	return rc;
}

#if defined(CYPRESS_CYTTSP5_PROXIMITY_SUPPORT_ENABLE)
static int cyttsp5_mt_proximity_check(struct cyttsp5_mt_data *md)
{
	struct device *dev = md->dev;
	int data = -1;

	parade_debug(dev, DEBUG_LEVEL_2, "%s: [proximity] check start\n",
		__func__);
	PROX_dataread_func(&data);
	parade_debug(dev, DEBUG_LEVEL_2, "%s: [proximity] check end(data:%d)\n",
		__func__, data);

	if(data == CYPRESS_CYTTSP5_PROXIMITY_NEAR){
		return CYPRESS_CYTTSP5_PROXIMITY_NEAR;
	}

	return CYPRESS_CYTTSP5_PROXIMITY_FAR;
}
#endif /* CYPRESS_CYTTSP5_PROXIMITY_SUPPORT_ENABLE */

static void cyttsp5_mt_send_dummy_event(struct cyttsp5_core_data *cd,
		struct cyttsp5_mt_data *md)
{
#if defined(CYPRESS_CYTTSP5_LPWG_ENABLE)
	struct device *dev = md->dev;

	if (cd->gesture_id == GESTURE_DOUBLE_TAP && (cd->sleep_state == SS_SLEEP_ON)) {
		md->lpwg.double_tap_x = cd->gesture_data[1] << 8 | cd->gesture_data[0];
		md->lpwg.double_tap_y = cd->gesture_data[3] << 8 | cd->gesture_data[2];

#if defined(CYPRESS_CYTTSP5_PROXIMITY_SUPPORT_ENABLE)
		if (cyttsp5_mt_proximity_check(md) != CYPRESS_CYTTSP5_PROXIMITY_NEAR) {
			sysfs_notify(&dev->kobj, NULL, cyttsp5_mt_sysfs_wake_event.attr.name);

			mutex_lock(&cd->lpwg_wake_lock_on_mutex);

			if (cd->lpwg_wake_lock_on == false) {
				cd->lpwg_wake_lock_on = true;
				parade_debug(dev, DEBUG_LEVEL_1, "%s: wake_lock(lpwg_wake_lock)\n", __func__);
				wake_lock(&cd->lpwg_wake_lock);
			}

			mutex_unlock(&cd->lpwg_wake_lock_on_mutex);

			cancel_delayed_work(&cd->lpwg_wake_lock_timer);
			schedule_delayed_work(&cd->lpwg_wake_lock_timer,
									msecs_to_jiffies(CYPRESS_CYTTSP5_LPWG_WAKE_LOCK_TIMEOUT_MS));
		}
		else{
			parade_debug(dev, DEBUG_LEVEL_1, "%s: [LPWG] proximity near\n",
				__func__);
		}
#else /* CYPRESS_CYTTSP5_PROXIMITY_SUPPORT_ENABLE */
		sysfs_notify(&dev->kobj, NULL, cyttsp5_mt_sysfs_wake_event.attr.name);
#endif /* CYPRESS_CYTTSP5_PROXIMITY_SUPPORT_ENABLE */
	}
#else
#ifndef EASYWAKE_TSG6
	/* TSG5 EasyWake */
	unsigned long ids = 0;

	/* for easy wakeup */
	if (md->mt_function.input_report)
		md->mt_function.input_report(md->input, ABS_MT_TRACKING_ID,
			0, CY_OBJ_STANDARD_FINGER);
	if (md->mt_function.input_sync)
		md->mt_function.input_sync(md->input);
	if (md->mt_function.final_sync)
		md->mt_function.final_sync(md->input, 0, 1, &ids);
	if (md->mt_function.report_slot_liftoff)
		md->mt_function.report_slot_liftoff(md, 1);
	if (md->mt_function.final_sync)
		md->mt_function.final_sync(md->input, 1, 1, &ids);
#else
	/* TSG6 FW1.3 and above only. TSG6 FW1.0 - 1.2 does not */
	/*  support EasyWake, and this function will not be called */
	u8 key_value;

	switch (cd->gesture_id) {
	case GESTURE_DOUBLE_TAP:
		key_value = KEY_F1;
	break;
	case GESTURE_TWO_FINGERS_SLIDE:
		key_value = KEY_F2;
	break;
	case GESTURE_TOUCH_DETECTED:
		key_value = KEY_F3;
	break;
	case GESTURE_PUSH_BUTTON:
		key_value = KEY_F4;
	break;
	case GESTURE_SINGLE_SLIDE_DE_TX:
		key_value = KEY_F5;
	break;
	case GESTURE_SINGLE_SLIDE_IN_TX:
		key_value = KEY_F6;
	break;
	case GESTURE_SINGLE_SLIDE_DE_RX:
		key_value = KEY_F7;
	break;
	case GESTURE_SINGLE_SLIDE_IN_RX:
		key_value = KEY_F8;
	break;
	default:
	break;
	}

	input_report_key(md->input, key_value, 1);
	mdelay(10);
	input_report_key(md->input, key_value, 0);
	input_sync(md->input);
#endif
#endif /* CYPRESS_CYTTSP5_LPWG_ENABLE */
}

static int cyttsp5_mt_attention(struct device *dev)
{
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp5_mt_data *md = &cd->md;
	int rc;

	if (md->si->xy_mode[2] !=  md->si->desc.tch_report_id)
		return 0;

	/* core handles handshake */
	mutex_lock(&md->mt_lock);
	rc = cyttsp5_xy_worker(md);
	mutex_unlock(&md->mt_lock);
	if (rc < 0)
		dev_err(dev, "%s: xy_worker error r=%d\n", __func__, rc);

	return rc;
}

static int cyttsp5_mt_wake_attention(struct device *dev)
{
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp5_mt_data *md = &cd->md;

	mutex_lock(&md->mt_lock);
	cyttsp5_mt_send_dummy_event(cd, md);
	mutex_unlock(&md->mt_lock);
	return 0;
}

static int cyttsp5_startup_attention(struct device *dev)
{
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp5_mt_data *md = &cd->md;

	mutex_lock(&md->mt_lock);
	cyttsp5_mt_lift_all(md);
	mutex_unlock(&md->mt_lock);

	return 0;
}

static int cyttsp5_mt_suspend_attention(struct device *dev)
{
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp5_mt_data *md = &cd->md;

	mutex_lock(&md->mt_lock);
	cyttsp5_mt_lift_all(md);
	md->is_suspended = true;
	mutex_unlock(&md->mt_lock);

	pm_runtime_put(dev);

	return 0;
}

static int cyttsp5_mt_resume_attention(struct device *dev)
{
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp5_mt_data *md = &cd->md;

	pm_runtime_get(dev);

	mutex_lock(&md->mt_lock);
	md->is_suspended = false;
	mutex_unlock(&md->mt_lock);

	return 0;
}

static int cyttsp5_mt_open(struct input_dev *input)
{
	struct device *dev = input->dev.parent;
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp5_mt_data *md = &cd->md;

	pm_runtime_get_sync(dev);

	mutex_lock(&md->mt_lock);
	md->is_suspended = false;
	mutex_unlock(&md->mt_lock);

	parade_debug(dev, DEBUG_LEVEL_2, "%s: setup subscriptions\n", __func__);

	/* set up touch call back */
	_cyttsp5_subscribe_attention(dev, CY_ATTEN_IRQ, CYTTSP5_MT_NAME,
		cyttsp5_mt_attention, CY_MODE_OPERATIONAL);

	/* set up startup call back */
	_cyttsp5_subscribe_attention(dev, CY_ATTEN_STARTUP, CYTTSP5_MT_NAME,
		cyttsp5_startup_attention, 0);

	/* set up wakeup call back */
	_cyttsp5_subscribe_attention(dev, CY_ATTEN_WAKE, CYTTSP5_MT_NAME,
		cyttsp5_mt_wake_attention, 0);

	/* set up suspend call back */
	_cyttsp5_subscribe_attention(dev, CY_ATTEN_SUSPEND, CYTTSP5_MT_NAME,
		cyttsp5_mt_suspend_attention, 0);

	/* set up resume call back */
	_cyttsp5_subscribe_attention(dev, CY_ATTEN_RESUME, CYTTSP5_MT_NAME,
		cyttsp5_mt_resume_attention, 0);

	return 0;
}

static void cyttsp5_mt_close(struct input_dev *input)
{
	struct device *dev = input->dev.parent;
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp5_mt_data *md = &cd->md;

	_cyttsp5_unsubscribe_attention(dev, CY_ATTEN_IRQ, CYTTSP5_MT_NAME,
		cyttsp5_mt_attention, CY_MODE_OPERATIONAL);

	_cyttsp5_unsubscribe_attention(dev, CY_ATTEN_STARTUP, CYTTSP5_MT_NAME,
		cyttsp5_startup_attention, 0);

	_cyttsp5_unsubscribe_attention(dev, CY_ATTEN_WAKE, CYTTSP5_MT_NAME,
		cyttsp5_mt_wake_attention, 0);

	_cyttsp5_unsubscribe_attention(dev, CY_ATTEN_SUSPEND, CYTTSP5_MT_NAME,
		cyttsp5_mt_suspend_attention, 0);

	_cyttsp5_unsubscribe_attention(dev, CY_ATTEN_RESUME, CYTTSP5_MT_NAME,
		cyttsp5_mt_resume_attention, 0);

	mutex_lock(&md->mt_lock);
	if (!md->is_suspended) {
		pm_runtime_put(dev);
		md->is_suspended = true;
	}
	mutex_unlock(&md->mt_lock);
}

static int cyttsp5_setup_input_device(struct device *dev)
{
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp5_mt_data *md = &cd->md;
	int signal = CY_IGNORE_VALUE;
	int max_x, max_y, max_p, min, max;
	int max_x_tmp, max_y_tmp;
	int i;
	int rc;

	parade_debug(dev, DEBUG_LEVEL_2, "%s: Initialize event signals\n",
		__func__);
	__set_bit(EV_ABS, md->input->evbit);
	__set_bit(EV_REL, md->input->evbit);
	__set_bit(EV_KEY, md->input->evbit);
#ifdef INPUT_PROP_DIRECT
	__set_bit(INPUT_PROP_DIRECT, md->input->propbit);
#endif

	/* If virtualkeys enabled, don't use all screen */
	if (md->pdata->flags & CY_MT_FLAG_VKEYS) {
		max_x_tmp = md->pdata->vkeys_x;
		max_y_tmp = md->pdata->vkeys_y;
	} else {
		max_x_tmp = md->si->sensing_conf_data.res_x;
		max_y_tmp = md->si->sensing_conf_data.res_y;
	}

	/* get maximum values from the sysinfo data */
	if (md->pdata->flags & CY_MT_FLAG_FLIP) {
		max_x = max_y_tmp - 1;
		max_y = max_x_tmp - 1;
	} else {
		max_x = max_x_tmp - 1;
		max_y = max_y_tmp - 1;
	}
#if defined(CYPRESS_CYTTSP5_GLOVE_ENABLE)
	max_p = 511;
#else
	max_p = md->si->sensing_conf_data.max_z;
#endif /* CYPRESS_CYTTSP5_GLOVE_ENABLE */

	/* set event signal capabilities */
	for (i = 0; i < NUM_SIGNALS(md->pdata->frmwrk); i++) {
		signal = MT_PARAM_SIGNAL(md, i);
		if (signal != CY_IGNORE_VALUE) {
			__set_bit(signal, md->input->absbit);

			min = MT_PARAM_MIN(md, i);
			max = MT_PARAM_MAX(md, i);
			if (i == CY_ABS_ID_OST) {
				/* shift track ids down to start at 0 */
				max = max - min;
				min = min - min;
			}
#if defined(CYTTSP5_MT_SIMPLE_ORIENTATION_ENABLE)
			else if (i == CY_ABS_OR_OST) {
				min = 0;
				max = 1;
			}
#endif /* CYTTSP5_MT_SIMPLE_ORIENTATION_ENABLE */
			else if (i == CY_ABS_X_OST)
				max = max_x;
			else if (i == CY_ABS_Y_OST)
				max = max_y;
			else if (i == CY_ABS_P_OST)
				max = max_p;
#if 1
			else if (i == CY_ABS_MAJ_OST)
				max = 50;
			else if (i == CY_ABS_MIN_OST)
				max = 50;
#endif

			input_set_abs_params(md->input, signal, min, max,
				MT_PARAM_FUZZ(md, i), MT_PARAM_FLAT(md, i));
			parade_debug(dev, DEBUG_LEVEL_1,
				"%s: register signal=%02X min=%d max=%d\n",
				__func__, signal, min, max);
		}
	}

#if defined(CYTTSP5_MT_SIMPLE_ORIENTATION_ENABLE)
	md->or_min = 0;
	md->or_max = 1;
#else
	md->or_min = MT_PARAM_MIN(md, CY_ABS_OR_OST);
	md->or_max = MT_PARAM_MAX(md, CY_ABS_OR_OST);
#endif /* CYTTSP5_MT_SIMPLE_ORIENTATION_ENABLE */

	md->t_min = MT_PARAM_MIN(md, CY_ABS_ID_OST);
	md->t_max = MT_PARAM_MAX(md, CY_ABS_ID_OST);

	rc = md->mt_function.input_register_device(md->input,
			md->si->tch_abs[CY_TCH_T].max);
	if (rc < 0)
		dev_err(dev, "%s: Error, failed register input device r=%d\n",
			__func__, rc);
	else
		md->input_device_registered = true;

#if defined(CYPRESS_CYTTSP5_LPWG_ENABLE)
#else
#ifdef EASYWAKE_TSG6
	input_set_capability(md->input, EV_KEY, KEY_F1);
	input_set_capability(md->input, EV_KEY, KEY_F2);
	input_set_capability(md->input, EV_KEY, KEY_F3);
	input_set_capability(md->input, EV_KEY, KEY_F4);
	input_set_capability(md->input, EV_KEY, KEY_F5);
	input_set_capability(md->input, EV_KEY, KEY_F6);
	input_set_capability(md->input, EV_KEY, KEY_F7);
	input_set_capability(md->input, EV_KEY, KEY_F8);
#endif
#endif /* CYPRESS_CYTTSP5_LPWG_ENABLE */
	return rc;
}

static int cyttsp5_setup_input_attention(struct device *dev)
{
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp5_mt_data *md = &cd->md;
	int rc;

	md->si = _cyttsp5_request_sysinfo(dev);
	if (!md->si)
		return -EINVAL;

#if defined(CYTTSP5_MT_NOTIFIER_DIAG_TOUCH_ENABLE)
	cyttsp5_mt_diag.fingers = kzalloc(md->si->sensing_conf_data.max_tch * sizeof(struct touch_info), GFP_KERNEL);
	if (!cyttsp5_mt_diag.fingers) {
		dev_err(dev, "%s: Failed to allocate memory for cyttsp5_mt_diag.fingers\n",
				__func__);
	}
#endif

	rc = cyttsp5_setup_input_device(dev);

	_cyttsp5_unsubscribe_attention(dev, CY_ATTEN_STARTUP, CYTTSP5_MT_NAME,
		cyttsp5_setup_input_attention, 0);

	return rc;
}

int cyttsp5_mt_probe(struct device *dev)
{
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp5_mt_data *md = &cd->md;
	struct cyttsp5_platform_data *pdata = dev_get_platdata(dev);
	struct cyttsp5_mt_platform_data *mt_pdata;
	int rc = 0;

	if (!pdata || !pdata->mt_pdata) {
		dev_err(dev, "%s: Missing platform data\n", __func__);
		rc = -ENODEV;
		goto error_no_pdata;
	}
	mt_pdata = pdata->mt_pdata;

	cyttsp5_init_function_ptrs(md);

	mutex_init(&md->mt_lock);
	md->dev = dev;
	md->pdata = mt_pdata;
#if defined(CYTTSP5_MT_NOTIFIER_DIAG_TOUCH_ENABLE)
	memset(&cyttsp5_mt_diag, 0, sizeof(cyttsp5_mt_diag));
#endif


	/* Create the input device and register it. */
	parade_debug(dev, DEBUG_LEVEL_2,
		"%s: Create the input device and register it\n", __func__);
	md->input = input_allocate_device();
	if (!md->input) {
		dev_err(dev, "%s: Error, failed to allocate input device\n",
			__func__);
		rc = -ENODEV;
		goto error_alloc_failed;
	}

	if (md->pdata->inp_dev_name)
		md->input->name = md->pdata->inp_dev_name;
	else
		md->input->name = CYTTSP5_MT_NAME;
	scnprintf(md->phys, sizeof(md->phys), "%s/input%d", dev_name(dev),
			cd->phys_num++);
	md->input->phys = md->phys;
	md->input->dev.parent = md->dev;
	md->input->open = cyttsp5_mt_open;
	md->input->close = cyttsp5_mt_close;
	input_set_drvdata(md->input, md);

	/* get sysinfo */
	md->si = _cyttsp5_request_sysinfo(dev);

	if (md->si) {
		rc = cyttsp5_setup_input_device(dev);
		if (rc)
			goto error_init_input;
	} else {
		dev_err(dev, "%s: Fail get sysinfo pointer from core p=%p\n",
			__func__, md->si);
		_cyttsp5_subscribe_attention(dev, CY_ATTEN_STARTUP,
			CYTTSP5_MT_NAME, cyttsp5_setup_input_attention, 0);
	}

#if defined(CYTTSP5_MT_NOTIFIER_DIAG_TOUCH_ENABLE)
	init_waitqueue_head(&cyttsp5_mt_diag.wait);

	if (md->si) {
		cyttsp5_mt_diag.fingers = kzalloc(md->si->sensing_conf_data.max_tch * sizeof(struct touch_info), GFP_KERNEL);
		if (!cyttsp5_mt_diag.fingers) {
			dev_err(dev, "%s: Failed to allocate memory for cyttsp5_mt_diag.fingers\n",
					__func__);
		}
	}
#endif

	{
		int idx;
		for (idx = 0; idx < ARRAY_SIZE(cyttsp5_mt_attrs); idx++) {
			rc = device_create_file(dev,
					cyttsp5_mt_attrs[idx]);
			if (rc < 0) {
				dev_err(dev, "%s: Failed to create sysfs file\n",
					__func__);
				goto err_sysfs_create_file;
			}
		}
	}

	return 0;

#if defined(CYTTSP5_MT_NOTIFIER_DIAG_TOUCH_ENABLE)
err_sysfs_create_file:
	if (cyttsp5_mt_diag.fingers) {
		kfree(cyttsp5_mt_diag.fingers);
	}
#endif
error_init_input:
	input_free_device(md->input);
error_alloc_failed:
error_no_pdata:
	dev_err(dev, "%s failed.\n", __func__);
	return rc;
}

int cyttsp5_mt_release(struct device *dev)
{
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp5_mt_data *md = &cd->md;

#if defined(CYTTSP5_MT_NOTIFIER_DIAG_TOUCH_ENABLE)
	{
		int idx;
		for (idx = 0; idx < ARRAY_SIZE(cyttsp5_mt_attrs); idx++) {
			device_remove_file(dev, cyttsp5_mt_attrs[idx]);
		}
	}
	if (cyttsp5_mt_diag.fingers) {
		kfree(cyttsp5_mt_diag.fingers);
	}
#endif

	if (md->input_device_registered) {
		input_unregister_device(md->input);
	} else {
		input_free_device(md->input);
		_cyttsp5_unsubscribe_attention(dev, CY_ATTEN_STARTUP,
			CYTTSP5_MT_NAME, cyttsp5_setup_input_attention, 0);
	}

	return 0;
}
