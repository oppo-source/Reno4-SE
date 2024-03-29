//SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include "../../drivers/misc/mediatek/base/power/include/mtk_upower.h"

#if defined(VENDOR_EDIT) && defined(CONFIG_SCHEDUTIL_USE_TL)
//cuixiaogang@Power. seperate capacity_margin.2020.05.29
extern unsigned int capacity_margin_dvfs;
bool capacity_margin_dvfs_changed = false;
void set_capacity_margin_dvfs_changed(bool changed)
{
	capacity_margin_dvfs_changed = changed;
}
#endif

static unsigned int get_next_freq(struct sugov_policy *sg_policy,
				  unsigned long util, unsigned long max)
{
	struct cpufreq_policy *policy = sg_policy->policy;
	int idx, target_idx = 0;
	int cap;
	int cpu = policy->cpu;
	struct upower_tbl *tbl;
	unsigned int freq = arch_scale_freq_invariant() ?
				policy->cpuinfo.max_freq : policy->cur;
#if defined(VENDOR_EDIT) && defined(CONFIG_SCHEDUTIL_USE_TL)
//cuixiaogang@Power. add target load. 2020.05.12
	unsigned long target_util;

	if (!capacity_margin_dvfs_changed) {
		target_util = choose_util(sg_policy, util);
		if (target_util < 0)
			return freq;
		trace_sugov_next_util_tl(cpu, util, max, target_util);
		util = target_util;
	} else {
#endif
	#ifdef VENDOR_EDIT
	//cuixiaogang@Power. seperate capacity_margin.2020.05.29
		util = util * capacity_margin_dvfs / SCHED_CAPACITY_SCALE;
	#else
		util = util * capacity_margin / SCHED_CAPACITY_SCALE;
	#endif
#if defined(VENDOR_EDIT) && defined(CONFIG_SCHEDUTIL_USE_TL)
//cuixiaogang@Power. add target load. 2020.05.12
	}
#endif
	tbl = upower_get_core_tbl(cpu);
	for (idx = 0; idx < tbl->row_num ; idx++) {
		cap = tbl->row[idx].cap;
		if (!cap)
			break;

		target_idx = idx;

		if (cap > util)
			break;
	}

	freq = mt_cpufreq_get_cpu_freq(cpu, target_idx);

	sg_policy->cached_raw_freq = freq;
	return cpufreq_driver_resolve_freq(policy, freq);
}
