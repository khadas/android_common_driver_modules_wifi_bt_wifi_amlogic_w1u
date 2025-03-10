/*
 ****************************************************************************************
 *
 * Copyright (C) Amlogic 2010-2014
 *
 * Project: 11N 80211 mac  layer Software
 *
 * Description:
 *     wifi_mac android interface module, android call priv cmd function
 *
 *
 ****************************************************************************************
 */
#include "wifi_mac_com.h"
#include <linux/ctype.h>
#include "wifi_drv_reg_ops.h"
#include "wifi_cmd_func.h"

#ifdef CONFIG_COMPAT
    #include <linux/compat.h>
#endif // endif

const char *android_wifi_cmd_str[ANDROID_WIFI_CMD_MAX] =
{
    "START",
    "STOP",
    "SCAN-ACTIVE",
    "SCAN-PASSIVE",
    "RSSI",
    "LINKSPEED",
    "RXFILTER-START",
    "RXFILTER-STOP",
    "RXFILTER-ADD",
    "RXFILTER-REMOVE",
    "BTCOEXSCAN-START",
    "BTCOEXSCAN-STOP",
    "BTCOEXMODE",
    "SETSUSPENDMODE",
    "SETFWPATH",
    "SETBAND",
    "GETBAND",
    "COUNTRY",
    "P2P_SET_NOA",
    "P2P_GET_NOA",
    "P2P_SET_PS",
    "SET_AP_WPS_P2P_IE",//22
#ifdef PNO_SUPPORT
    "PNOSSIDCLR",
    "PNOSETUP ",
    "PNOFORCE",
    "PNODEBUG",
#endif

    "BLOCK",
    "WFD-ENABLE",
    "WFD-DISABLE",
    "WFD-SET-TCPPORT",
    "WFD-SET-MAXTPUT",
    "WFD-SET-DEVTYPE",
    "RESET",

};
static int aml_android_get_rssi(struct wlan_net_vif *wnet_vif, char *command, int total_len)
{
    int bytes_written = 0;
    if (wnet_vif->vm_state == WIFINET_S_CONNECTED)
    {
        unsigned char rssi = -100;
        if (wnet_vif->vm_mainsta != NULL)
        {
            rssi =  translate_to_dbm(wnet_vif->vm_mainsta->sta_avg_rssi);
            bytes_written += snprintf(&command[bytes_written], total_len, "%s rssi %d\n",
                wnet_vif->vm_mainsta->sta_essid, rssi);

            AML_PRINT_LOG_INFO("sta_avg_rssi:%d, sta_avg_bcn_rssi:%d\n",
                (wnet_vif->vm_mainsta->sta_avg_rssi - 255), wnet_vif->vm_mainsta->sta_avg_bcn_rssi);
        }
    }

    return bytes_written;
}
unsigned int aml_atoi(unsigned char* s)
{

    int num=0,flag=0;
    int i;
    for(i=0; i<=strlen(s); i++)
    {
        if(s[i] >= '0' && s[i] <= '9')
            num = num * 10 + s[i] -'0';
        else if(s[0] == '-' && i==0)
            flag =1;
        else
            break;
    }

    if(flag == 1)
        num = num * -1;

    return(num);

}

static int get_num_from_cmd(char* pcmd, unsigned short len)
{
    int i = 0;

    for (i = 0; i < len; i++) {
        if (pcmd[ i ] == '=') {
            i += 2;
            break;
        }
    }

    return (aml_atoi(pcmd + i));
}

static int aml_android_get_link_speed(struct wlan_net_vif *wnet_vif, char *command, int total_len)
{
    int bytes_written = 0;
    unsigned int rate;

    if (wnet_vif->vm_mainsta == NULL) {
        return -1;
    }
    rate = wifi_mac_sta_get_txrate(wnet_vif->vm_mainsta)/1000;

    bytes_written = snprintf(command, total_len, "LinkSpeed %d", rate);

    return bytes_written;
}

static int aml_android_set_country(struct wlan_net_vif *wnet_vif, char *command, int total_len)
{
    char *country_code = command + strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_COUNTRY]) + 1;

    AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_INFO, "set country <%s> by ioctl\n", country_code);

    /* wpa_cli -i wlan0 DRIVER COUNTRY will let
    *  the upper layer call aml_reg_notifier through by user
    *  regulatory_hint only notify kernel and not call aml_reg_notifier through by driver
    */
    regulatory_hint(wnet_vif->vm_wdev->wiphy,country_code);

    return 0;
}

