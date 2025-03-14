/*
 * Copyright (C) 2010-2013 Felix Fietkau <nbd@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifdef AUT_RATE_SIM
#include "type.h"
#include "opt_sim2.h"
#include "mac80211_temp.h"
#include "cfg80211.h"
extern unsigned long jiffies;
extern unsigned int HZ;
#else
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/debugfs.h>
#include <linux/random.h>
#include <linux/moduleparam.h>
#include <linux/ieee80211.h>
#include <net/mac80211.h>

#endif
#include "osdep.h"
#include "rc80211_minstrel.h"
#include "rc80211_minstrel_ht.h"
#include "wifi_rate_ctrl.h"
#include "wifi_debug.h"
#include "wifi_mac_com.h"
#ifdef CONFIG_MAC80211_RC_MINSTREL_VHT
static bool minstrel_vht_only = true;
#endif

/*
 * To enable sufficiently targeted rate sampling, MCS rates are divided into
 * groups, based on the number of streams and flags (HT40, SGI) that they
 * use.
 *
 * Sortorder has to be fixed for GROUP_IDX macro to be applicable:
 * BW -> SGI -> #streams
 */
const struct mcs_group minstrel_mcs_groups[] = {
	MCS_GROUP(1, 0, BW_20),
	MCS_GROUP(1, 1, BW_20),
	MCS_GROUP(1, 0, BW_40),
	MCS_GROUP(1, 1, BW_40),

	CCK_GROUP,

#ifdef CONFIG_MAC80211_RC_MINSTREL_VHT
	VHT_GROUP(1, 0, BW_20),
	VHT_GROUP(1, 1, BW_20),

	VHT_GROUP(1, 0, BW_40),
	VHT_GROUP(1, 1, BW_40),

	VHT_GROUP(1, 0, BW_80),
	VHT_GROUP(1, 1, BW_80),
#endif
};

static u8 sample_table[SAMPLE_COLUMNS][MCS_GROUP_RATES];

static void
minstrel_ht_update_rates(struct minstrel_priv *mp, struct minstrel_ht_sta *mi);

/*
 * Some VHT MCSes are invalid (when Ndbps / Nes is not an integer)
 * e.g for MCS9@20MHzx1Nss: Ndbps=8x52*(5/6) Nes=1
 *
 * Returns the valid mcs map for struct minstrel_mcs_group_data.supported
 */
static u16
minstrel_get_valid_vht_rates(int bw, int nss, /*__le16*/unsigned short mcs_map)
{
	u16 mask = 0;

	if (bw == BW_20) {
		if (nss != 3 && nss != 6)
			mask = BIT(9);
	} else if (bw == BW_80) {
		if (nss == 3 || nss == 7)
			mask = BIT(6);
		else if (nss == 6)
			mask = BIT(9);
	} else {
		ASSERT(bw == BW_40);
	}

	switch ((/*le16_to_cpu*/(mcs_map) >> (2 * (nss - 1))) & 3) {
	case IEEE80211_VHT_MCS_SUPPORT_0_7:
		mask |= 0x300;
		break;
	case IEEE80211_VHT_MCS_SUPPORT_0_8:
		mask |= 0x200;
		break;
	case IEEE80211_VHT_MCS_SUPPORT_0_9:
		break;
	default:
		mask = 0x3ff;
	}

	return 0x3ff & ~mask;
}

/*
 * Look up an MCS group index based on mac80211 rate information
 */
static int
minstrel_ht_get_group_idx(struct ieee80211_tx_rate *rate)
{
	return GROUP_IDX((rate->idx / 8) + 1,
			 !!(rate->flags & IEEE80211_TX_RC_SHORT_GI),
			 !!(rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH));
}

static int
minstrel_vht_get_group_idx(struct ieee80211_tx_rate *rate)
{
	return VHT_GROUP_IDX(ieee80211_rate_get_vht_nss(rate),
			     !!(rate->flags & IEEE80211_TX_RC_SHORT_GI),
			     !!(rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH) +
			     2*!!(rate->flags & IEEE80211_TX_RC_80_MHZ_WIDTH));
}

static struct minstrel_rate_stats *
minstrel_ht_get_stats(struct minstrel_priv *mp, struct minstrel_ht_sta *mi,
		      struct ieee80211_tx_rate *rate)
{
    int group, idx;

    if (rate->flags & IEEE80211_TX_RC_MCS) {
        group = minstrel_ht_get_group_idx(rate);
        idx = rate->idx % 8;

    } else if (rate->flags & IEEE80211_TX_RC_VHT_MCS) {
        group = minstrel_vht_get_group_idx(rate);
        idx = ieee80211_rate_get_vht_mcs(rate);

    } else {
        group = MINSTREL_CCK_GROUP;

        for (idx = 0; idx < ARRAY_SIZE(mp->cck_rates); idx++)
            if (rate->idx == mp->cck_rates[idx])
                break;

        /* short preamble */
        if (!(mi->supported[group] & BIT(idx)))
            idx += 4;
    }
    
    //group = 13;//vht20
    //group = 19;//vht40
    //group = 25;//vht80
    //group = 0;//ht20
    //group = 6; //ht40
    // AML_PRINT(AML_LOG_ID_RATE, AML_LOG_LEVEL_DEBUG, "idx=%d, count=%d,group=%d, idx=%d\n",
    //                 rate->idx,rate->count,group, idx);
    return &mi->groups[group].rates[idx];
}

static inline struct minstrel_rate_stats *
minstrel_get_ratestats(struct minstrel_ht_sta *mi, int index)
{
	return &mi->groups[index / MCS_GROUP_RATES].rates[index % MCS_GROUP_RATES];
}

/*
 * Return current throughput based on the average A-MPDU length, taking into
 * account the expected number of retransmissions and their expected length
 */
int
minstrel_ht_get_tp_avg(struct minstrel_ht_sta *mi, int group, int rate,
		       int prob_ewma)
{
    unsigned int nsecs = 0;

    /* do not account throughput if success prob is below 10% */
    if (prob_ewma < MINSTREL_FRAC(10, 100))
        return 0;

    if (group != MINSTREL_CCK_GROUP)
        nsecs = 1000 * mi->overhead / MINSTREL_TRUNC(mi->avg_ampdu_len);

    nsecs += minstrel_mcs_groups[group].duration[rate];

    /*
    * For the throughput calculation, limit the probability value to 90% to
    * account for collision related packet error rate fluctuation
    * (prob is scaled - see MINSTREL_FRAC above)
    */
    if (prob_ewma > MINSTREL_FRAC(90, 100)) {
        return MINSTREL_TRUNC(100 * ((unsigned int)(MINSTREL_FRAC(90, 100) * (unsigned int)1000000) / nsecs));

    } else {
        //return MINSTREL_TRUNC(100000 * ((prob_ewma * 1000) / nsecs));
        return MINSTREL_TRUNC(100 * ((unsigned int)(prob_ewma * 1000000) / nsecs));
    }
}

/*
 * Find & sort topmost throughput rates
 *
 * If multiple rates provide equal throughput the sorting is based on their
 * current success probability. Higher success probability is preferred among
 * MCS groups, CCK rates do not provide aggregation and are therefore at last.
 */
