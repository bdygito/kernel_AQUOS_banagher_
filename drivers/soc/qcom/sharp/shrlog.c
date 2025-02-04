/* drivers/sharp/shlog/shrlog.c
 * 
 * Copyright (C) 2010 Sharp Corporation
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

/*
 *
 *
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init_task.h>
#include <soc/qcom/sharp/shrlog.h>
#include <soc/qcom/sharp/shrlog_restart.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <asm-generic/sections.h>
#include <asm-generic/percpu.h>
#include <asm/memory.h>
#include <soc/qcom/scm.h>
#include <asm/ftrace.h>

#if defined(CONFIG_ARM) && defined(CONFIG_SHARP_SHLOG_TASKLIST)
#include <asm/unwind.h>
#endif /* CONFIG_ARM && CONFIG_SHARP_SHLOG_TASKLIST */

#if defined(CONFIG_SHARP_PANIC_ON_PWRKEY)
#include <linux/input.h>
#include <linux/hrtimer.h>
#include <uapi/linux/input.h>
#endif /* CONFIG_SHARP_PANIC_ON_PWRKEY */

#include <asm/cacheflush.h>

#if defined(CONFIG_SHARP_HANDLE_PANIC) || defined(CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE)
#include <linux/input/qpnp-power-on.h>
#endif /* CONFIG_SHARP_HANDLE_PANIC || CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE */

#if defined(CONFIG_SHARP_SHLOG_LATESTSTACK)
#include "../../../../kernel/sched/sched.h"
#endif /* CONFIG_SHARP_SHLOG_LATESTSTACK */

#if defined(CONFIG_KALLSYMS) && defined(CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE)
#include <linux/kallsyms.h>
#endif /* CONFIG_KALLSYMS && CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE */

#ifndef HW_WTD_MODE
#define HW_WTD_MODE (0x77665577)
#endif /* HW_WTD_MODE */
#ifndef EMERGENCY_MODE
#define EMERGENCY_MODE (0x77665590)
#endif /* EMERGENCY_MODE */
#ifndef SURFACE_MODE
#define SURFACE_MODE (0x77665594)
#endif /* SURFACE_MODE */

/*
 *
 *
 *
 */
static shrlog_fixed_apps_info *shrlog_info = NULL;
static struct shrlog_ram_fixed_T *shrlog_ram_fixed = NULL;

int shrlog_is_enabled(void)
{
	return shrlog_info != NULL ? 1 : 0;
}

/*
 *
 *
 *
 */
#if defined(CONFIG_SHARP_SHLOG_TASKLIST)
#ifdef CONFIG_ARM
extern void *get_origin_unwind_addr(void);
extern struct unwind_idx __start_unwind_idx[];
extern struct unwind_idx __stop_unwind_idx[];
#endif /* CONFIG_ARM */
#endif /* CONFIG_SHARP_SHLOG_TASKLIST */

/*
 *
 *
 *
 */
#if defined(CONFIG_SHARP_SHLOG_LATESTSTACK)
#endif /* CONFIG_SHARP_SHLOG_LATESTSTACK */

/*
 *
 *
 *
 */
#if defined(CONFIG_SHARP_PANIC_ON_PWRKEY)
struct hrtimer pwrkey_panic_timer;
static int pwrkey_panic_time = 0;
static int pwrkey_panic_time_ms = 0;
unsigned int shrlog_handle_keyevent_keycode = 0;
module_param_named(pwrkey_panic_time,
		   pwrkey_panic_time, int, S_IRUGO | S_IWUSR | S_IWGRP);
module_param_named(pwrkey_panic_time_ms,
		   pwrkey_panic_time_ms, int, S_IRUGO | S_IWUSR | S_IWGRP);
static int enable_pwrkey_panic = 0;
module_param_named(enable_pwrkey_panic,
		   enable_pwrkey_panic, int, S_IRUGO);

static enum hrtimer_restart shrlog_input_panic_timer_expired(struct hrtimer *timer)
{
	if (!shrlog_info)
		return HRTIMER_NORESTART;
	if (enable_pwrkey_panic != 0) {
		panic("POWER KEY %d s pressed\n", pwrkey_panic_time);
	}
	return HRTIMER_NORESTART;
}

static void shrlog_handle_key_input_event(struct input_handle *handle,
					  unsigned int type,
					  unsigned int code, int value)
{
	if (!shrlog_info)
		return;
	if (enable_pwrkey_panic == 0)
		return;
	if (!shrlog_handle_keyevent_keycode)
		return;
	if (type == EV_KEY) {
		if (code == shrlog_handle_keyevent_keycode) {
			if (value) {
				if (pwrkey_panic_time) {
					ktime_t ptime;
					ptime = ktime_set(pwrkey_panic_time, pwrkey_panic_time_ms * (1000*1000));
					hrtimer_start(&pwrkey_panic_timer,
						      ptime, HRTIMER_MODE_REL);
				}
			} else {
				hrtimer_try_to_cancel(&pwrkey_panic_timer);
			}
		}
	}
	return;
}

static int shrlog_handle_input_suspend(struct device *dev)
{
	if (!shrlog_info)
		return 0;
	if (enable_pwrkey_panic == 0)
		return 0;
	hrtimer_try_to_cancel(&pwrkey_panic_timer);
	return 0;
}


static int shrlog_handle_key_input_connect(struct input_handler *handler,
					   struct input_dev *dev,
					   const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;
	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;
	handle->dev = dev;
	handle->handler = handler;
	handle->name = "powerkey";
	error = input_register_handle(handle);
	if (error)
		goto err2;
	error = input_open_device(handle);
	if (error)
		goto err1;
	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void shrlog_handle_key_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id shrlog_handle_key_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_PWR) },
	},
	{ },
};

static struct input_handler shrlog_handle_key_input_handler = {
	.event          = shrlog_handle_key_input_event,
	.connect        = shrlog_handle_key_input_connect,
	.disconnect     = shrlog_handle_key_input_disconnect,
	.name           = "shrlog",
	.id_table       = shrlog_handle_key_ids,
};

