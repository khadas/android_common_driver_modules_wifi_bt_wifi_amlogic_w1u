#include "wifi_mac_com.h"

#ifdef  CONFIG_CONCURRENT_MODE
void concurrent_vsdb_init(struct wifi_mac *wifimac)
{
    wifimac->wm_vsdb_slot = CONCURRENT_SLOT_NONE;
    wifimac->wm_vsdb_flags = 0;
    wifimac->vsdb_mode_set_noa_enable = 0;
}

static void concurrent_change_channel_timeout_ex(SYS_TYPE param1,
                                            SYS_TYPE param2,SYS_TYPE param3, SYS_TYPE param4,SYS_TYPE param5)
{
    struct wifi_mac *wifimac = (struct wifi_mac *)param1;
    struct drv_private *drv_priv = wifimac->drv_priv;
    struct wlan_net_vif *main_vmac = drv_priv->drv_wnet_vif_table[NET80211_MAIN_VMAC];
    struct wlan_net_vif *p2p_vmac = drv_priv->drv_wnet_vif_table[NET80211_P2P_VMAC];

    AML_PRINT(AML_LOG_ID_P2P, AML_LOG_LEVEL_DEBUG, "++\n");

    if (vm_p2p_is_state(p2p_vmac->vm_p2p, NET80211_P2P_STATE_LISTEN)) {
        vm_p2p_cancel_remain_channel(p2p_vmac->vm_p2p);
        AML_PRINT(AML_LOG_ID_P2P, AML_LOG_LEVEL_DEBUG, "p2p in listen state\n");
        return;

    } else if (vm_p2p_is_state(p2p_vmac->vm_p2p, NET80211_P2P_STATE_SCAN)) {
        AML_PRINT(AML_LOG_ID_P2P, AML_LOG_LEVEL_DEBUG, "p2p in scan state\n");
        return;
    }

    if (main_vmac->vm_curchan != WIFINET_CHAN_ERR)
    {
        AML_PRINT(AML_LOG_ID_P2P, AML_LOG_LEVEL_DEBUG, "pri_chan%d\n", main_vmac->vm_curchan->chan_pri_num);
        wifi_mac_restore_wnet_vif_channel(main_vmac);
    }
}

int concurrent_change_channel_timeout(void * data)
{
    struct wifi_mac *wifimac = (struct wifi_mac *)data;

    AML_PRINT(AML_LOG_ID_LOG, AML_LOG_LEVEL_INFO, "\n" );

    wifimac->wm_p2p_connection_protect = 0;
    wifi_mac_add_work_task(wifimac, concurrent_change_channel_timeout_ex,NULL,(SYS_TYPE)data,0,0,0,0 );
    return OS_TIMER_NOT_REARMED;
}

void concurrent_channel_restore(int target_channel, struct wlan_net_vif *wnet_vif, int times)
{
    struct wifi_mac *wifimac = wnet_vif->vm_wmac;
    struct wifi_channel *main_vmac_chan = NULL;

    AML_PRINT(AML_LOG_ID_CFG80211, AML_LOG_LEVEL_INFO, "\n");

    main_vmac_chan = wifi_mac_get_main_vmac_channel(wifimac);
    if ((main_vmac_chan != WIFINET_CHAN_ERR) && (times != 0))
    {
        os_timer_ex_start_period(&wifimac->wm_concurrenttimer, times);
    }
}

void concurrent_channel_protection(struct wlan_net_vif *wnet_vif, int times)
{
    struct wifi_mac *wifimac = wnet_vif->vm_wmac;

    AML_PRINT(AML_LOG_ID_CFG80211, AML_LOG_LEVEL_INFO, "wm_p2p_connection_protect_period:%d\n", times);

    wifimac->wm_p2p_connection_protect = 1;
    wifimac->wm_p2p_connection_protect_period = jiffies + times * HZ / 1000;
}