static void
minstrel_ht_sort_best_tp_rates(struct minstrel_ht_sta *mi, u16 index,
			       u16 *tp_list)
{
	int cur_group, cur_idx, cur_tp_avg, cur_prob;
	int tmp_group, tmp_idx, tmp_tp_avg, tmp_prob;
	int j = MAX_THR_RATES;

	cur_group = index / MCS_GROUP_RATES;
	cur_idx = index  % MCS_GROUP_RATES;
	cur_prob = mi->groups[cur_group].rates[cur_idx].prob_ewma;
	cur_tp_avg = minstrel_ht_get_tp_avg(mi, cur_group, cur_idx, cur_prob);
	/*for debug */
	mi->groups[cur_group].rates[cur_idx].tp_avg = cur_tp_avg;

	do {
		tmp_group = tp_list[j - 1] / MCS_GROUP_RATES;
		tmp_idx = tp_list[j - 1] % MCS_GROUP_RATES;
		tmp_prob = mi->groups[tmp_group].rates[tmp_idx].prob_ewma;
		tmp_tp_avg = minstrel_ht_get_tp_avg(mi, tmp_group, tmp_idx, tmp_prob);

		if ((cur_tp_avg < tmp_tp_avg) || (cur_tp_avg == tmp_tp_avg && cur_prob <= tmp_prob))
			break;
		j--;
	} while (j > 0);

	if (j < MAX_THR_RATES - 1) {
		memmove(&tp_list[j + 1], &tp_list[j], (sizeof(*tp_list) * (MAX_THR_RATES - (j + 1))));
	}
	if (j < MAX_THR_RATES)
		tp_list[j] = index;
}

/*
 * Find and set the topmost probability rate per sta and per group
 */
static void
minstrel_ht_set_best_prob_rate(struct minstrel_ht_sta *mi, u16 index)
{
	struct minstrel_mcs_group_data *mg;
	struct minstrel_rate_stats *mrs;
	int tmp_group, tmp_idx, tmp_tp_avg, tmp_prob;
	int max_tp_group, cur_tp_avg, cur_group, cur_idx;
	int max_gpr_group, max_gpr_idx;
	int max_gpr_tp_avg, max_gpr_prob;

	cur_group = index / MCS_GROUP_RATES;
	cur_idx = index % MCS_GROUP_RATES;
	mg = &mi->groups[index / MCS_GROUP_RATES];
	mrs = &mg->rates[index % MCS_GROUP_RATES];

	tmp_group = mi->max_prob_rate / MCS_GROUP_RATES;
	tmp_idx = mi->max_prob_rate % MCS_GROUP_RATES;
	tmp_prob = mi->groups[tmp_group].rates[tmp_idx].prob_ewma;
	tmp_tp_avg = minstrel_ht_get_tp_avg(mi, tmp_group, tmp_idx, tmp_prob);

	/* if max_tp_rate[0] is from MCS_GROUP max_prob_rate get selected from
	 * MCS_GROUP as well as CCK_GROUP rates do not allow aggregation */
	max_tp_group = mi->max_tp_rate[0] / MCS_GROUP_RATES;
	if ((index / MCS_GROUP_RATES == MINSTREL_CCK_GROUP) &&
	    (max_tp_group != MINSTREL_CCK_GROUP))
		return;

	max_gpr_group = mg->max_group_prob_rate / MCS_GROUP_RATES;
	max_gpr_idx = mg->max_group_prob_rate % MCS_GROUP_RATES;
	max_gpr_prob = mi->groups[max_gpr_group].rates[max_gpr_idx].prob_ewma;

	if (mrs->prob_ewma > MINSTREL_FRAC(75, 100)) {
		cur_tp_avg = minstrel_ht_get_tp_avg(mi, cur_group, cur_idx, mrs->prob_ewma);
		if (cur_tp_avg > tmp_tp_avg)
			mi->max_prob_rate = index;

		max_gpr_tp_avg = minstrel_ht_get_tp_avg(mi, max_gpr_group, max_gpr_idx, max_gpr_prob);
		if (cur_tp_avg > max_gpr_tp_avg)
			mg->max_group_prob_rate = index;
	} else {
		if (mrs->prob_ewma > tmp_prob)
			mi->max_prob_rate = index;
		if (mrs->prob_ewma > max_gpr_prob)
			mg->max_group_prob_rate = index;
	}
}


/*
 * Assign new rate set per sta and use CCK rates only if the fastest
 * rate (max_tp_rate[0]) is from CCK group. This prohibits such sorted
 * rate sets where MCS and CCK rates are mixed, because CCK rates can
 * not use aggregation.
 */
static void
minstrel_ht_assign_best_tp_rates(struct minstrel_ht_sta *mi,
				 u16 tmp_mcs_tp_rate[MAX_THR_RATES],
				 u16 tmp_cck_tp_rate[MAX_THR_RATES])
{
    unsigned int tmp_group, tmp_idx, tmp_cck_tp, tmp_mcs_tp, tmp_prob;
    int i;

    tmp_group = tmp_cck_tp_rate[0] / MCS_GROUP_RATES;
    tmp_idx = tmp_cck_tp_rate[0] % MCS_GROUP_RATES;
    tmp_prob = mi->groups[tmp_group].rates[tmp_idx].prob_ewma;
    tmp_cck_tp = minstrel_ht_get_tp_avg(mi, tmp_group, tmp_idx, tmp_prob);

    tmp_group = tmp_mcs_tp_rate[0] / MCS_GROUP_RATES;
    tmp_idx = tmp_mcs_tp_rate[0] % MCS_GROUP_RATES;
    tmp_prob = mi->groups[tmp_group].rates[tmp_idx].prob_ewma;
    tmp_mcs_tp = minstrel_ht_get_tp_avg(mi, tmp_group, tmp_idx, tmp_prob);

    if (tmp_cck_tp > tmp_mcs_tp) {
        for(i = 0; i < MAX_THR_RATES; i++) {
            minstrel_ht_sort_best_tp_rates(mi, tmp_cck_tp_rate[i], tmp_mcs_tp_rate);
        }
    }
}

static void minstrel_ht_modify_tp_rates(struct minstrel_ht_sta *mi, u16 *tp_list, u16 *max_prob_rate)
{
    int cur_group, cur_idx, cur_tp_avg, cur_prob;
    int final_idx, tmp_idx, tmp_tp_avg, tmp_prob;
    int tp0_group = tp_list[0] / MCS_GROUP_RATES;
    int tp0_idx = tp_list[0] % MCS_GROUP_RATES;
    int rate_idx = tp0_idx;

    while (mi->groups[tp0_group].rates[rate_idx].prob_ewma < MINSTREL_FRAC(50, 100)) {
        if (!rate_idx)
            break;
         rate_idx--;
    }

    tp_list[0] = tp0_group * MCS_GROUP_RATES + rate_idx;
    tp0_idx = rate_idx;

    if ((*max_prob_rate % MCS_GROUP_RATES) >= tp0_idx) {
        cur_group = *max_prob_rate / MCS_GROUP_RATES;
        cur_idx = tp0_idx ? tp0_idx -1 : tp0_idx;
        cur_prob = mi->groups[cur_group].rates[cur_idx].prob_ewma;
        cur_tp_avg = minstrel_ht_get_tp_avg(mi, cur_group, cur_idx, cur_prob);
        final_idx = cur_idx;

        for (tmp_idx = cur_idx - 1; tmp_idx >= 0; tmp_idx--) {
            tmp_prob = mi->groups[cur_group].rates[tmp_idx].prob_ewma;
            tmp_tp_avg = minstrel_ht_get_tp_avg(mi, cur_group, tmp_idx, tmp_prob);

            if ((cur_tp_avg < tmp_tp_avg) || (cur_tp_avg == tmp_tp_avg && cur_prob <= tmp_prob)) {
                cur_tp_avg = tmp_tp_avg;
                cur_prob = tmp_prob;
                final_idx = tmp_idx;
            }
        }
        *max_prob_rate = cur_group * MCS_GROUP_RATES + final_idx;
    }
}
/*
 * Try to increase robustness of max_prob rate by decrease number of
 * streams if possible.
 */