static int wl_android_wifi_on(struct net_device *dev)
{
    int ret = 0;

    AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_INFO,"in \n");
    if (!dev)
    {
        AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_ERROR,"dev is null\n");
        return -EINVAL;
    }


    return ret;
}

static int wl_android_wifi_off(struct net_device *dev)
{
    int ret = 0;

    AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_INFO,"in \n");
    if (!dev)
    {
        AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_ERROR,"dev is null\n");
        return -EINVAL;
    }


    return ret;
}

static int aml_android_cmdstr_to_num(char *cmdstr)
{
    int cmd_num;
    for (cmd_num=0 ; cmd_num<ANDROID_WIFI_CMD_MAX; cmd_num++)
        if (0 == strnicmp(cmdstr , android_wifi_cmd_str[cmd_num],
                            strlen(android_wifi_cmd_str[cmd_num])) )
            break;

    return cmd_num;
}

#define PRIVATE_COMMAND_MAX_LEN	8192
 int aml_android_priv_cmd(struct wlan_net_vif *wnet_vif, void __user *data, int cmd)
{
    int ret = 0;
    char *command = NULL;
    int cmd_num;
    int bytes_written = 0;
    struct android_wifi_priv_cmd priv_cmd;
#ifdef CONFIG_P2P
    int skip = 0;
#endif
    int i = 0;
    int is_found = 0;

    if (!IS_RUNNING(wnet_vif->vm_ndev))
    {
        AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_WARN,"flags %x cmd abort\n", wnet_vif->vm_ndev->flags);
        ret = -EFAULT;
        goto exit;
    }

    if (!data) {
        AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_WARN,"data=NULL\n");
        ret = -EINVAL;
        goto exit;
    }

#ifdef CONFIG_COMPAT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0))
    if (in_compat_syscall())
#else
    if (is_compat_task())
#endif
    {
        compat_android_wifi_priv_cmd compat_priv_cmd;
        if (copy_from_user(&compat_priv_cmd, data, sizeof(compat_android_wifi_priv_cmd))) {
            ret = -EFAULT;
            goto exit;
        }

        priv_cmd.buf = compat_ptr(compat_priv_cmd.buf);
        priv_cmd.used_len = compat_priv_cmd.used_len;
        priv_cmd.total_len = compat_priv_cmd.total_len;
    } else