static int shrlog_handle_key_init(struct platform_device *pdev)
{
	int ret = 0;
	u32 toval = 0;
#ifdef SVERSION_OF_SOFT
	if (enable_pwrkey_panic == 0) {
		if (strncmp(SVERSION_OF_SOFT, "S", 1) != 0) {
			enable_pwrkey_panic = 1;
		}
	}
#endif
	hrtimer_init(&pwrkey_panic_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pwrkey_panic_timer.function = shrlog_input_panic_timer_expired;
	ret = input_register_handler(&shrlog_handle_key_input_handler);
	if (ret) {
		pr_err("unable to register powerkey(%d)\n", ret);
		return ret;
	}
	pwrkey_panic_time = CONFIG_SHARP_PANIC_ON_PWRKEY_TIMEOUT;
	pwrkey_panic_time_ms = CONFIG_SHARP_PANIC_ON_PWRKEY_TIMEOUT_MS;
	if (pdev != NULL) {
		ret = of_property_read_u32(pdev->dev.of_node,
					   "sharp,pwrkey-panic-time-in-ms",
					   &toval);
		if (!ret) {
			pwrkey_panic_time = toval / 1000;
			pwrkey_panic_time_ms = toval % 1000;
		}
	}
#ifdef SVERSION_OF_SOFT
	if (strncmp(SVERSION_OF_SOFT, "S", 1) == 0) {
		pwrkey_panic_time = CONFIG_SHARP_PANIC_ON_PWRKEY_TIMEOUT_VS;
		pwrkey_panic_time_ms = CONFIG_SHARP_PANIC_ON_PWRKEY_TIMEOUT_MS_VS;
		if (pdev != NULL) {
			ret = of_property_read_u32(pdev->dev.of_node,
						   "sharp,pwrkey-panic-time-vs-in-ms",
						   &toval);
			if (!ret) {
				pwrkey_panic_time = toval / 1000;
				pwrkey_panic_time_ms = toval % 1000;
			}
		}
	}
#endif
	return 0;
}

static int shrlog_handle_key_exit(void)
{
	pwrkey_panic_time = 0;
	mb();
	input_unregister_handler(&shrlog_handle_key_input_handler);
	hrtimer_cancel(&pwrkey_panic_timer);
	return 0;
}

#endif /* CONFIG_SHARP_PANIC_ON_PWRKEY */

/*
 *
 *
 *
 */
static int handle_dload = 0;
static int shrlog_set_handle_dload(void)
{
	if (!shrlog_ram_fixed) return 0;
#ifdef CONFIG_SHARP_HANDLE_DLOAD
	if (handle_dload) {
		shrlog_ram_fixed->handle_dload1 = SHRLOG_FIXED_MAGIC_NUM_A;
		shrlog_ram_fixed->handle_dload2 = SHRLOG_FIXED_MAGIC_NUM_B;
		return 0;
	}
#endif /* CONFIG_SHARP_HANDLE_DLOAD */
	shrlog_ram_fixed->handle_dload1 = 0;
	shrlog_ram_fixed->handle_dload2 = 0;
	return 0;
}
static int shrlog_handle_dload_set(const char *val, const struct kernel_param *kp)
{
	int ret;
	int old_val = handle_dload;
	ret = param_set_int(val, kp);
	if (ret)
		return ret;
	if (handle_dload >> 1) {
		handle_dload = old_val;
		return -EINVAL;
	}
#ifndef CONFIG_SHARP_HANDLE_DLOAD
	handle_dload = 0;
#endif /* !CONFIG_SHARP_HANDLE_DLOAD */
	shrlog_set_handle_dload();
	return 0;
}
/* interface for exporting attributes */

module_param_call(handle_dload, shrlog_handle_dload_set, param_get_int,
			&handle_dload, 0644);


/*
 *
 *
 *
 */
static char sysinfo0[SHRLOG_SZ_OF_INFO];
static char sysinfo1[SHRLOG_SZ_OF_INFO];
static char sysinfo2[SHRLOG_SZ_OF_INFO];
static char sysinfo3[SHRLOG_SZ_OF_INFO];
static char sysinfo4[SHRLOG_SZ_OF_INFO];
static char sysinfo5[SHRLOG_SZ_OF_INFO];
static char sysinfo6[SHRLOG_SZ_OF_INFO];
static char sysinfo7[SHRLOG_SZ_OF_INFO];
static char sysinfo8[SHRLOG_SZ_OF_INFO];
static char sysinfo9[SHRLOG_SZ_OF_INFO];
static char sysinfo10[SHRLOG_SZ_OF_INFO];
static char sysinfo11[SHRLOG_SZ_OF_INFO];
static char sysinfo12[SHRLOG_SZ_OF_INFO];
static char sysinfo13[SHRLOG_SZ_OF_INFO];
static char sysinfo14[SHRLOG_SZ_OF_INFO];
static char sysinfo15[SHRLOG_SZ_OF_INFO];
static struct kparam_string kp_sysinfo0 = {
	.string = sysinfo0,
	.maxlen = sizeof(sysinfo0)
};
static struct kparam_string kp_sysinfo1 = {
	.string = sysinfo1,
	.maxlen = sizeof(sysinfo1)
};
static struct kparam_string kp_sysinfo2 = {
	.string = sysinfo2,
	.maxlen = sizeof(sysinfo2)
};
static struct kparam_string kp_sysinfo3 = {
	.string = sysinfo3,
	.maxlen = sizeof(sysinfo3)
};
static struct kparam_string kp_sysinfo4 = {
	.string = sysinfo4,
	.maxlen = sizeof(sysinfo4)
};
static struct kparam_string kp_sysinfo5 = {
	.string = sysinfo5,
	.maxlen = sizeof(sysinfo5)
};
static struct kparam_string kp_sysinfo6 = {
	.string = sysinfo6,
	.maxlen = sizeof(sysinfo6)
};
static struct kparam_string kp_sysinfo7 = {
	.string = sysinfo7,
	.maxlen = sizeof(sysinfo7)
};
static struct kparam_string kp_sysinfo8 = {
	.string = sysinfo8,
	.maxlen = sizeof(sysinfo8)
};
static struct kparam_string kp_sysinfo9 = {
	.string = sysinfo9,
	.maxlen = sizeof(sysinfo9)
};
static struct kparam_string kp_sysinfo10 = {
	.string = sysinfo10,
	.maxlen = sizeof(sysinfo10)
};
static struct kparam_string kp_sysinfo11 = {
	.string = sysinfo11,
	.maxlen = sizeof(sysinfo11)
};
static struct kparam_string kp_sysinfo12 = {
	.string = sysinfo12,
	.maxlen = sizeof(sysinfo12)
};
static struct kparam_string kp_sysinfo13 = {
	.string = sysinfo13,
	.maxlen = sizeof(sysinfo13)
};
static struct kparam_string kp_sysinfo14 = {
	.string = sysinfo14,
	.maxlen = sizeof(sysinfo14)
};
static struct kparam_string kp_sysinfo15 = {
	.string = sysinfo15,
	.maxlen = sizeof(sysinfo15)
};
static int setup_sysinfo_common(const char *str, const struct kernel_param *kp, char* pbuf1, volatile char* pbuf2)
{
	int i = 0;
	size_t len = strlen(str);
	if (shrlog_info == NULL) {
		printk(KERN_ERR "shrlog: info not initialized\n");
		return -ENOSPC;
	}
	if (len >= SHRLOG_SZ_OF_INFO) {
		printk(KERN_ERR "shrlog: info too long\n");
		return -ENOSPC;
	}
	strcpy(pbuf1, str);
	for (i=0; i<SHRLOG_SZ_OF_INFO; i++) {
		pbuf2[i] = pbuf1[i];
	}
	return 0;
}
static int setup_sysinfo0(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo0, shrlog_info->sysinfo[0].c);
}
static int setup_sysinfo1(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo1, shrlog_info->sysinfo[1].c);
}
static int setup_sysinfo2(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo2, shrlog_info->sysinfo[2].c);
}
static int setup_sysinfo3(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo3, shrlog_info->sysinfo[3].c);
}
static int setup_sysinfo4(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo4, shrlog_info->sysinfo[4].c);
}
static int setup_sysinfo5(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo5, shrlog_info->sysinfo[5].c);
}
static int setup_sysinfo6(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo6, shrlog_info->sysinfo[6].c);
}
static int setup_sysinfo7(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo7, shrlog_info->sysinfo[7].c);
}
static int setup_sysinfo8(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo8, shrlog_info->sysinfo[8].c);
}
static int setup_sysinfo9(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo9, shrlog_info->sysinfo[9].c);
}
static int setup_sysinfo10(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo10, shrlog_info->sysinfo[10].c);
}
static int setup_sysinfo11(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo11, shrlog_info->sysinfo[11].c);
}
static int setup_sysinfo12(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo12, shrlog_info->sysinfo[12].c);
}
static int setup_sysinfo13(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo13, shrlog_info->sysinfo[13].c);
}
static int setup_sysinfo14(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo14, shrlog_info->sysinfo[14].c);
}
static int setup_sysinfo15(const char *str, const struct kernel_param *kp)
{
	return setup_sysinfo_common(str, kp, sysinfo15, shrlog_info->sysinfo[15].c);
}
module_param_call(sysinfo0, setup_sysinfo0, param_get_string,
		  &kp_sysinfo0, 0200);