static inline void
minstrel_ht_prob_rate_reduce_streams(struct minstrel_ht_sta *mi)
{
    struct minstrel_mcs_group_data *mg;
    int tmp_max_streams, group, tmp_idx, tmp_prob;
    int tmp_tp = 0;

    tmp_max_streams = minstrel_mcs_groups[mi->max_tp_rate[0] / MCS_GROUP_RATES].streams;
    for (group = 0; group < ARRAY_SIZE(minstrel_mcs_groups); group++) {
        mg = &mi->groups[group];
        if (!mi->supported[group] || group == MINSTREL_CCK_GROUP)
            continue;

        tmp_idx = mg->max_group_prob_rate % MCS_GROUP_RATES;
        tmp_prob = mi->groups[group].rates[tmp_idx].prob_ewma;

        if (tmp_tp < minstrel_ht_get_tp_avg(mi, group, tmp_idx, tmp_prob) &&
            (minstrel_mcs_groups[group].streams < tmp_max_streams)) {
            mi->max_prob_rate = mg->max_group_prob_rate;
            tmp_tp = minstrel_ht_get_tp_avg(mi, group, tmp_idx, tmp_prob);
        }
    }
}

void minstrel_clear_unfitable_rate_stats(struct minstrel_ht_sta *mi, unsigned char rate_index)
{
    struct minstrel_mcs_group_data *mg;
    struct minstrel_rate_stats *mrs;
    int group, i;

    for (group = 0; group < ARRAY_SIZE(minstrel_mcs_groups); group++) {
        mg = &mi->groups[group];
        if (!mi->supported[group] || ((rate_index + 2) > MCS_GROUP_RATES))
            continue;

        for (i = rate_index + 1; i < MCS_GROUP_RATES; i++) {
            if (!(mi->supported[group] & BIT(i)))
                continue;

            mrs = &mg->rates[i];
            if (mrs->succ_hist != 0) {
                AML_PRINT_LOG_INFO("clear rate:%d, input:%d\n", i, rate_index);
                mrs->attempts = 1;
                mrs->last_attempts = 0;
                mrs->att_hist = 0;

                mrs->success = 0;
                mrs->last_success = 0;
                mrs->succ_hist = 0;
                mrs->tp_avg = 0;
            }
        }
    }
}

void minstrel_clear_unfitable_prob_ewma(struct minstrel_ht_sta *mi)
{
    unsigned int i = 0, bw = 0, group = 0;
    struct minstrel_mcs_group_data *mg;
    struct minstrel_rate_stats *mrs;

    for (group = 0; group < ARRAY_SIZE(minstrel_mcs_groups); group++) {
        if (!mi->supported[group])
            continue;

        bw = minstrel_mcs_groups[group].flags & IEEE80211_TX_RC_80_MHZ_WIDTH ? BW_80
            : minstrel_mcs_groups[group].flags & IEEE80211_TX_RC_40_MHZ_WIDTH ? BW_40 : BW_20;

        if (!(mi->sample_clear_bw & BIT(bw)))
            continue;

        mg = &mi->groups[group];
        for (i = 0; i < MCS_GROUP_RATES; i++) {
            if (!(mi->supported[group] & BIT(i))) {
                continue;
            }
            mrs = &mg->rates[i];
            mrs->prob_ewma = 0;
        }
    }
}

/*
 * Update rate statistics and select new primary rates
 *
 * Rules for rate selection:
 *  - max_prob_rate must use only one stream, as a tradeoff between delivery
 *    probability and throughput during strong fluctuations
 *  - as long as the max prob rate has a probability of more than 75%, pick
 *    higher throughput rates, even if the probability is a bit lower
 */
