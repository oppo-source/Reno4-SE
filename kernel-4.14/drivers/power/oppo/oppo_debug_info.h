#ifndef __OPPO_DEBUG_INFO__H
#define __OPPO_DEBUG_INFO__H

#include "oppo_charger.h"

enum GAUGE_SEAL_UNSEAL_ERROR{
	OPPO_GAUGE_SEAL_FAIL,
	OPPO_GAUGE_UNSEAL_FIAL,
};

extern int oppo_chg_debug_info_init(void);
extern int oppo_chg_debug_chg_monitor(struct oppo_chg_chip *chip);
extern int oppo_chg_debug_set_cool_down_by_user(int is_cool_down);
extern int oppo_chg_debug_get_cooldown_current(int chg_current_by_tbatt, int chg_current_by_cooldown);
extern int oppo_chg_debug_set_soc_info(struct oppo_chg_chip *chip);
extern void oppo_chg_gauge_seal_unseal_fail(int type);

#endif