module_param_call(sysinfo1, setup_sysinfo1, param_get_string,
		  &kp_sysinfo1, 0200);
module_param_call(sysinfo2, setup_sysinfo2, param_get_string,
		  &kp_sysinfo2, 0200);
module_param_call(sysinfo3, setup_sysinfo3, param_get_string,
		  &kp_sysinfo3, 0200);
module_param_call(sysinfo4, setup_sysinfo4, param_get_string,
		  &kp_sysinfo4, 0200);
module_param_call(sysinfo5, setup_sysinfo5, param_get_string,
		  &kp_sysinfo5, 0200);
module_param_call(sysinfo6, setup_sysinfo6, param_get_string,
		  &kp_sysinfo6, 0200);
module_param_call(sysinfo7, setup_sysinfo7, param_get_string,
		  &kp_sysinfo7, 0200);
module_param_call(sysinfo8, setup_sysinfo8, param_get_string,
		  &kp_sysinfo8, 0200);
module_param_call(sysinfo9, setup_sysinfo9, param_get_string,
		  &kp_sysinfo9, 0200);
module_param_call(sysinfo10, setup_sysinfo10, param_get_string,
		  &kp_sysinfo10, 0200);
module_param_call(sysinfo11, setup_sysinfo11, param_get_string,
		  &kp_sysinfo11, 0200);
module_param_call(sysinfo12, setup_sysinfo12, param_get_string,
		  &kp_sysinfo12, 0200);
module_param_call(sysinfo13, setup_sysinfo13, param_get_string,
		  &kp_sysinfo13, 0200);
module_param_call(sysinfo14, setup_sysinfo14, param_get_string,
		  &kp_sysinfo14, 0200);
module_param_call(sysinfo15, setup_sysinfo15, param_get_string,
		  &kp_sysinfo15, 0200);

/*
 *
 *
 *
 */
#if defined(CONFIG_SHARP_HANDLE_PANIC) || defined(CONFIG_SHARP_HANDLE_SURFACE_FLINGER)

extern int sharp_panic_dload_set(void);
static void *qcom_restart_reason = NULL;

#define SCM_WDOG_DEBUG_BOOT_PART	0x9

static void shrlog_set_restart_reason(unsigned int reason_value)
{
	if (!shrlog_info)
		return;
	if (!qcom_restart_reason)
		return;
	__raw_writel(reason_value, qcom_restart_reason);
}

void sharp_panic_init_restart_reason_addr(void *addr)
{
	qcom_restart_reason = addr;
}

void sharp_panic_clear_restart_reason(void)
{
	if (!shrlog_info)
		return;
	qpnp_pon_set_restart_reason(PON_RESTART_REASON_UNKNOWN);
	shrlog_set_restart_reason(0x00000000);
}

bool sharp_panic_needs_warm_reset(bool need_warm_reset,
				  const char *cmd, bool dload_mode,
				  int restart_mode, int in_panic)
{
	sharp_panic_clear_restart_reason();
	if (need_warm_reset != false)
		return need_warm_reset;
	if (!shrlog_info)
		return false;
#ifdef CONFIG_SHARP_HANDLE_PANIC
	if (in_panic) {
		need_warm_reset = true;
	}
#endif /* CONFIG_SHARP_HANDLE_PANIC */
#ifdef CONFIG_SHARP_HANDLE_SURFACE_FLINGER
	if ((cmd != NULL) && !strncmp(cmd, "surfaceflinger", 14)) {
		need_warm_reset = true;
	}
#endif /* CONFIG_SHARP_HANDLE_SURFACE_FLINGER */
	return need_warm_reset;
}

int sharp_panic_preset_restart_reason(int download_mode)
{
	if (!shrlog_info)
		return download_mode;
	download_mode = 0;
	qpnp_pon_set_restart_reason(
		PON_RESTART_REASON_WHILE_LINUX_RUNNING);
	shrlog_set_restart_reason(HW_WTD_MODE);
	return download_mode;
}
void sharp_panic_set_restart_reason(const char *cmd, bool dload_mode,
				    int restart_mode, int in_panic)
{
	if (!shrlog_info)
		return;
#ifdef CONFIG_SHARP_HANDLE_PANIC
	if (in_panic) {
		if (!dload_mode) {
			qpnp_pon_set_restart_reason(
				PON_RESTART_REASON_EMERGENCY_NODLOAD);
		}
		shrlog_set_restart_reason(EMERGENCY_MODE);
	}
#endif /* CONFIG_SHARP_HANDLE_PANIC */
#ifdef CONFIG_SHARP_HANDLE_SURFACE_FLINGER
	if ((cmd != NULL) && !strncmp(cmd, "surfaceflinger", 14)) {
		if (!dload_mode) {
			qpnp_pon_set_restart_reason(
				PON_RESTART_REASON_EMERGENCY_NODLOAD);
		}
		shrlog_set_restart_reason(SURFACE_MODE);
	}
#endif /* CONFIG_SHARP_HANDLE_SURFACE_FLINGER */
}