static void
minstrel_ht_update_stats(struct minstrel_priv *mp, struct minstrel_ht_sta *mi)
{
    struct minstrel_mcs_group_data *mg;
    struct minstrel_rate_stats *mrs;
    int group, i, j, cur_prob;
    u16 tmp_mcs_tp_rate[MAX_THR_RATES], tmp_group_tp_rate[MAX_THR_RATES];
    u16 tmp_cck_tp_rate[MAX_THR_RATES], index;

    if (mi->ampdu_packets > 0) {
        mi->avg_ampdu_len = minstrel_ewma(mi->avg_ampdu_len,
            MINSTREL_FRAC(mi->ampdu_len, mi->ampdu_packets), EWMA_LEVEL);
        mi->ampdu_len = 0;
        mi->ampdu_packets = 0;
    }

    mi->sample_slow = 0;
    mi->sample_count = 0;

    for (j = 0; j < MAX_THR_RATES; j++) {
        tmp_mcs_tp_rate[j] = 0;
        tmp_cck_tp_rate[j] = 0;
        tmp_group_tp_rate[j] = 0;
    }

    /* Find best rate sets within all MCS groups*/
    for (group = 0; group < ARRAY_SIZE(minstrel_mcs_groups); group++) {
        mg = &mi->groups[group];
        if (!mi->supported[group])
            continue;

        mi->sample_count++;

        for (i = 0; i < MCS_GROUP_RATES; i++) {
            if (!(mi->supported[group] & BIT(i)))
                continue;

            index = MCS_GROUP_RATES * group + i;

            mrs = &mg->rates[i];
            mrs->retry_updated = false;
            minstrel_calc_rate_stats(mrs);
            cur_prob = mrs->prob_ewma;

            //if (minstrel_ht_get_tp_avg(mi, group, i, cur_prob) == 0)
            //	continue;

            /* Find max throughput rate set */
            if (group != MINSTREL_CCK_GROUP) {
                minstrel_ht_sort_best_tp_rates(mi, index, tmp_mcs_tp_rate);

            } else if (group == MINSTREL_CCK_GROUP) {
                minstrel_ht_sort_best_tp_rates(mi, index, tmp_cck_tp_rate);
            }

            /* Find max throughput rate set within a group */
            minstrel_ht_sort_best_tp_rates(mi, index, tmp_group_tp_rate);

            /* Find max probability rate per group and global */
            minstrel_ht_set_best_prob_rate(mi, index);

            if ((mrs->att_hist != 0) && (mrs->succ_hist != 0)) {
                if ((mi->sample_init_flag && (index == mi->sample_init_idx) && mrs->prob_ewma && (mrs->prob_ewma < MINSTREL_FRAC(10, 10)))
                    || (mrs->prob_ewma && (mrs->prob_ewma < MINSTREL_FRAC(10, 100)))) {
                    mi->sample_all_bw = 1;
                    mi->sample_clear_flag = 1;
                    mi->sample_clear_bw |= minstrel_mcs_groups[group].flags & IEEE80211_TX_RC_80_MHZ_WIDTH ? (1 << BW_80)
                        : minstrel_mcs_groups[group].flags & IEEE80211_TX_RC_40_MHZ_WIDTH ? (1 << BW_40) : (1 << BW_20);
                }

                AML_PRINT(AML_LOH_ID_RATE_CTR,AML_LOG_LEVEL_DEBUG, "group=%d,rate idx =%d, success=%4d,attempts=%4d, succ_hist=%6d, att_hist=%7d,%3d%%, prob_ewma=%4d,tp_avg=%3d,duration=%d\n",
                     group, i, mrs->last_success, mrs->last_attempts, mrs->succ_hist,mrs->att_hist,mrs->succ_hist*100/mrs->att_hist, mrs->prob_ewma, mrs->tp_avg, minstrel_mcs_groups[group].duration[i]);
            }
        }

        memcpy(mg->max_group_tp_rate, tmp_group_tp_rate, sizeof(mg->max_group_tp_rate));
    }

    /* Assign new rate set per sta */
    minstrel_ht_assign_best_tp_rates(mi, tmp_mcs_tp_rate, tmp_cck_tp_rate);
    memcpy(mi->max_tp_rate, tmp_mcs_tp_rate, sizeof(mi->max_tp_rate));
    AML_PRINT(AML_LOH_ID_RATE_CTR,AML_LOG_LEVEL_DEBUG, " best_tp:%d, max_tp_rate:%d:%d:%d:%d\n", mi->max_prob_rate,
        mi->max_tp_rate[0], mi->max_tp_rate[1], mi->max_tp_rate[2], mi->max_tp_rate[3]);
    /* Try to increase robustness of max_prob_rate*/
    minstrel_ht_prob_rate_reduce_streams(mi);

    /* try to sample all available rates during each interval */
    mi->sample_count *= 10;

#ifdef CONFIG_MAC80211_DEBUGFS
    /* use fixed index if set */
    if (mp->fixed_rate_idx != -1) {
        for (i = 0; i < 4; i++)
            mi->max_tp_rate[i] = mp->fixed_rate_idx;
        mi->max_prob_rate = mp->fixed_rate_idx;
    }
#endif

    /* Reset update timer */
    mi->last_stats_update = jiffies;
}

void minstrel_init_start_stats(void *priv, void *priv_sta, unsigned char max_rate, unsigned char bw)
{
    struct minstrel_priv *mp = priv;
    struct minstrel_mcs_group_data *mg;
    struct minstrel_ht_sta_priv *msp = priv_sta;
    struct minstrel_ht_sta *mi = &msp->ht;
    struct minstrel_rate_stats *mrs;
    int group;
    unsigned char group_bw = 0;

    mi->sample_init_flag = 1;
    mi->sample_force_counts = 3;
    mi->need_clear_rate_index = 0;
    for (group = 0; group < ARRAY_SIZE(minstrel_mcs_groups); group++) {
        group_bw = minstrel_mcs_groups[group].flags & IEEE80211_TX_RC_80_MHZ_WIDTH ? BW_80
            : minstrel_mcs_groups[group].flags & IEEE80211_TX_RC_40_MHZ_WIDTH ? BW_40 : BW_20;

        mg = &mi->groups[group];
        if (!mi->supported[group] || (group_bw != bw))
            continue;

        if (!(mi->supported[group] & BIT(max_rate)))
            continue;

        AML_PRINT_LOG_INFO("rate_index:%d, bw:%d\n", max_rate, bw);
        mrs = &mg->rates[max_rate];
        mrs->attempts = 2;
        mrs->att_hist = 0;
        mrs->success = 2;
        mrs->succ_hist = 0;

        mrs = &mg->rates[2];
        mrs->attempts = 1;
        mrs->att_hist = 0;
        mrs->success = 1;
        mrs->succ_hist = 0;

        mrs = &mg->rates[0];
        mrs->attempts = 1;
        mrs->att_hist = 0;
        mrs->success = 1;
        mrs->succ_hist = 0;

        mi->sample_init_idx = group * MCS_GROUP_RATES + 0;
    }

    minstrel_ht_update_stats(mp, mi);
}


static bool
minstrel_ht_txstat_valid(struct minstrel_priv *mp, struct ieee80211_tx_rate *rate)
{
    if (rate->idx < 0)
        return false;

    if (!rate->count)
        return false;

    if (rate->flags & IEEE80211_TX_RC_MCS ||
        rate->flags & IEEE80211_TX_RC_VHT_MCS)
        return true;

    return rate->idx == mp->cck_rates[0] ||
        rate->idx == mp->cck_rates[1] ||
        rate->idx == mp->cck_rates[2] ||
        rate->idx == mp->cck_rates[3];
}

static void
minstrel_set_next_sample_idx(struct minstrel_ht_sta *mi)
{
	struct minstrel_mcs_group_data *mg;

	for (;;) {
		mi->sample_group++;
		mi->sample_group %= ARRAY_SIZE(minstrel_mcs_groups);
		mg = &mi->groups[mi->sample_group];

		if (!mi->supported[mi->sample_group])
			continue;

		if (++mg->index >= MCS_GROUP_RATES) {
			mg->index = 0;
			if (++mg->column >= ARRAY_SIZE(sample_table))
				mg->column = 0;
		}
		break;
	}
}