static void concurrent_vsdb_do_channel_change_ex(SYS_TYPE param1,
    SYS_TYPE param2,SYS_TYPE param3, SYS_TYPE param4,SYS_TYPE param5)
{
    struct wifi_mac *wifimac = (struct wifi_mac *)param1;
    struct drv_private *drv_priv = wifimac->drv_priv;
    struct wlan_net_vif *main_vmac = drv_priv->drv_wnet_vif_table[NET80211_MAIN_VMAC];
    struct wlan_net_vif *p2p_vmac = drv_priv->drv_wnet_vif_table[NET80211_P2P_VMAC];
    struct wlan_net_vif *selected_vmac = NULL;

    if (wifimac->wm_nrunning > 1) {
        if (wifimac->wm_vsdb_flags & CONCURRENT_CHANNEL_SWITCH) {
            wifimac->wm_vsdb_flags &= ~CONCURRENT_CHANNEL_SWITCH;
            if (wifimac->wm_vsdb_slot == CONCURRENT_SLOT_STA) {
                if (p2p_vmac->vm_curchan != WIFINET_CHAN_ERR) {
                    wifimac->wm_vsdb_slot = CONCURRENT_SLOT_P2P;
                    wifi_mac_restore_wnet_vif_channel(p2p_vmac);
                }

            } else if (wifimac->wm_vsdb_slot == CONCURRENT_SLOT_P2P) {
                if (main_vmac->vm_curchan != WIFINET_CHAN_ERR) {
                    wifimac->wm_vsdb_slot = CONCURRENT_SLOT_STA;
                    wifi_mac_restore_wnet_vif_channel(main_vmac);
                }
            }

            AML_PRINT(AML_LOG_ID_CONNECT, AML_LOG_LEVEL_DEBUG,"%s, slot:%d\n", wifimac->wm_vsdb_slot);

            if (wifimac->wm_vsdb_slot != CONCURRENT_SLOT_NONE) {
                selected_vmac = drv_priv->drv_wnet_vif_table[wifimac->wm_vsdb_slot];
                wifi_mac_scan_notify_leave_or_back(selected_vmac, 0);
            }
        }
    }

    wifimac->wm_vsdb_flags = 0;
}

void concurrent_vsdb_do_channel_change(void * data)
{
    struct wifi_mac *wifimac = (struct wifi_mac *)data;
    if (wifimac->wm_vsdb_flags & CONCURRENT_NOTIFY_AP_SUCCESS) {
        wifimac->wm_vsdb_flags &= ~CONCURRENT_NOTIFY_AP_SUCCESS;
    }
    if (wifimac->wm_vsdb_flags & CONCURRENT_AP_SWITCH_CHANNEL) {
        wifimac->wm_vsdb_flags &= ~CONCURRENT_AP_SWITCH_CHANNEL;
    }
    wifi_mac_add_work_task(wifimac, concurrent_vsdb_do_channel_change_ex,NULL,(SYS_TYPE)data,0,0,0,0);
}

static void concurrent_vsdb_change_channel_ex(SYS_TYPE param1,
                                            SYS_TYPE param2,SYS_TYPE param3, SYS_TYPE param4,SYS_TYPE param5)
{
    struct wifi_mac *wifimac = (struct wifi_mac *)param1;
    struct drv_private *drv_priv = wifimac->drv_priv;
    struct wlan_net_vif *main_vmac = drv_priv->drv_wnet_vif_table[NET80211_MAIN_VMAC];
    struct wlan_net_vif *p2p_vmac = drv_priv->drv_wnet_vif_table[NET80211_P2P_VMAC];

    AML_PRINT(AML_LOG_ID_CONNECT, AML_LOG_LEVEL_DEBUG, "slot:%d\n", wifimac->wm_vsdb_slot);

    if ((wifimac->wm_nrunning > 1) && (wifimac->wm_vsdb_flags & CONCURRENT_CHANNEL_SWITCH)) {
        if (wifimac->wm_vsdb_slot == CONCURRENT_SLOT_STA) {
            wifi_mac_scan_notify_leave_or_back(main_vmac, 1);
            wifimac->drv_priv->drv_ops.drv_set_is_mother_channel(wifimac->drv_priv, main_vmac->wnet_vif_id, 1);
            wifimac->wm_vsdb_flags |= CONCURRENT_NOTIFY_AP;

        } else if (wifimac->wm_vsdb_slot == CONCURRENT_SLOT_P2P) {
            if (p2p_vmac->vm_p2p->p2p_role == NET80211_P2P_ROLE_CLIENT) {
                wifi_mac_scan_notify_leave_or_back(p2p_vmac, 1);
                wifimac->drv_priv->drv_ops.drv_set_is_mother_channel(wifimac->drv_priv, p2p_vmac->wnet_vif_id, 1);
                wifimac->wm_vsdb_flags |= CONCURRENT_NOTIFY_AP;
            } else if (IS_APSTA_CONCURRENT(aml_wifi_get_con_mode()) && !concurrent_check_is_vmac_same_pri_channel(wifimac)) {
                if (drv_priv->hal_priv->hal_ops.hal_tx_empty()) {
                    concurrent_vsdb_do_channel_change(wifimac);
                }
                else {
                    wifimac->wm_vsdb_flags |= CONCURRENT_AP_SWITCH_CHANNEL;
                }
            } else {
                AML_PRINT_LOG_INFO("current vsdb flags %04x\n", wifimac->wm_vsdb_flags);
                wifimac->wm_vsdb_flags &= ~CONCURRENT_CHANNEL_SWITCH;
            }
        } else if (wifimac->wm_vsdb_slot == CONCURRENT_SLOT_NONE) {
            wifimac->wm_vsdb_flags &= ~CONCURRENT_CHANNEL_SWITCH;
        }

    } else if ((wifimac->wm_nrunning < 2) && (wifimac->wm_vsdb_flags & CONCURRENT_CHANNEL_SWITCH)) {
        wifimac->wm_vsdb_flags = 0;
    }
}