static struct sharp_msm_restart_callback shrlog_restart_cb = {
	sharp_panic_preset_restart_reason, /* preset_reason */
	sharp_panic_needs_warm_reset, /* warm_reset_ow */
	sharp_panic_set_restart_reason, /* restart_reason_ow */
	shrlog_is_enabled, /* enabled */
	sharp_panic_clear_restart_reason, /* poweroff_ow */
	sharp_panic_init_restart_reason_addr, /* restart_addr_set */
};

#endif /* CONFIG_SHARP_HANDLE_PANIC || CONFIG_SHARP_HANDLE_SURFACE_FLINGER */


/*
 *
 *
 *
 */
#if defined(CONFIG_SHARP_SHLOG_DLOADTYPE_MINI_OVERRIDE)

#define SHRLOG_SCM_DLOAD_OFF      (0x00)
#define SHRLOG_SCM_DLOAD_FULLDUMP (0x10)
#define SHRLOG_SCM_DLOAD_MINIDUMP (0x20)

#define SHRLOG_SCM_DLOAD_CMD      (0x10)

static int shrlog_override_dload_type(unsigned long val)
{
	if (scm_is_call_available(SCM_SVC_BOOT, SHRLOG_SCM_DLOAD_CMD) > 0) {
		struct scm_desc desc = {
			.args[0] = val,
			.args[1] = 0,
			.arginfo = SCM_ARGS(2),
		};
		return scm_call2_atomic(SCM_SIP_FNID(SCM_SVC_BOOT, SHRLOG_SCM_DLOAD_CMD), &desc);
	} else {
		struct device_node *node = of_find_compatible_node(NULL, NULL, "qcom,pshold");
		if (node) {
			struct platform_device *pdev = of_find_device_by_node(node);
			if (pdev) {
				struct resource *mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "tcsr-boot-misc-detect");
				if (mem) {
					return scm_io_write(mem->start, val);
				}
			}
		}
	}
	return -1;
}

static void shrlog_override_dload_init(void)
{
#if defined(CONFIG_KALLSYMS)
	do {
		unsigned long addr = kallsyms_lookup_name("download_mode");
		if (addr != 0) {
			int cur_dload_mode = *((int*)addr);
			if (cur_dload_mode != 0) return;
		}
	} while(0);
#endif /* CONFIG_KALLSYMS */
	if (shrlog_override_dload_type(SHRLOG_SCM_DLOAD_MINIDUMP) != 0) return;
#if defined(CONFIG_KALLSYMS)
	do {
		unsigned long addr = kallsyms_lookup_name("dload_type");
		if (addr != 0) {
			int* dload_type_p = (int*)addr;
			*dload_type_p = SHRLOG_SCM_DLOAD_MINIDUMP;
		}
	} while(0);
#endif /* CONFIG_KALLSYMS */
}
#endif /* CONFIG_SHARP_SHLOG_DLOADTYPE_MINI_OVERRIDE */

/*
 *
 *
 *
 */
#if defined(CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE)

static struct {
	bool* force_warm_reboot_p;
	int* download_mode_p;
	void* qcom_restart_reason_p;
} shrlog_forcewarm_work = { NULL, NULL, NULL };

static int sharp_panic_restart_prepare(struct notifier_block *this,
				       unsigned long event, void *ptr)
{
	if (!shrlog_info) goto notify_done;
	if (shrlog_forcewarm_work.force_warm_reboot_p == NULL) goto notify_done;
	if (shrlog_forcewarm_work.download_mode_p == NULL) goto notify_done;
	if (shrlog_forcewarm_work.qcom_restart_reason_p == NULL) goto notify_done;
	*shrlog_forcewarm_work.force_warm_reboot_p = true;
	if ((*shrlog_forcewarm_work.download_mode_p) == 0) {
		qpnp_pon_set_restart_reason(PON_RESTART_REASON_EMERGENCY_NODLOAD);
	}
	__raw_writel(EMERGENCY_MODE, shrlog_forcewarm_work.qcom_restart_reason_p);
notify_done:
	return NOTIFY_DONE;
}

static struct notifier_block shrlog_panic_blk = {
	.notifier_call	= sharp_panic_restart_prepare,
};


static void sharp_panic_restart_init(void)
{
#if defined(CONFIG_KALLSYMS)
	do {
		unsigned long addr = kallsyms_lookup_name("force_warm_reboot");
		if (addr != 0) {
			shrlog_forcewarm_work.force_warm_reboot_p = (bool*)addr;
		}
	} while(0);
	do {
		unsigned long addr = kallsyms_lookup_name("download_mode");
		if (addr != 0) {
			shrlog_forcewarm_work.download_mode_p = (int*)addr;
		}
	} while(0);
#endif /* CONFIG_KALLSYMS */
	do {
		struct device_node *np;
		np = of_find_compatible_node(NULL, NULL, "qcom,msm-imem-restart_reason");
		if (np) {
			shrlog_forcewarm_work.qcom_restart_reason_p = of_iomap(np, 0);
		}
	} while(0);
	if (shrlog_forcewarm_work.qcom_restart_reason_p != NULL) {
		qpnp_pon_set_restart_reason(PON_RESTART_REASON_WHILE_LINUX_RUNNING);
		__raw_writel(HW_WTD_MODE, shrlog_forcewarm_work.qcom_restart_reason_p);
	}
	atomic_notifier_chain_register(&panic_notifier_list, &shrlog_panic_blk);
}

static void sharp_panic_restart_remove(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list, &shrlog_panic_blk);
}

#endif /* CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE */

/*
 *
 *
 *
 */
#if defined(CONFIG_SHARP_SHLOG_SHOW_ABOOTLOG)

static int shrlog_aboot_logbuf_size = 0;
static char *shrlog_aboot_logbuf = NULL;

static ssize_t shrlog_show_aboot(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	if (!buf)
		return 0;
	if (!shrlog_aboot_logbuf)
		return 0;
	if (!shrlog_aboot_logbuf_size)
		return 0;
	if (shrlog_aboot_logbuf_size > PAGE_SIZE) {
		dev_err(dev, "abootlog: Couldn't write complete log!\n");
		return 0;
	}
	memcpy(buf, shrlog_aboot_logbuf, shrlog_aboot_logbuf_size);
	return shrlog_aboot_logbuf_size;
}
static DEVICE_ATTR(abootlog, S_IRUGO, shrlog_show_aboot, NULL);