static void
minstrel_downgrade_rate(struct minstrel_ht_sta *mi, u16 *idx, bool primary)
{
    int group, orig_group;

    orig_group = group = *idx / MCS_GROUP_RATES;
    while (group > 0) {
        group--;

        if (!mi->supported[group])
            continue;

        if (minstrel_mcs_groups[group].streams >
            minstrel_mcs_groups[orig_group].streams)
            continue;

        if (primary)
            *idx = mi->groups[group].max_group_tp_rate[0];
        else
            *idx = mi->groups[group].max_group_tp_rate[1];
        break;
    }
}
extern unsigned char get_fitable_mcs_rate(struct wifi_station *sta, unsigned char bw);
static void
minstrel_ht_tx_status(void *priv, struct ieee80211_supported_band *sband,
                      void *priv_sta,  struct ieee80211_tx_info *info,void *p_sta)
{
    struct minstrel_ht_sta_priv *msp = priv_sta;
    struct minstrel_ht_sta *mi = &msp->ht;
    struct ieee80211_tx_rate *ar = info->status.rates;
    struct wifi_station *sta  = (struct wifi_station *)p_sta;
    struct minstrel_rate_stats *rate, *rate2;
    struct minstrel_priv *mp = priv;
    bool last, update = false;
    int i;
    int max_rate = 0;
    unsigned char bw = sta->sta_chbw;

    if (!msp->is_ht)
        return mac80211_minstrel.tx_status(priv, sband, &msp->legacy, info,p_sta);

    // AML_PRINT(AML_LOG_ID_RATE,AML_LOG_LEVEL_DEBUG, "info->flags =%d\n",info->flags);
    /* This packet was aggregated but doesn't carry status info */
    if ((info->flags & IEEE80211_TX_CTL_AMPDU) &&
        !(info->flags & IEEE80211_TX_STAT_AMPDU))
        return;

    if (!(info->flags & IEEE80211_TX_STAT_AMPDU)) {
        info->status.ampdu_ack_len = (info->flags & IEEE80211_TX_STAT_ACK ? 1 : 0);
        info->status.ampdu_len = 1;
    }

    mi->ampdu_packets++;
    mi->ampdu_len += info->status.ampdu_len;

    if (!mi->sample_wait && !mi->sample_tries && mi->sample_count > 0) {
        mi->sample_wait = 10;
        mi->sample_tries = 2;
        mi->sample_count--;
        //AML_PRINT(AML_LOG_ID_RATE,AML_LOG_LEVEL_DEBUG, "reinit sample_wait and sample_tries, sample_wait=%d,mi->sample_count=%d\n", mi->sample_wait,mi->sample_count);
    }

    if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
        mi->sample_packets += info->status.ampdu_len;

    for (i = 0; i < 3; i++) {
        last = !minstrel_ht_txstat_valid(mp, &ar[i + 1]);

        rate = minstrel_ht_get_stats(mp, mi, &ar[i]);
        rate->attempts += ar[i].count * info->status.ampdu_len;

        if (last) {
            rate->success += info->status.ampdu_ack_len;
        }

        if (last) {
            break;
        }
    }

    /*
    * check for sudden death of spatial multiplexing,
    * downgrade to a lower number of streams if necessary.
    */
    rate = minstrel_get_ratestats(mi, mi->max_tp_rate[0]);
    if (rate->attempts > 30 &&
        MINSTREL_FRAC(rate->success, rate->attempts) < MINSTREL_FRAC(20, 100)) {
        minstrel_downgrade_rate(mi, &mi->max_tp_rate[0], true);
        update = true;
    }

    rate2 = minstrel_get_ratestats(mi, mi->max_tp_rate[1]);
    if (rate2->attempts > 30 &&
        MINSTREL_FRAC(rate2->success, rate2->attempts) < MINSTREL_FRAC(20, 100)) {
        minstrel_downgrade_rate(mi, &mi->max_tp_rate[1], false);
        update = true;
    }

    if (time_after(jiffies, mi->last_stats_update + (mp->update_interval * HZ) * 5 / 1000)) {
        update = true;
        max_rate = get_fitable_mcs_rate(sta, bw);
        if(mi->need_clear_rate_index && mi->need_clear_rate_index < max_rate) {
            max_rate = mi->need_clear_rate_index;
        }
        mi->need_clear_rate_index = 0;
        if(sta->sta_wnet_vif->txtp_stat.vm_tx_speed > 0) {
            minstrel_clear_unfitable_rate_stats(mi, max_rate);
        }
        minstrel_ht_update_stats(mp, mi);
    }

    if (update) {
        minstrel_ht_update_rates(mp, mi);
    }
}

static void
minstrel_calc_retransmit(struct minstrel_priv *mp, struct minstrel_ht_sta *mi,
                         int index)
{
	struct minstrel_rate_stats *mrs;
	const struct mcs_group *group;
	unsigned int tx_time, tx_time_rtscts, tx_time_data;
	unsigned int cw = mp->cw_min;
	unsigned int ctime = 0;
	unsigned int t_slot = 9; /* FIXME */
	unsigned int ampdu_len = MINSTREL_TRUNC(mi->avg_ampdu_len);
	unsigned int overhead = 0, overhead_rtscts = 0;

	mrs = minstrel_get_ratestats(mi, index);
	if (mrs->prob_ewma < MINSTREL_FRAC(1, 10)) {
		mrs->retry_count = 1;
		mrs->retry_count_rtscts = 1;
		return;
	}

	mrs->retry_count = 2;
	mrs->retry_count_rtscts = 2;
	mrs->retry_updated = true;

	group = &minstrel_mcs_groups[index / MCS_GROUP_RATES];
	tx_time_data = group->duration[index % MCS_GROUP_RATES] * ampdu_len / 1000;

	/* Contention time for first 2 tries */
	ctime = (t_slot * cw) >> 1;
	cw = MIN((cw << 1) | 1, mp->cw_max);
	ctime += (t_slot * cw) >> 1;
	cw = MIN((cw << 1) | 1, mp->cw_max);

	if (index / MCS_GROUP_RATES != MINSTREL_CCK_GROUP) {
		overhead = mi->overhead;
		overhead_rtscts = mi->overhead_rtscts;
	}

	/* Total TX time for data and Contention after first 2 tries */
	tx_time = ctime + 2 * (overhead + tx_time_data);
	tx_time_rtscts = ctime + 2 * (overhead_rtscts + tx_time_data);

	/* See how many more tries we can fit inside segment size */
	do {
		/* Contention time for this try */
		ctime = (t_slot * cw) >> 1;
		cw = MIN((cw << 1) | 1, mp->cw_max);

		/* Total TX time after this try */
		tx_time += ctime + overhead + tx_time_data;
		tx_time_rtscts += ctime + overhead_rtscts + tx_time_data;

		if (tx_time_rtscts < mp->segment_size)
			mrs->retry_count_rtscts++;
	} while ((tx_time < mp->segment_size) &&
	         (++mrs->retry_count < mp->max_retry));
}


static void
minstrel_ht_set_rate(struct minstrel_priv *mp, struct minstrel_ht_sta *mi,
                     struct ieee80211_sta_rates *ratetbl, int offset, int index)
{
    const struct mcs_group *group = &minstrel_mcs_groups[index / MCS_GROUP_RATES];
    struct minstrel_rate_stats *mrs;
    u8 idx;
    u16 flags = group->flags;

    mrs = minstrel_get_ratestats(mi, index);
    if (!mrs->retry_updated)
        minstrel_calc_retransmit(mp, mi, index);

    if (mrs->prob_ewma < MINSTREL_FRAC(20, 100) || !mrs->retry_count) {
        ratetbl->rate[offset].count = 2;
        ratetbl->rate[offset].count_rts = 2;
        ratetbl->rate[offset].count_cts = 2;

    } else {
        ratetbl->rate[offset].count = mrs->retry_count;
        ratetbl->rate[offset].count_cts = mrs->retry_count;
        ratetbl->rate[offset].count_rts = mrs->retry_count_rtscts;
    }

    if (index / MCS_GROUP_RATES == MINSTREL_CCK_GROUP)
        idx = mp->cck_rates[index % ARRAY_SIZE(mp->cck_rates)];
    else if (flags & IEEE80211_TX_RC_VHT_MCS)
        idx = ((group->streams - 1) << 4) | ((index % MCS_GROUP_RATES) & 0xF);
    else
        idx = index % MCS_GROUP_RATES + (group->streams - 1) * 8;