int concurrent_vsdb_change_channel(void * data)
{
    return 0;
}

void concurrent_vsdb_prepare_change_channel(struct wifi_mac *wifimac)
{
    struct wifi_mac_scan_state *ss = wifimac->wm_scan;
    struct drv_private *drv_priv = wifimac->drv_priv;
    struct wlan_net_vif *p2p_vmac = drv_priv->drv_wnet_vif_table[NET80211_P2P_VMAC];

    if ((wifimac->wm_nrunning > 1 && wifimac->wm_vsdb_slot == CONCURRENT_SLOT_P2P && p2p_vmac->vm_p2p->p2p_role == NET80211_P2P_ROLE_GO) ||
        (wifimac->wm_vsdb_slot == CONCURRENT_SLOT_STA && wifimac->vsdb_mode_set_noa_enable == 1)) {
        if (p2p_vmac->vm_p2p->p2p_negotiation_state != NET80211_P2P_STATE_GO_COMPLETE) {
            return;
        }
        if (wifimac->vsdb_mode_set_noa_enable == 0) {
            unsigned long long tsf = wifimac->drv_priv->drv_ops.drv_hal_get_tsf(wifimac->drv_priv, p2p_vmac->wnet_vif_id);
            unsigned int ltsf;
            unsigned int next_tbtt;
            struct p2p_noa noa = {0};

            ltsf = (unsigned int)tsf;
            next_tbtt = roundup(ltsf, p2p_vmac->vm_bcn_intval*TU_DURATION);

            noa.start = next_tbtt + p2p_vmac->vm_bcn_intval * TU_DURATION;
            noa.count = 255;
            noa.duration = p2p_vmac->vm_bcn_intval * TU_DURATION;
            noa.interval = 2 * p2p_vmac->vm_bcn_intval * TU_DURATION;
            p2p_vmac->vm_p2p->noa_index++;
            wifimac->vsdb_mode_set_noa_enable = 1;
            vm_p2p_noa_start(p2p_vmac->vm_p2p, &noa);
        }
        return;
    }

    //AML_PRINT_LOG_INFO("\n");
    if ((wifimac->wm_nrunning > 1)
        && (time_after(jiffies, wifimac->wm_vsdb_switch_time + (WIFINET_SCAN_DEFAULT_INTERVAL * HZ / 1000))
        || (wifimac->wm_vsdb_switch_time == 0))) {
        if (!(wifimac->wm_vsdb_flags & CONCURRENT_CHANNEL_SWITCH)) {
            wifimac->wm_vsdb_flags |= CONCURRENT_CHANNEL_SWITCH;
            wifi_mac_add_work_task(wifimac, concurrent_vsdb_change_channel_ex,NULL,(SYS_TYPE)wifimac,0,0,0,0);
            wifimac->wm_vsdb_switch_time = jiffies;

        } else {
            //AML_PRINT_LOG_INFO("not right time\n");
        }

        if ((wifimac->wm_vsdb_slot == CONCURRENT_SLOT_P2P)
            && (wifimac->wm_flags & WIFINET_F_SCAN) && (!(ss->scan_StateFlags & SCANSTATE_F_RESTORE))) {
            os_timer_ex_start_period(&ss->ss_scan_timer, (WIFINET_SCAN_DEFAULT_INTERVAL - ss->scan_chan_wait - 1));
        }
    }
}
#endif//CONFIG_CONCURRENT_MODE