int shrlog_prepare_appsboot_info(struct device *dev, void *top_of_logbuf)
{
	shrlog_aboot_log_t *pInfo = (shrlog_aboot_log_t*)top_of_logbuf;
	if (shrlog_aboot_logbuf != NULL)
		return 0;
	if (pInfo == NULL)
		return -ENOMEM;
	if (pInfo->bufsize > RLOG_ABOOT_LOG_BUF_SIZE)
		return -ENOMEM;

	shrlog_aboot_logbuf =
		(char*)kzalloc(RLOG_ABOOT_LOG_BUF_SIZE, GFP_KERNEL);
	if (!shrlog_aboot_logbuf)
		return -ENOMEM;
	if (pInfo->cur_idx > pInfo->bufsize)
		return -ENOMEM;

	if (pInfo->total_size < pInfo->bufsize) {
		shrlog_aboot_logbuf_size = pInfo->total_size;
		memcpy(shrlog_aboot_logbuf,
		       pInfo->data,
		       shrlog_aboot_logbuf_size);
	}else{
		shrlog_uint32 len = pInfo->bufsize - pInfo->cur_idx;
		memcpy(shrlog_aboot_logbuf,
		       &(pInfo->data[pInfo->cur_idx]),
		       len);
		if (pInfo->cur_idx) {
			memcpy(shrlog_aboot_logbuf + len,
			       &(pInfo->data[0]),
			       pInfo->cur_idx);
		}
		shrlog_aboot_logbuf_size = pInfo->bufsize;
	}
	device_create_file(dev, &dev_attr_abootlog);
	return 0;
}

#endif /* CONFIG_SHARP_SHLOG_SHOW_ABOOTLOG */



/*
 *
 *
 *
 */
static shrlog_uint64 shrlog_get_ttbr(void)
{
	shrlog_uint64 ret = 0;
#ifdef CONFIG_ARM
	do {
		u32 ttb;
		asm volatile(
			"mrc p15, 0, %0, c2, c0, 1\n"
			: "=r" (ttb));
		ret = (shrlog_uint64)ttb;
	} while(0);
#endif /* CONFIG_ARM */
#ifdef CONFIG_ARM64
	do {
		u64 ttb;
		asm volatile(
			"mrs %0, ttbr1_el1\n"
			: "=r" (ttb));
		ret = (shrlog_uint64)ttb;
	} while(0);
#endif /* CONFIG_ARM64 */
	return ret;
}

#ifndef MODULE
static int
shrlog_module_load_notify(struct notifier_block *self,
			  unsigned long val, void *data)
{
	struct module *mod = data;
	static int tried = 0;
	if (!shrlog_info)
		return 0;
	if (tried)
		return 0;
	if (val == MODULE_STATE_COMING) {
		if (!shrlog_info->module_addr) {
			struct module *m;
			mutex_lock(&module_mutex);
			list_for_each_entry(m, &(mod->list), list) {
				if (!__module_address((unsigned long)&(m->list))) {
					shrlog_info->module_addr =
						(shrlog_kernel_t)&(m->list);
					break;
				}
			}
			mutex_unlock(&module_mutex);
		}
		tried = 1;
	}
	return 0;
}
static struct notifier_block shrlog_module_load_nb = {
	.notifier_call = shrlog_module_load_notify,
};
#endif /* !MODULE */

/*
 *
 *
 *
 */