    /* enable RTS/CTS if needed:
    *  - if station is in dynamic SMPS (and streams > 1)
    *  - for fallback rates, to increase chances of getting through
    */
    if (offset > 0 || (mi->sta->smps_mode == IEEE80211_SMPS_DYNAMIC &&
        group->streams > 1)) {
        ratetbl->rate[offset].count = ratetbl->rate[offset].count_rts;
        flags |= IEEE80211_TX_RC_USE_RTS_CTS;
    }

    ratetbl->rate[offset].idx = idx;
    ratetbl->rate[offset].flags = flags;
}

static inline int
minstrel_ht_get_prob_ewma(struct minstrel_ht_sta *mi, int rate)
{
	int group = rate / MCS_GROUP_RATES;
	rate %= MCS_GROUP_RATES;
	return mi->groups[group].rates[rate].prob_ewma;
}



static void
minstrel_ht_update_rates(struct minstrel_priv *mp, struct minstrel_ht_sta *mi)
{
    struct ieee80211_sta_rates *rates;
    int i = 0;

    rates = mi->sta->rates;
    if (!rates)
        return;

    minstrel_ht_modify_tp_rates(mi, mi->max_tp_rate, &mi->max_prob_rate);

    /* Start with max_tp_rate[0] */
    minstrel_ht_set_rate(mp, mi, rates, i++, mi->max_tp_rate[0]);

    if (mp->hw->max_rates >= 3) {
        /* At least 3 tx rates supported, use max_tp_rate[1] next */
        minstrel_ht_set_rate(mp, mi, rates, i++, mi->max_tp_rate[1]);
    }

    if (mp->hw->max_rates >= 2) {
        /*
        * At least 2 tx rates supported, use max_prob_rate next */
        minstrel_ht_set_rate(mp, mi, rates, i++, mi->max_prob_rate);
    }

    //mi->sta->max_rc_amsdu_len = minstrel_ht_get_max_amsdu_len(mi);
    rates->rate[i].idx = -1;
    //rate_control_set_rates(mp->hw, mi->sta, rates);
}

static inline int
minstrel_get_duration(int index)
{
	const struct mcs_group *group = &minstrel_mcs_groups[index / MCS_GROUP_RATES];
	return group->duration[index % MCS_GROUP_RATES];
}

static int minstrel_check_sample_idx(int tp_rate1_grp, int tp_rate1_idx, unsigned int *sample_group, int *sample_idx, struct minstrel_ht_sta *mi)
{
    struct minstrel_mcs_group_data *mg = &mi->groups[tp_rate1_grp];
    struct minstrel_rate_stats *mrs = &mg->rates[tp_rate1_idx];
    int tmp_prob_success = 50;

    if(mrs->prob_ewma < MINSTREL_FRAC(40, 100) && tp_rate1_idx > 1 && mrs->attempts > 30 
        && tp_rate1_grp < 4) {
        mi->need_clear_rate_index = tp_rate1_idx - 1;
    }
    if((tp_rate1_idx % MCS_GROUP_RATES == 6 || tp_rate1_idx % MCS_GROUP_RATES == 5) && tp_rate1_grp < 4)
        tmp_prob_success = 90;

    if ((*sample_idx > tp_rate1_idx) && (mrs->prob_ewma < MINSTREL_FRAC(tmp_prob_success, 100))) {
        while (1) {
            *sample_group = mi->sample_group;
            mg = &mi->groups[*sample_group];
            *sample_idx = sample_table[mg->column][mg->index];
            minstrel_set_next_sample_idx(mi);

            if (!(mi->supported[*sample_group] & BIT(*sample_idx))) {
                continue ;
            }

            if (*sample_idx <= tp_rate1_idx) {
                break;
            }
        }
    }

    return 0;

}

static int
minstrel_get_sample_rate(struct minstrel_priv *mp, struct minstrel_ht_sta *mi, struct ieee80211_tx_info *info)
{
    struct minstrel_rate_stats *mrs;
    struct minstrel_mcs_group_data *mg;
    unsigned int sample_dur, sample_group, cur_max_tp_streams;
    int tp_rate1, tp_rate2;
    int sample_idx = 0;
    int mcs_idx = 0;
    int tp_rate1_grp = 0;
    int tp_rate1_idx = 0;
    struct ieee80211_tx_rate *rate;
    const struct mcs_group *sample_mcs_group;

    if (mi->sample_wait > 0) {
        mi->sample_wait--;
        return -1;
    }

    if (!mi->sample_tries)
        return -1;

    sample_group = mi->sample_group;
    mg = &mi->groups[sample_group];
    sample_idx = sample_table[mg->column][mg->index];

    minstrel_set_next_sample_idx(mi);

    if (!(mi->supported[sample_group] & BIT(sample_idx))) {
        return -1;
    }

    /* Set tp_rate1, tp_rate2 to the highest / second highest max_tp_rate */
    if (minstrel_get_duration(mi->max_tp_rate[0]) > minstrel_get_duration(mi->max_tp_rate[1])) {
        tp_rate1 = mi->max_tp_rate[1];
        tp_rate2 = mi->max_tp_rate[0];

    } else {
        tp_rate1 = mi->max_tp_rate[0];
        tp_rate2 = mi->max_tp_rate[1];
    }

    tp_rate1_grp = tp_rate1 / MCS_GROUP_RATES;
    tp_rate1_idx = tp_rate1 % MCS_GROUP_RATES;
    minstrel_check_sample_idx(tp_rate1_grp, tp_rate1_idx, &sample_group, &sample_idx, mi);

    mrs = &mg->rates[sample_idx];
    mcs_idx = sample_idx;
    sample_idx += sample_group * MCS_GROUP_RATES;
    if (mcs_idx - tp_rate1_idx > 1) {
        mcs_idx = tp_rate1_idx + (tp_rate1 % 2) + 1;
        sample_idx = mcs_idx + sample_group * MCS_GROUP_RATES;
    }
    AML_PRINT(AML_LOH_ID_RATE_CTR,AML_LOG_LEVEL_DEBUG, "tp_rate1:%d, sample_idx:%d\n", tp_rate1, sample_idx);

    /*
    * Sampling might add some overhead (RTS, no aggregation)
    * to the frame. Hence, don't use sampling for the highest currently
    * used highest throughput or probability rate.
    */
    if (sample_idx == mi->max_tp_rate[0] || sample_idx == mi->max_prob_rate)
        return -1;

    if (mi->sample_init_flag) {
        sample_idx = mi->sample_init_idx;
        mi->sample_init_counts++;
        if (mi->sample_init_counts > mi->sample_force_counts) {
            mi->sample_init_flag = 0;
        }
    }

    /*
    * Make sure that lower rates get sampled only occasionally,
    * if the link is working perfectly.
    */
    cur_max_tp_streams = minstrel_mcs_groups[tp_rate1 / MCS_GROUP_RATES].streams;
    sample_dur = minstrel_get_duration(sample_idx);
    if (sample_dur >= minstrel_get_duration(tp_rate2)
        && (cur_max_tp_streams - 1 < minstrel_mcs_groups[sample_group].streams
        || sample_dur >= minstrel_get_duration(mi->max_prob_rate))
        && (tp_rate1_idx - mcs_idx > 1)
        && (mrs->prob_ewma > MINSTREL_FRAC(70, 100))) {
        rate = &info->status.rates[1];

    } else {
        rate = &info->status.rates[0];
    }

    sample_mcs_group = &minstrel_mcs_groups[sample_idx / MCS_GROUP_RATES];
    rate->count = 1;

    if (sample_idx / MCS_GROUP_RATES == MINSTREL_CCK_GROUP) {
        int idx = sample_idx % ARRAY_SIZE(mp->cck_rates);
        rate->idx = mp->cck_rates[idx];

    } else if (sample_mcs_group->flags & IEEE80211_TX_RC_VHT_MCS) {
        ieee80211_rate_set_vht(rate, sample_idx % MCS_GROUP_RATES, sample_mcs_group->streams);

    } else {
        rate->idx = sample_idx % MCS_GROUP_RATES + (sample_mcs_group->streams - 1) * 8;
    }

    rate->flags = sample_mcs_group->flags;
    info->flags |= IEEE80211_TX_CTL_RATE_CTRL_PROBE;

    mi->sample_tries--;
    return sample_idx;
}