#endif /* CONFIG_COMPAT */
    {
        if (copy_from_user(&priv_cmd, data, sizeof(struct android_wifi_priv_cmd))) {
            AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_ERROR,"copy_from_user data fail\n");
            ret = -EFAULT;
            goto exit;
        }
    }

    AML_PRINT(AML_LOG_ID_LOG,AML_LOG_LEVEL_INFO, "cmd_len %d %zd\n", priv_cmd.total_len, sizeof(struct android_wifi_priv_cmd));
    if ((priv_cmd.total_len > PRIVATE_COMMAND_MAX_LEN) || (priv_cmd.total_len < 0)) {
        AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_WARN,"cmd length error:%d\n", priv_cmd.total_len);
        ret = -EINVAL;
        goto exit;
    }

    command = ZMALLOC(priv_cmd.total_len, "command", GFP_ATOMIC);
    if (!command) {
        AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_ERROR,"failed to allocate memory\n");
        ret = -ENOMEM;
        goto exit;
    }

    if (copy_from_user(command, priv_cmd.buf, priv_cmd.total_len)) {
        AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_ERROR,"copy_from_user priv_cmd.buf fail\n");
        ret = -EFAULT;
        goto exit;
    }

    //AML_PRINT(AML_LOG_ID_LOG,AML_LOG_LEVEL_INFO, " Android private cmd \"%s\" on %s\n", command, ifr->ifr_name);

    cmd_num = aml_android_cmdstr_to_num(command);
    AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_DEBUG,"cmd_num=%d\n", cmd_num);
    if (cmd_num == ANDROID_WIFI_CMD_MAX) {
        while (cmd_to_func[i].name[0] != '\0') {
            if (strnicmp(command , cmd_to_func[i].name, strlen(cmd_to_func[i].name)) == 0) {
                is_found = 1;
                break;
            }
            i++;
        }
        if (is_found) {
            cmd_to_func[i].cmd_proc_func(wnet_vif, command, priv_cmd.total_len);
            goto exit;
        }
    }

    switch (cmd_num)
    {
        case ANDROID_WIFI_CMD_START:
            bytes_written = wl_android_wifi_on(wnet_vif->vm_ndev);
            goto response;

        case ANDROID_WIFI_CMD_SETFWPATH:
            goto response;

        case ANDROID_WIFI_CMD_STOP:
            bytes_written = wl_android_wifi_off(wnet_vif->vm_ndev);
            break;

        case ANDROID_WIFI_CMD_SCAN_ACTIVE:
            break;
        case ANDROID_WIFI_CMD_SCAN_PASSIVE:
            break;

        case ANDROID_WIFI_CMD_RSSI:
            bytes_written = aml_android_get_rssi(wnet_vif, command, priv_cmd.total_len);
            break;
        case ANDROID_WIFI_CMD_LINKSPEED:
            bytes_written = aml_android_get_link_speed(wnet_vif, command, priv_cmd.total_len);
            break;

        case ANDROID_WIFI_CMD_BLOCK:
            //bytes_written = aml_android_set_block(net, command, priv_cmd.total_len);
            break;

        case ANDROID_WIFI_CMD_RXFILTER_START:
            //bytes_written = net_os_set_packet_filter(net, 1);
            break;
        case ANDROID_WIFI_CMD_RXFILTER_STOP:
            //bytes_written = net_os_set_packet_filter(net, 0);
            break;
        case ANDROID_WIFI_CMD_RXFILTER_ADD:
            //int filter_num = *(command + strlen(CMD_RXFILTER_ADD) + 1) - '0';
            //bytes_written = net_os_rxfilter_add_remove(net, true, filter_num);
            break;
        case ANDROID_WIFI_CMD_RXFILTER_REMOVE:
            //int filter_num = *(command + strlen(CMD_RXFILTER_REMOVE) + 1) - '0';
            //bytes_written = net_os_rxfilter_add_remove(net, false, filter_num);
            break;

        case ANDROID_WIFI_CMD_BTCOEXSCAN_START:
            break;
        case ANDROID_WIFI_CMD_BTCOEXSCAN_STOP:
            break;
        case ANDROID_WIFI_CMD_BTCOEXMODE:
            break;

        case ANDROID_WIFI_CMD_SETSUSPENDOPT:
            //bytes_written = wl_android_set_suspendopt(net, command, priv_cmd.total_len);
            break;

        case ANDROID_WIFI_CMD_SETBAND:
            //uint band = *(command + strlen(CMD_SETBAND) + 1) - '0';
            //bytes_written = wldev_set_band(net, band);
            break;
        case ANDROID_WIFI_CMD_GETBAND:
            //bytes_written = wl_android_get_band(net, command, priv_cmd.total_len);
            break;

        case ANDROID_WIFI_CMD_COUNTRY:
            bytes_written = aml_android_set_country(wnet_vif, command, priv_cmd.total_len);
            break;

#ifdef PNO_SUPPORT
        case ANDROID_WIFI_CMD_PNOSSIDCLR_SET:
            //bytes_written = dhd_dev_pno_reset(net);
            break;
        case ANDROID_WIFI_CMD_PNOSETUP_SET:
            //bytes_written = wl_android_set_pno_setup(net, command, priv_cmd.total_len);
            break;
        case ANDROID_WIFI_CMD_PNOENABLE_SET:
            //uint pfn_enabled = *(command + strlen(CMD_PNOENABLE_SET) + 1) - '0';
            //bytes_written = dhd_dev_pno_enable(net, pfn_enabled);
            break;
#endif

#ifdef CONFIG_P2P
        case ANDROID_WIFI_CMD_P2P_SET_NOA:
            skip = strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_P2P_SET_NOA]) + 1;
            bytes_written = vm_p2p_set_p2p_noa(wnet_vif->vm_ndev, command + skip, priv_cmd.total_len - skip);
            break;
        case ANDROID_WIFI_CMD_P2P_GET_NOA:
            //bytes_written = wl_cfg80211_get_p2p_noa(net, command, priv_cmd.total_len);
            break;
        case ANDROID_WIFI_CMD_P2P_SET_PS:
            skip = strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_P2P_SET_PS]) + 1;
            bytes_written = vm_p2p_set_p2p_ps(wnet_vif->vm_ndev, command + skip, priv_cmd.total_len - skip);
            break;

        case ANDROID_WIFI_CMD_SET_AP_WPS_P2P_IE:
            skip = strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_SET_AP_WPS_P2P_IE]) + 3;
            bytes_written = vm_p2p_set_wpsp2pie(wnet_vif->vm_ndev, command + skip,
                priv_cmd.total_len - skip, *(command + skip - 2) - '0');
            AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_DEBUG,"bytes_written=%d\n", bytes_written);
            break;