static int shrlog_set_apps_info(volatile shrlog_fixed_apps_info *apps_info)
{
#if defined(CONFIG_SHARP_SHLOG_RAMOOPS)
	int ret = 0;
	struct device_node *node = NULL;
	struct device_node *mem_region = NULL;
	struct property *reg_node = NULL;
#endif /* defined(CONFIG_SHARP_SHLOG_RAMOOPS */

	if (!apps_info)
		return -ENOMEM;

	apps_info->adr.st.xtime_sec_addr = (unsigned long)&ktime_get_real_seconds;
	apps_info->adr.st.symbol_put_addr = (unsigned long)(&symbol_put_addr);
	apps_info->adr.st.per_cpu_offset_addr = (unsigned long)(__per_cpu_offset);
	apps_info->size_of_task_struct = sizeof(struct task_struct);
	apps_info->size_of_thread_struct = sizeof(struct thread_struct);
	apps_info->size_of_mm_struct = sizeof(struct mm_struct);
	apps_info->stack_offset = offsetof(struct task_struct, stack);
	apps_info->tasks_offset = offsetof(struct task_struct, tasks);
	apps_info->pid_offset = offsetof(struct task_struct, pid);
	apps_info->thread_group_offset =
		offsetof(struct task_struct, thread_group);
	apps_info->comm_offset = offsetof(struct task_struct, comm);
	apps_info->mm_offset = offsetof(struct task_struct, mm);
	apps_info->pgd_offset = offsetof(struct mm_struct, pgd);
	apps_info->size_of_module = sizeof(struct module);
	apps_info->module_list_offset = offsetof(struct module, list);
	apps_info->module_name_offset = offsetof(struct module, name);
	apps_info->module_name_size = MODULE_NAME_LEN;
	apps_info->module_version_offset = offsetof(struct module, version);
	apps_info->module_startup_offset = offsetof(struct module, init);
	apps_info->module_init_offset = offsetof(struct module, init_layout.base);
	apps_info->module_core_offset = offsetof(struct module, core_layout.base);
	apps_info->module_init_size_offset = offsetof(struct module, init_layout.size);
	apps_info->module_core_size_offset = offsetof(struct module, core_layout.size);

#ifdef CONFIG_ARM64
	apps_info->size_of_cpu_context = sizeof(struct cpu_context);
	apps_info->cpu_context_fp_offset =
		offsetof(struct task_struct, thread.cpu_context.fp);
	apps_info->cpu_context_sp_offset =
		offsetof(struct task_struct, thread.cpu_context.sp);
	apps_info->cpu_context_pc_offset =
		offsetof(struct task_struct, thread.cpu_context.pc);
#endif /* CONFIG_ARM64 */
#ifdef CONFIG_ARM
	apps_info->size_of_thread_info = sizeof(struct thread_info);
	apps_info->cpu_context_in_thread_info =
		offsetof(struct thread_info, cpu_context);
	apps_info->size_of_cpu_context_save =
		sizeof(struct cpu_context_save);
	apps_info->cpu_context_save_fp_offset =
		offsetof(struct cpu_context_save, fp);
	apps_info->cpu_context_save_sp_offset =
		offsetof(struct cpu_context_save, sp);
	apps_info->cpu_context_save_pc_offset =
		offsetof(struct cpu_context_save, pc);
#endif /* CONFIG_ARM */

/*
 *
 */
#if defined(CONFIG_SHARP_SHLOG_RAMOOPS)
	apps_info->oops_addr = 0;
	apps_info->oops_size = 0;
	node = of_find_compatible_node(NULL, NULL, "ramoops");

	if (node)
		mem_region = of_parse_phandle(node, "memory-region", 0);

	if (node && !mem_region)
		reg_node = of_find_property(node, "reg", NULL);

	if (node && (mem_region || reg_node)) {
		struct resource res;

		if (mem_region) {
			ret = of_address_to_resource(mem_region, 0, &res);
			of_node_put(mem_region);
		}
		if (reg_node) {
			ret = of_address_to_resource(node, 0, &res);
		}

		if (ret) {
			apps_info->oops_size = 0;
		} else {
			apps_info->oops_addr = res.start;
			apps_info->oops_size = resource_size(&res);
		}

		if (apps_info->oops_size) {

			u64 val64;

			ret = of_property_read_u64(node,
						   "console-size",
						   &val64);
			if (ret) {
				u32 val32;
				ret = of_property_read_u32(node,
							   "console-size",
							   &val32);
				if (!ret)
					val64 = val32;
			}
			if (ret)
				apps_info->oops_console_size = 0;
			else if (val64 > ULONG_MAX)
				apps_info->oops_console_size = 0;
			else
				apps_info->oops_console_size =
					(shrlog_kernel_t)val64;

			ret = of_property_read_u64(node,
						   "record-size",
						   &val64);
			if (ret) {
				u32 val32;
				ret = of_property_read_u32(node,
							   "record-size",
							   &val32);
				if (!ret)
					val64 = val32;
			}
			if (ret)
				apps_info->oops_record_size = 0;
			else if (val64 > ULONG_MAX)
				apps_info->oops_record_size = 0;
			else
				apps_info->oops_record_size =
					(shrlog_kernel_t)val64;

			ret = of_property_read_u64(node,
						   "ftrace-size",
						   &val64);
			if (ret) {
				u32 val32;
				ret = of_property_read_u32(node,
							   "ftrace-size",
							   &val32);
				if (!ret)
					val64 = val32;
			}
			if (ret)
				apps_info->oops_ftrace_size = 0;
			else if (val64 > ULONG_MAX)
				apps_info->oops_ftrace_size = 0;
			else
				apps_info->oops_ftrace_size =
					(shrlog_kernel_t)val64;

			ret = of_property_read_u64(node,
						   "pmsg-size",
						   &val64);
			if (ret) {
				u32 val32;
				ret = of_property_read_u32(node,
							   "pmsg-size",
							   &val32);
				if (!ret)
					val64 = val32;
			}
			if (ret)
				apps_info->oops_pmsg_size = 0;
			else if (val64 > ULONG_MAX)
				apps_info->oops_pmsg_size = 0;
			else
				apps_info->oops_pmsg_size =
					(shrlog_kernel_t)val64;

		}
	}
	else if (node) {
		u32 oops_addr = 0;
		u32 oops_size = 0;

		ret = of_property_read_u32(node,
					   "android,ramoops-buffer-start",
					   &oops_addr);
		if (ret) {
			oops_size = 0;
		} else {
			ret = of_property_read_u32(node,
						   "android,ramoops-buffer-size",
						   &oops_size);
			if (ret)
				oops_size = 0;
		}
		apps_info->oops_addr = oops_addr;
		apps_info->oops_size = oops_size;

		if (oops_size) {
			ret = of_property_read_u32(node,
						   "android,ramoops-console-size",
						   &oops_size);
			apps_info->oops_console_size = (ret ? 0 : oops_size);
			ret = of_property_read_u32(node,
						   "android,ramoops-record-size",
						   &oops_size);
			apps_info->oops_record_size = (ret ? 0 : oops_size);
			ret = of_property_read_u32(node,
						   "android,ramoops-ftrace-size",
						   &oops_size);
			apps_info->oops_ftrace_size = (ret ? 0 : oops_size);
			ret = of_property_read_u32(node,
						   "android,ramoops-pmsg-size",
						   &oops_size);
			apps_info->oops_pmsg_size = (ret ? 0 : oops_size);
		}
	}
#endif /* CONFIG_SHARP_SHLOG_RAMOOPS */

/*
 *
 */
#if defined(CONFIG_SHARP_SHLOG_TASKLIST)
	apps_info->adr.st.init_task_addr = (unsigned long)(&init_task);
#ifdef CONFIG_ARM
	apps_info->adr.st.__start_unwind_idx_addr =
		(shrlog_kernel_t)__start_unwind_idx;
	apps_info->adr.st.__origin_unwind_idx_addr =
		(shrlog_kernel_t)get_origin_unwind_addr();
	apps_info->adr.st.__stop_unwind_idx_addr =
		(shrlog_kernel_t)__stop_unwind_idx;
#endif /* CONFIG_ARM */
#endif /* CONFIG_SHARP_SHLOG_TASKLIST */

/*
 *
 */
#if defined(CONFIG_SHARP_SHLOG_LATESTSTACK)
#ifdef CONFIG_SCHED_QHMP
	apps_info->adr.st.runqueues_addr =
		(shrlog_kernel_t)(&io_schedule);
#else /* CONFIG_SCHED_QHMP */
	apps_info->adr.st.runqueues_addr =
		(shrlog_kernel_t)(&single_task_running);
#endif /* CONFIG_SCHED_QHMP */
	apps_info->rq_curr_offset = offsetof(struct rq, curr);
	apps_info->rq_idle_offset = offsetof(struct rq, idle);
	apps_info->rq_stop_offset = offsetof(struct rq, stop);
	apps_info->rq_nr_running_offset = offsetof(struct rq, nr_running);
#endif /* CONFIG_SHARP_SHLOG_LATESTSTACK */

/*
 *
 */
#if defined(CONFIG_FUNCTION_TRACER)
	apps_info->adr.st.mcount_addr = (shrlog_kernel_t)(MCOUNT_ADDR);
#else /* !CONFIG_FUNCTION_TRACER */
	apps_info->adr.st.mcount_addr = (shrlog_kernel_t)0;
#endif /* !CONFIG_FUNCTION_TRACER */

/*
 *
 */
	apps_info->module_addr = 0;
/*
 *
 */
	apps_info->xtime_sec_value = 0;
	apps_info->_text_value = 0;
	apps_info->_stext_value = 0;
	apps_info->_etext_value = 0;
/*
 *
 */
	do {
		int i = 0;
		sysinfo0[0] = '\0';
		sysinfo1[0] = '\0';
		sysinfo2[0] = '\0';
		sysinfo3[0] = '\0';
		sysinfo4[0] = '\0';
		sysinfo5[0] = '\0';
		sysinfo6[0] = '\0';
		sysinfo7[0] = '\0';
		for (i=0; i<SHRLOG_NUM_OF_INFO; i++) {
			apps_info->sysinfo[i].c[0] = '\0';
		}
	} while(0);
#ifdef SVERSION_OF_SOFT
	do {
		int i = 0;
		strcpy(sysinfo0, SVERSION_OF_SOFT);
		for (i=0; i<SHRLOG_SZ_OF_INFO; i++) {
			apps_info->sysinfo[0].c[i] = sysinfo0[i];
		}
	} while(0);
#endif
/*
 *
 */
	apps_info->info_size = sizeof(*apps_info);
	apps_info->info_magic = SHRLOG_FIXED_LINUX_MAGIC_NUM;

	return 0;
}