static void
minstrel_ht_get_rate(void *priv, struct ieee80211_sta_aml *sta, void *priv_sta,
                     struct ieee80211_tx_info *info)
{
    struct minstrel_ht_sta_priv *msp = priv_sta;
    struct minstrel_ht_sta *mi = &msp->ht;
    struct minstrel_priv *mp = priv;

    if (!msp->is_ht)
        return mac80211_minstrel.get_rate(priv, sta, &msp->legacy, info);

    info->flags |= mi->tx_flags;

#ifdef CONFIG_MAC80211_DEBUGFS
    if (mp->fixed_rate_idx != -1)
        return;
#endif

    /* Don't use EAPOL frames for sampling on non-mrr hw */
    if (!((mp->hw->max_rates == 1) && (info->control.flags & IEEE80211_TX_CTRL_PORT_CTRL_PROTO))) {
        minstrel_get_sample_rate(mp, mi, info);
    }

    mi->total_packets++;
    if (mi->total_packets == ~0) {
        mi->total_packets = 0;
        mi->sample_packets = 0;
    }
}

static void
minstrel_ht_update_cck(struct minstrel_priv *mp, struct minstrel_ht_sta *mi,
		       struct ieee80211_supported_band *sband,
		       struct ieee80211_sta_aml *sta)
{
	int i;
#if LINUX_VERSION_CODE > KERNEL_VERSION(3,14,29)
	if (sband->band != (enum nl80211_band)IEEE80211_BAND_2GHZ)
		return;
#else
     if (sband->band != IEEE80211_BAND_2GHZ)
		return;
#endif


	mi->cck_supported = 0;
	mi->cck_supported_short = 0;
	for (i = 0; i < 4; i++) {
		if (!rate_supported_aml(sta, sband->band, mp->cck_rates[i]))
			continue;

		mi->cck_supported |= BIT(i);
		if (sband->bitrates[i].flags & IEEE80211_RATE_SHORT_PREAMBLE)
			mi->cck_supported_short |= BIT(i);
	}

	mi->supported[MINSTREL_CCK_GROUP] = mi->cck_supported;
}

static void
minstrel_ht_update_caps(void *priv, struct ieee80211_supported_band *sband,
			/*struct cfg80211_chan_def *chandef,*/
					struct ieee80211_sta_aml *sta, void *priv_sta)
{
	struct minstrel_priv *mp = priv;
	struct minstrel_ht_sta_priv *msp = priv_sta;
	struct minstrel_ht_sta *mi = &msp->ht;
	struct ieee80211_mcs_info *mcs = &sta->ht_cap.mcs;
	u16 sta_cap = sta->ht_cap.cap;
	struct ieee80211_sta_vht_cap *vht_cap = &sta->vht_cap;
	int use_vht;
	int n_supported = 0;
	int ack_dur;
	int stbc;
	int i;

	/* fall back to the old minstrel for legacy stations */
	if (!sta->ht_cap.ht_supported)
		goto use_legacy;

	//ASSERT(ARRAY_SIZE(minstrel_mcs_groups) != MINSTREL_GROUPS_NB);

#ifdef CONFIG_MAC80211_RC_MINSTREL_VHT
	if (vht_cap->vht_supported) {
		use_vht = (vht_cap->vht_mcs.tx_mcs_map != 0);
	}
	else
#endif
	use_vht = 0;
	msp->is_ht = true;
	memset(mi, 0, sizeof(*mi));

	mi->sta = sta;
	mi->last_stats_update = jiffies;

	ack_dur = ieee80211_frame_duration(sband->band, 10, 60, 1, 1, 0);
	mi->overhead = ieee80211_frame_duration(sband->band, 0, 60, 1, 1, 0);
	mi->overhead += ack_dur;
	mi->overhead_rtscts = mi->overhead + 2 * ack_dur;

	mi->avg_ampdu_len = MINSTREL_FRAC(1, 1);

	/* When using MRR, sample more on the first attempt, without delay */
	if (mp->has_mrr) {
		mi->sample_count = 32;
		mi->sample_wait = 0;
	} else {
		mi->sample_count = 8;
		mi->sample_wait = 8;
	}
	mi->sample_tries = 4;

	/* TODO tx_flags for vht - ATM the RC API is not fine-grained enough */
	if (!use_vht) {
		stbc = (sta_cap & IEEE80211_HT_CAP_RX_STBC) >>
			IEEE80211_HT_CAP_RX_STBC_SHIFT;
		mi->tx_flags |= stbc << IEEE80211_TX_CTL_STBC_SHIFT;

		if (sta_cap & IEEE80211_HT_CAP_LDPC_CODING)
			mi->tx_flags |= IEEE80211_TX_CTL_LDPC;
	}