unsigned char concurrent_check_is_vmac_same_pri_channel(struct wifi_mac *wifimac)
{
    struct drv_private *drv_priv = wifimac->drv_priv;
    struct wlan_net_vif *main_vmac = drv_priv->drv_wnet_vif_table[NET80211_MAIN_VMAC];
    struct wlan_net_vif *p2p_vmac = drv_priv->drv_wnet_vif_table[NET80211_P2P_VMAC];

    AML_PRINT(AML_LOG_ID_P2P, AML_LOG_LEVEL_DEBUG, "\n");

    if ((main_vmac->vm_curchan == WIFINET_CHAN_ERR) || (p2p_vmac->vm_curchan == WIFINET_CHAN_ERR)) {
        return 1;

    } else {
        return (main_vmac->vm_curchan->chan_pri_num == p2p_vmac->vm_curchan->chan_pri_num);
    }
}

unsigned char concurrent_check_vmac_is_AP(struct wifi_mac *wifimac)
{
    struct drv_private *drv_priv = wifimac->drv_priv;
    struct wlan_net_vif *p2p_vmac = drv_priv->drv_wnet_vif_table[NET80211_P2P_VMAC];
    AML_PRINT(AML_LOG_ID_P2P, AML_LOG_LEVEL_DEBUG, "\n");
    if (p2p_vmac->vm_opmode == WIFINET_M_HOSTAP)
        return 1;
    else
        return 0;
}

struct wlan_net_vif *wifi_mac_running_main_wnet_vif(struct wifi_mac *wifimac)
{
    struct drv_private *drv_priv = wifimac->drv_priv;
    struct wlan_net_vif *main_vmac = drv_priv->drv_wnet_vif_table[NET80211_MAIN_VMAC];

    if (main_vmac->vm_state == WIFINET_S_CONNECTED)
    {
        return main_vmac;
    }

    return NULL;
}

struct wlan_net_vif *wifi_mac_running_wnet_vif(struct wifi_mac *wifimac)
{
    struct drv_private *drv_priv = wifimac->drv_priv;
    struct wlan_net_vif *main_vmac = drv_priv->drv_wnet_vif_table[NET80211_MAIN_VMAC];
    struct wlan_net_vif *p2p_vmac = drv_priv->drv_wnet_vif_table[NET80211_P2P_VMAC];

    if (wifimac->wm_nrunning != 1) {
        return NULL;
    }

    if (main_vmac->vm_state == WIFINET_S_CONNECTED)
    {
        return main_vmac;
    }
    else if (p2p_vmac->vm_state == WIFINET_S_CONNECTED)
    {
        return p2p_vmac;
    }

    return NULL;
}

void channel_switch_announce_trigger(struct wifi_mac *wifimac, unsigned int chan_pri_num, enum wifi_mac_bwc_width bandwidth, unsigned int center_chan) {
    unsigned int delay_ms = 0;
    struct drv_private *drv_priv = wifimac->drv_priv;
    struct wlan_net_vif *p2p_wnet_vif = drv_priv->drv_wnet_vif_table[NET80211_P2P_VMAC];

    while ((wifimac->wm_flags & WIFINET_F_DOTH) && (wifimac->wm_flags & WIFINET_F_CHANSWITCH) && delay_ms < 1000) {
        mdelay(1);
        delay_ms++;
    }

    if (p2p_wnet_vif->vm_curchan && (p2p_wnet_vif->vm_curchan->chan_pri_num == chan_pri_num) && (p2p_wnet_vif->vm_curchan->chan_bw == bandwidth)) {
        AML_PRINT_LOG_INFO("channel[%d] bandwidth[%d] switch not trigger! \n",chan_pri_num, bandwidth);
        return;
    }

    if (wifi_mac_find_chan(wifimac, chan_pri_num, bandwidth, center_chan) == NULL) {
        AML_PRINT_LOG_ERR("channel[%d] bandwidth[%d]  center_chan[%d] not support !\n",chan_pri_num, bandwidth, center_chan);
        return;
    }

    AML_PRINT(AML_LOG_ID_BEACON, AML_LOG_LEVEL_DEBUG,"chan_pri_num:%d, delay_ms:%d\n",chan_pri_num,delay_ms);
    if (IS_APSTA_CONCURRENT(aml_wifi_get_con_mode()) && concurrent_check_vmac_is_AP(wifimac)) {
        p2p_wnet_vif->vm_wmac->wm_flags |= WIFINET_F_DOTH;
        p2p_wnet_vif->vm_wmac->wm_flags |= WIFINET_F_CHANSWITCH;
        p2p_wnet_vif->vm_wmac->wm_doth_channel = chan_pri_num;
        p2p_wnet_vif->vm_wmac->wm_doth_tbtt = CSA_COUNT;
        if (p2p_wnet_vif->vm_p2p->go_hidden_mode) {
            p2p_wnet_vif->vm_flags &= ~WIFINET_F_HIDESSID;
        }
    }
}