#endif //CONFIG_P2P
#ifdef CONFIG_WFD
        case ANDROID_WIFI_CMD_WFD_ENABLE:
        {

            struct wifi_mac_wfd_info     *pwfd_info= &wnet_vif->vm_p2p->wfd_info;
            pwfd_info->wfd_enable = true;
            break;
        }

        case ANDROID_WIFI_CMD_WFD_DISABLE:
        {
            struct wifi_mac_wfd_info     *pwfd_info= &wnet_vif->vm_p2p->wfd_info;

            pwfd_info->wfd_enable = false;
            break;
        }
        case ANDROID_WIFI_CMD_WFD_SET_TCPPORT:
        {
            //  wpa_cli driver wfd-set-tcpport = 554
            struct wifi_mac_wfd_info *pwfd_info= &wnet_vif->vm_p2p->wfd_info;
            pwfd_info->rtsp_ctrlport = (unsigned short)get_num_from_cmd(priv_cmd.buf, priv_cmd.total_len);
            break;
        }
        case ANDROID_WIFI_CMD_WFD_SET_MAX_TPUT:
        {
            struct wifi_mac_wfd_info *pwfd_info= &wnet_vif->vm_p2p->wfd_info;
            pwfd_info->max_thruput = (unsigned short)get_num_from_cmd(priv_cmd.buf, priv_cmd.total_len);

            break;
        }
        case ANDROID_WIFI_CMD_WFD_SET_DEVTYPE:
        {

            struct wifi_mac_wfd_info *pwfd_info= &wnet_vif->vm_p2p->wfd_info;
            pwfd_info->wfd_device_type = (unsigned char)get_num_from_cmd(priv_cmd.buf, priv_cmd.total_len);

            if ( ( WFD_DEVINFO_SOURCE != pwfd_info->wfd_device_type )
                 && ( WFD_DEVINFO_PSINK != pwfd_info->wfd_device_type ) )
            {
                pwfd_info->wfd_device_type = WFD_DEVINFO_PSINK;
            }

            break;
        }
#endif

        case ANDROID_WIFI_CMD_FW_REPAIR:
        {
#if 0
            struct wifi_mac *wifimac = wnet_vif->vm_wmac;

            WIFINET_FW_STAT_LOCK(wifimac);
            if ((wifimac->wm_recovery_flags & WIFINET_RECOVERY_F_RUNNING) != 0) {
                WIFINET_FW_STAT_UNLOCK(wifimac);
                break;
            }
            wifimac->wm_recovery_flags |= WIFINET_RECOVERY_F_RUNNING;
            WIFINET_FW_STAT_UNLOCK(wifimac);

            wifimac->drv_priv->drv_ops.fw_repair(wifimac->drv_priv);
            wifimac->wm_recovery_flags &= ~WIFINET_RECOVERY_F_RUNNING;
#endif
            break;
        }

        default:
            AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_WARN,"Unknown PRIVATE command %s - ignored\n", command);
            snprintf(command, 3, "OK");
            bytes_written = strlen("OK");
    }

response:
    if (bytes_written >= 0)
    {
        if ((bytes_written == 0) && (priv_cmd.total_len > 0))
            command[0] = '\0';

        if (bytes_written >= priv_cmd.total_len)
        {
            AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_DEBUG,"bytes_written = %d\n", bytes_written);
            bytes_written = priv_cmd.total_len;
        }
        else
        {
            bytes_written++;
        }
        priv_cmd.used_len = bytes_written;
        if (copy_to_user(priv_cmd.buf, command, bytes_written))
        {
            AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_ERROR,"failed to copy data to user buffer\n");
            ret = -EFAULT;
        }
    }
    else
    {
        ret = bytes_written;
    }

exit:
    if (command)
    {
        FREE(command,"command");
    }

    AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_DEBUG,"ret=%d\n",  ret);
    return ret;
}