	for (i = 0; i < ARRAY_SIZE(mi->groups); i++) {
		u32 gflags = minstrel_mcs_groups[i].flags;
		int bw, nss;

		mi->supported[i] = 0;
		if (i == MINSTREL_CCK_GROUP) {
			minstrel_ht_update_cck(mp, mi, sband, sta);
			continue;
		}

		if ((!(gflags & IEEE80211_TX_RC_80_MHZ_WIDTH)) && (gflags & IEEE80211_TX_RC_SHORT_GI)) {
			if (gflags & IEEE80211_TX_RC_40_MHZ_WIDTH) {
				if (!(sta_cap & IEEE80211_HT_CAP_SGI_40))
					continue;
			} else {
				if (!(sta_cap & IEEE80211_HT_CAP_SGI_20))
					continue;
			}
		}

		if (gflags & IEEE80211_TX_RC_40_MHZ_WIDTH &&
		    sta->bandwidth < IEEE80211_STA_RX_BW_40)
			continue;

		nss = minstrel_mcs_groups[i].streams;

		/* Mark MCS > 7 as unsupported if STA is in static SMPS mode */
		if (sta->smps_mode == IEEE80211_SMPS_STATIC && nss > 1)
			continue;

		/* HT rate */
		if (gflags & IEEE80211_TX_RC_MCS) {
#ifdef CONFIG_MAC80211_RC_MINSTREL_VHT
			if (use_vht && minstrel_vht_only)
				continue;
#endif
			mi->supported[i] = mcs->rx_mask[nss - 1];
			AML_PRINT_LOG_INFO("mi->supported:%08x:%d\n", mi->supported[i], i);
			if (mi->supported[i])
				n_supported++;
			continue;
		}

		//ASSERT(!(gflags & IEEE80211_TX_RC_VHT_MCS));
		//ASSERT(gflags & IEEE80211_TX_RC_160_MHZ_WIDTH);
		/* VHT rate */
		if (!vht_cap->vht_supported ||
			(!(gflags & IEEE80211_TX_RC_VHT_MCS)) ||
			(gflags & IEEE80211_TX_RC_160_MHZ_WIDTH))
			continue;

		if (gflags & IEEE80211_TX_RC_80_MHZ_WIDTH) {
			if (((gflags & IEEE80211_TX_RC_SHORT_GI) && 
				!(vht_cap->cap & IEEE80211_VHT_CAP_SHORT_GI_80))
				 || sta->bandwidth < IEEE80211_STA_RX_BW_80) {
				continue;
			}
		}

		if (gflags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			bw = BW_40;
		else if (gflags & IEEE80211_TX_RC_80_MHZ_WIDTH)
			bw = BW_80;
		else
			bw = BW_20;

		if (sta->bandwidth < bw) {
			continue;
		}

		mi->supported[i] = minstrel_get_valid_vht_rates(bw, nss, vht_cap->vht_mcs.tx_mcs_map);
		AML_PRINT_LOG_INFO("mi->supported:%08x:%d\n", mi->supported[i], i);

		if (mi->supported[i])
			n_supported++;
	}

	if (!n_supported)
		goto use_legacy;

	/* create an initial rate table with the lowest supported rates */
	minstrel_ht_update_stats(mp, mi);
	minstrel_ht_update_rates(mp, mi);

	return;

use_legacy:
	msp->is_ht = false;
	memset(&msp->legacy, 0, sizeof(msp->legacy));
	msp->legacy.r = msp->ratelist;
	msp->legacy.sample_table = msp->sample_table;
	return mac80211_minstrel.rate_init(priv, sband, /*chandef,*/ sta,
					   &msp->legacy);
}

static void
minstrel_ht_rate_init(void *priv, struct ieee80211_supported_band *sband,
			struct ieee80211_sta_aml *sta, void *priv_sta)
{
	minstrel_ht_update_caps(priv, sband, /*chandef,*/ sta, priv_sta);
}

static void
minstrel_ht_rate_update(void *priv, struct ieee80211_supported_band *sband,
			/*struct cfg80211_chan_def *chandef,*/
			struct ieee80211_sta_aml *sta, void *priv_sta,
                        u32 changed)
{
	minstrel_ht_update_caps(priv, sband, /*chandef,*/ sta, priv_sta);
}

static void *
minstrel_ht_alloc_sta(void *priv, struct ieee80211_sta_aml *sta, gfp_t gfp)
{
    struct ieee80211_supported_band *sband;
    struct minstrel_ht_sta_priv *msp;
    struct minstrel_priv *mp = priv;
    struct ieee80211_hw *hw = mp->hw;
    int max_rates = 0;
    int i;

    AML_PRINT_LOG_INFO("priv = %p\n", priv);
    for (i = 0; i < IEEE80211_BAND_60GHZ; i++) {
        sband = hw->wiphy->bands[i];
        if (sband && sband->n_bitrates > max_rates)
        max_rates = sband->n_bitrates;
    }

    msp = (struct minstrel_ht_sta_priv *)ZMALLOC(sizeof(*msp), "minstrel_ht_sta_priv", gfp);
    if (!msp)
        return NULL;

    msp->ratelist = ZMALLOC(sizeof(struct minstrel_rate) * max_rates, "minstrel_ratelist", gfp);
    if (!msp->ratelist)
        goto error;

    msp->sample_table = ZMALLOC(SAMPLE_COLUMNS * max_rates, "minstrel_ht_sample_table", gfp);
    if (!msp->sample_table)
        goto error1;

    return msp;

error1:
    FREE(msp->ratelist, "minstrel_ratelist");
error:
    FREE(msp, "minstrel_ht_sta_priv");
    return NULL;
}

static void
minstrel_ht_free_sta(void *priv_sta)
{
    struct minstrel_ht_sta_priv *msp = priv_sta;

    if (msp == NULL) {
        return;
    }

    FREE(msp->sample_table, "minstrel_ht_sample_table");
    FREE(msp->ratelist, "minstrel_ratelist");
    FREE(msp, "minstrel_ht_sta_priv");
}

static void init_sample_table(void);
static void *minstrel_ht_alloc(struct ieee80211_hw *hw)
{
	init_sample_table();
	return mac80211_minstrel.alloc(hw);
}

static void minstrel_ht_free(void *priv)
{
	mac80211_minstrel.free(priv);
}

static u32 minstrel_ht_get_expected_throughput(void *priv_sta)
{
	struct minstrel_ht_sta_priv *msp = priv_sta;
	struct minstrel_ht_sta *mi = &msp->ht;
	int i, j, prob, tp_avg;

	if (!msp->is_ht)
		return mac80211_minstrel.get_expected_throughput(priv_sta);

	i = mi->max_tp_rate[0] / MCS_GROUP_RATES;
	j = mi->max_tp_rate[0] % MCS_GROUP_RATES;
	prob = mi->groups[i].rates[j].prob_ewma;

	/* convert tp_avg from pkt per second in kbps */
	tp_avg = minstrel_ht_get_tp_avg(mi, i, j, prob) * 10;
	tp_avg = tp_avg * AVG_PKT_SIZE * 8 / 1024;

	return tp_avg;
}

static  struct minstrel_rate_control_ops mac80211_minstrel_ht = {
	.name = "minstrel_ht",
	.tx_status = minstrel_ht_tx_status,
	.get_rate = minstrel_ht_get_rate,
	.rate_init = minstrel_ht_rate_init,
	.rate_update = minstrel_ht_rate_update,
	.alloc_sta = minstrel_ht_alloc_sta,
	.free_sta = minstrel_ht_free_sta,
	.alloc = minstrel_ht_alloc,
	.free = minstrel_ht_free,
#ifdef CONFIG_MAC80211_DEBUGFS
	.add_sta_debugfs = minstrel_ht_add_sta_debugfs,
	.remove_sta_debugfs = minstrel_ht_remove_sta_debugfs,
#endif
	.get_expected_throughput = minstrel_ht_get_expected_throughput,
};


static void  init_sample_table(void)
{
    int col, i, new_idx;
    u8 rnd[MCS_GROUP_RATES];

    memset(sample_table, 0xff, sizeof(sample_table));
    for (col = 0; col < SAMPLE_COLUMNS; col++) {
        prandom_bytes(rnd, sizeof(rnd));
        for (i = 0; i < MCS_GROUP_RATES; i++) {
            new_idx = (i + rnd[i]) % MCS_GROUP_RATES;
            while (sample_table[col][new_idx] != 0xff)
            new_idx = (new_idx + 1) % MCS_GROUP_RATES;
            sample_table[col][new_idx] = i;
        }
    }
}

struct minstrel_rate_control_ops* get_rate_control_ops_ht(void)
{
    return &mac80211_minstrel_ht;
}