typedef struct shrlog_fixedinfo_appsinfo {
	struct shrlog_ram_fixed_T info;
	shrlog_fixed_apps_info apps_info;
} shrlog_fixedinfo_appsinfo_t;

static int shrlog_runtime_suspend(struct device *dev)
{
#if defined(CONFIG_SHARP_PANIC_ON_PWRKEY)
	return shrlog_handle_input_suspend(dev);
#endif /* CONFIG_SHARP_PANIC_ON_PWRKEY */
	return 0;
}

static int shrlog_probe(struct platform_device *pdev)
{
	int ret = 0;
	uint32_t offset_in_region = 0;
	uint32_t apps_offset_in_region = 0;
	struct device_node *pnode = NULL;
	void *ramfixed_ptr = NULL;
	u64 ramfixed_phys = 0;
	u64 ramfixed_size = 0;
	shrlog_uint64 shrlog_info_phys = 0;
#if defined(CONFIG_SHARP_PANIC_ON_PWRKEY)
	u32 keycode = 0;
#endif /* CONFIG_SHARP_PANIC_ON_PWRKEY */
#if (defined(CONFIG_SHARP_HANDLE_PANIC) || defined(CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE)) && defined(CONFIG_SHARP_HANDLE_DLOAD)
	int s_enabled = 0;
#endif /* (CONFIG_SHARP_HANDLE_PANIC || CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE) && CONFIG_SHARP_HANDLE_DLOAD */


	if (!pdev->dev.of_node) {
		pr_err("unable to find DT imem node\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "sharp,shrlog-offset",
				   &offset_in_region);
	if (ret)
		offset_in_region = 0;
	pnode = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (pnode) {
		u64 mem_size = 0;
		ramfixed_ptr = of_iomap(pnode, 0);
		if (!ramfixed_ptr) {
			of_node_put(pnode);
		} else {
			ramfixed_phys =
				of_translate_address(pnode,
						     of_get_address(pnode, 0,
								    &mem_size, NULL));
			ramfixed_size = mem_size;
		}

	} else {
		pnode = of_parse_phandle(pdev->dev.of_node,
					 "linux,contiguous-region", 0);
		if (pnode) {
			const u32 *addr;
			u64 mem_size;
			addr = of_get_address(pnode, 0, &mem_size, NULL);
			if (IS_ERR_OR_NULL(addr)) {
				ramfixed_ptr = NULL;
			} else {
				u32 mem_addr;
				mem_addr = (u32) of_read_ulong(addr, 2);
				ramfixed_ptr = ioremap(mem_addr, mem_size);
				ramfixed_phys = mem_addr;
				ramfixed_size = mem_size;
			}
			of_node_put(pnode);
		}
	}
	if (ramfixed_ptr)
		shrlog_ram_fixed =
			(struct shrlog_ram_fixed_T*)(((char*)ramfixed_ptr)+offset_in_region);
	else
		pr_debug("region is not reserved for %s\n", pdev->name);

	if (!shrlog_ram_fixed)
		return -ENOMEM;

#if (defined(CONFIG_SHARP_HANDLE_PANIC) || defined(CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE)) && defined(CONFIG_SHARP_HANDLE_DLOAD)
	if ((shrlog_ram_fixed->smagic1 == (shrlog_uint64)SHRLOG_FIXED_MAGIC_NUM_A)
	    && (shrlog_ram_fixed->smagic2 == (shrlog_uint64)SHRLOG_FIXED_MAGIC_NUM_B))
		s_enabled = 1;
	else
		s_enabled = 0;
#endif /* (CONFIG_SHARP_HANDLE_PANIC || CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE) && CONFIG_SHARP_HANDLE_DLOAD */
	shrlog_ram_fixed->smagic1 = 0;
	shrlog_ram_fixed->smagic2 = 0;

	ret = of_property_read_u32(pdev->dev.of_node,
				   "sharp,shrlog-linux-offset",
				   &apps_offset_in_region);
	if (!ret) {
		if (!apps_offset_in_region) {
			if (ramfixed_size >= sizeof(shrlog_fixedinfo_appsinfo_t)) {
				apps_offset_in_region = 
					offsetof(shrlog_fixedinfo_appsinfo_t, apps_info);
			}
		}
		shrlog_info =
			(shrlog_fixed_apps_info*)(((char*)ramfixed_ptr)+apps_offset_in_region);
		shrlog_info_phys = ramfixed_phys + apps_offset_in_region;
	}
	else {
		shrlog_info = kzalloc(sizeof(*shrlog_info), GFP_KERNEL);
		shrlog_info_phys = virt_to_phys(shrlog_info);
	}


	if (!shrlog_info) {
		return -ENOMEM;
	}

#if defined(CONFIG_SHARP_SHLOG_SHOW_ABOOTLOG)
	ret = of_property_read_u32(pdev->dev.of_node,
				   "sharp,shrlog-abootlog-offset",
				   &offset_in_region);
	if (ret)
		offset_in_region = 0;
	pnode = of_parse_phandle(pdev->dev.of_node,
				 "sharp,aboot-log-region", 0);
	if (pnode) {
		void *alogptr = of_iomap(pnode, 0);
		if (!alogptr) {
			of_node_put(pnode);
		} else {
			shrlog_prepare_appsboot_info(
				&pdev->dev,
				((char*)alogptr)+offset_in_region);
		}
	}
#endif /* CONFIG_SHARP_SHLOG_SHOW_ABOOTLOG */

	ret = shrlog_set_apps_info(shrlog_info);
	if (ret)
		return ret;

	shrlog_ram_fixed->shrlog_ram_fixed_addr = 
		(shrlog_uint64)shrlog_info_phys;
	shrlog_ram_fixed->linux_phys_offset = PHYS_OFFSET;
#if defined(CONFIG_ARM64) && defined(KIMAGE_VADDR)
	shrlog_ram_fixed->linux_kimage_voffset = kimage_voffset;
#else /* ! CONFIG_ARM64 && KIMAGE_VADDR */
	shrlog_ram_fixed->linux_kimage_voffset = 0;
#endif /* ! CONFIG_ARM64 && KIMAGE_VADDR */
	shrlog_ram_fixed->linux_kalsr_offset = 0;
#ifdef CONFIG_RANDOMIZE_BASE
	do {
		struct device_node *np;

		np = of_find_compatible_node(NULL, NULL,
					     "qcom,msm-imem-kaslr_offset");
		if (np) {
			static void *kaslr_imem_addr = NULL;
			kaslr_imem_addr = of_iomap(np, 0);
			if (kaslr_imem_addr) {
				uint32_t read_val_l;
				uint32_t read_val_h;
				read_val_l = __raw_readl(kaslr_imem_addr + 4);
				read_val_h = __raw_readl(kaslr_imem_addr + 8);
				iounmap(kaslr_imem_addr);
				shrlog_ram_fixed->linux_kalsr_offset =
					((uint64_t)read_val_h) << 32
					| (uint64_t)read_val_l;
			}
		}
	} while(0);
#endif /* ! CONFIG_RANDOMIZE_BASE */
	shrlog_ram_fixed->linux_ttbr = shrlog_get_ttbr();

#if defined(CONFIG_SHARP_PANIC_ON_PWRKEY)
	shrlog_handle_keyevent_keycode = 0;
#if defined(KEY_POWER)
	shrlog_handle_keyevent_keycode = KEY_POWER;
#endif /* KEY_POWER */
	ret = of_property_read_u32(pdev->dev.of_node,
				   "sharp,panic-keycode",
				   &keycode);
	if (!ret)
		shrlog_handle_keyevent_keycode = keycode;
	if (shrlog_handle_keyevent_keycode) {
		ret = shrlog_handle_key_init(pdev);
		if (ret)
			pr_err("unable to init key handler.\n");
	}
#endif /* CONFIG_SHARP_PANIC_ON_PWRKEY */

	shrlog_ram_fixed->imem_restart_reason = 0;
	shrlog_ram_fixed->magic_num1 = (shrlog_uint64)SHRLOG_FIXED_MAGIC_NUM;
	shrlog_ram_fixed->magic_num2 = (shrlog_uint64)SHRLOG_FIXED_MAGIC_NUM;
#ifdef CONFIG_SHARP_HANDLE_DLOAD
	handle_dload = 1;
#if (defined(CONFIG_SHARP_HANDLE_PANIC) || defined(CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE))
	if (s_enabled)
		handle_dload = 0;
#endif /* CONFIG_SHARP_HANDLE_PANIC || CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE */
#endif /* !CONFIG_SHARP_HANDLE_DLOAD */
	shrlog_set_handle_dload();

#if defined(CONFIG_SHARP_HANDLE_PANIC)
	do {
		if (of_property_read_bool(pdev->dev.of_node,
					  "sharp,disable-sdi-on-boot")) {
			struct scm_desc desc = {
				.args[0] = 1,
				.args[1] = 0,
				.arginfo = SCM_ARGS(2),
			};
			ret = scm_call2_atomic(SCM_SIP_FNID(SCM_SVC_BOOT,
							    SCM_WDOG_DEBUG_BOOT_PART),
					       &desc);
		}
	} while(0);
#ifdef CONFIG_SHARP_HANDLE_DLOAD
	if (s_enabled){
		sharp_msm_set_restart_callback(&shrlog_restart_cb);
	}
#else /* CONFIG_SHARP_HANDLE_DLOAD */
	sharp_msm_set_restart_callback(&shrlog_restart_cb);
#endif /* CONFIG_SHARP_HANDLE_DLOAD */
	if (!qcom_restart_reason) {
		struct device_node *node;
		node = of_find_compatible_node(NULL, NULL,
					       "qcom,msm-imem-restart_reason");
		if (!node) {
			pr_err("unable to find DT imem restart reason node\n");
		} else {
			qcom_restart_reason = of_iomap(node, 0);
			if (!qcom_restart_reason) {
				pr_err("unable to map imem restart reason offset\n");
				return -ENOMEM;
			}
		}
	}
#endif /* CONFIG_SHARP_HANDLE_PANIC */

#if defined(CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE)
#ifdef CONFIG_SHARP_HANDLE_DLOAD
	if (s_enabled){
		sharp_panic_restart_init();
	}
#else /* CONFIG_SHARP_HANDLE_DLOAD */
	sharp_panic_restart_init();
#endif /* CONFIG_SHARP_HANDLE_DLOAD */
#endif /* CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE */
#if defined(CONFIG_SHARP_SHLOG_DLOADTYPE_MINI_OVERRIDE)
	shrlog_override_dload_init();
#endif /* CONFIG_SHARP_SHLOG_DLOADTYPE_MINI_OVERRIDE */

#ifdef MODULE
	do {
		struct module *m;
		mutex_lock(&module_mutex);
		list_for_each_entry(m, &(THIS_MODULE->list), list) {
			if (!__module_address((unsigned long)&(m->list))) {
				shrlog_info->module_addr =
					(shrlog_kernel_t)&(m->list);
				break;
			}
		}
		mutex_unlock(&module_mutex);
	} while(0);
#else /* !MODULE */
	do {
		ret = register_module_notifier(&shrlog_module_load_nb);
		if (ret)
			pr_err("unable to register module notifier.\n");
	} while(0);
#endif /* !MODULE */

	return 0;
}

static int shrlog_remove(struct platform_device *pdev)
{
#ifndef MODULE
	unregister_module_notifier(&shrlog_module_load_nb);
#endif /* !MODULE */
#if defined(CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE)
	sharp_panic_restart_remove();
#endif /* CONFIG_SHARP_SHLOG_FORCEWARM_OVERRIDE */
#if defined(CONFIG_SHARP_SHLOG_SHOW_ABOOTLOG)
	device_remove_file(&(pdev->dev), &dev_attr_abootlog);
	if (shrlog_aboot_logbuf)
		kfree(shrlog_aboot_logbuf);
	shrlog_aboot_logbuf = NULL;
#endif /* CONFIG_SHARP_SHLOG_SHOW_ABOOTLOG */
#ifdef CONFIG_SHARP_PANIC_ON_PWRKEY
	shrlog_handle_key_exit();
#endif /* CONFIG_SHARP_PANIC_ON_PWRKEY */
	if (shrlog_ram_fixed) {
		shrlog_ram_fixed->magic_num1 = 0;
		shrlog_ram_fixed->magic_num2 = 0;
		shrlog_ram_fixed->handle_dload1 = 0;
		shrlog_ram_fixed->handle_dload2 = 0;
	}
	return 0;
}

/*
 *
 *
 *
 */
#define SHRLOG_COMPAT_STR "sharp,shrlog"

static struct of_device_id shrlog_match_table[] = {
	{
		.compatible = SHRLOG_COMPAT_STR
	},
	{},
};

static const struct dev_pm_ops shrlog_dev_pm_ops = {
	.suspend = shrlog_runtime_suspend,
};

static struct platform_driver shrlog_driver = {
	.probe = shrlog_probe,
	.remove = shrlog_remove,
	.driver = {
		.name = "shrlog",
		.of_match_table = shrlog_match_table,
		.pm = &shrlog_dev_pm_ops,
	},
};

static int __init shrlog_init(void)
{
	return platform_driver_register(&shrlog_driver);
}
static void __exit shrlog_exit(void)
{
	platform_driver_unregister(&shrlog_driver);
}

subsys_initcall(shrlog_init);
module_exit(shrlog_exit);

MODULE_AUTHOR("SHARP");
MODULE_DESCRIPTION("sharp rlog module");
MODULE_LICENSE("GPL");
