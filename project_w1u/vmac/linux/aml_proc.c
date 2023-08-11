#include <linux/netdevice.h>
#include "wifi_mac_if.h"
#include "wifi_debug.h"
#include <linux/proc_fs.h>
#include <linux/uaccess.h>


static struct proc_dir_entry *g_proc;
#define AML_PARENT_NAME "wlan"
#define AML_CFG_NAME "cfg"
#define AML_DRIVER_NAME "driver"
#define AML_COUNTRY_NAME "country"
#define AML_DBGLEVEL_NAME "dbglevel"
#define AML_DISCONNECT_NAME "disconnect_info"
#define DRV_DFS_SWITCH "bypassdfs"
#define DRV_PRINT_OFFT 23
#define CFG_BWINFO "Sta5gBw"
#define CFG_TXLIMIT "TxRetryLimit"
#define LEN_DRV_BUFF 1500
#define LEN_DRV_DFSINFO_BUFF 150
#define LEN_DRV_MAX_CHANINFO 32
#define LEN_DRV_MAX_DFSINFO 5
#define LEN_DRV_BYPASSDFS 12
#define MAX_BUF_LENGTH 100
#define CFG_BMFMINFO_1 "StaHTBfee"
#define CFG_BMFMINFO_2 "StaVHTBfee"
#define CFG_BMFMINFO_3 "StaVHTMuBfee"
#define CFG_SPATIAL_NUM 1

static unsigned char g_aucprocbuf[MAX_BUF_LENGTH];
bool g_DFS_on = false;
extern struct wlan_net_vif *g_wnet_vif0;

int32_t aml_proc_init(void)
{
    if (init_net.proc_net == (struct proc_dir_entry *)NULL) {
        ERROR_DEBUG_OUT("init proc fail: proc_net == NULL\n");
        return -ENOENT;
    }

    g_proc = proc_mkdir(AML_PARENT_NAME, init_net.proc_net);
    if (!g_proc) {
        ERROR_DEBUG_OUT("g_proc == NULL\n");
    }
    return 0;
}

void aml_proc_deinit(void)
{
    if (g_proc) {
        proc_remove(g_proc);
    }

    return;
}

#if defined(SU_BF) || defined(MU_BF)
extern int aml_set_beamforming(struct wlan_net_vif *wnet_vif, unsigned int set1,unsigned int set2);
#endif
static ssize_t cfgRead(struct file *filp, char __user *buf, size_t count,
    loff_t *f_pos)
{
    unsigned int len;
    unsigned char *out;
    struct wlan_net_vif *wnet_vif = NULL;
    struct wifi_mac *wifimac;

    if (*f_pos > 0)
        return 0;

    wnet_vif = g_wnet_vif0;
    wifimac = wifi_mac_get_mac_handle();

    memset(g_aucprocbuf, 0, MAX_BUF_LENGTH);
    out = g_aucprocbuf;
    out += sprintf(out, "StaHTBfee|0\n");
    out += sprintf(out, "StaVHTBfee|%d\n", ((wifimac->wm_flags_ext2 & WIFINET_VHTCAP_SU_BFMEE) == 0 ? 0: 1));
    out += sprintf(out, "StaVHTMuBfee|%d\n", ((wifimac->wm_flags_ext2 & WIFINET_VHTCAP_MU_BFMEE) == 0 ? 0: 1));
    out += sprintf(out, "Sta5gBw|%d\n", wnet_vif->vm_bandwidth);
    out += sprintf(out, "TxRetryLimit|%d\n", wifimac->drv_priv->drv_config.cfg_retrynum);
    len = strlen(g_aucprocbuf);

    if (copy_to_user(buf, g_aucprocbuf, len)) {
        ERROR_DEBUG_OUT("copy to user failed\n");
        return -EFAULT;
    }
    *f_pos += len;

    return len;
}

static ssize_t cfgWrite(struct file *filp, char __user *buf, size_t count,
    loff_t *f_pos)
{
    unsigned int len = 0, retrynum = 0, i;
    struct wlan_net_vif *wnet_vif = NULL;
    struct wifi_mac *wifimac;
#if defined(SU_BF) || defined(MU_BF)
    static unsigned int bmfm_enable = 3;
#endif
    unsigned char *retrydata;

    wnet_vif = g_wnet_vif0;
    wifimac = wifi_mac_get_mac_handle();

    memset(g_aucprocbuf, 0, MAX_BUF_LENGTH);
    len = (count < MAX_BUF_LENGTH) ? count : (MAX_BUF_LENGTH - 1);

    if (copy_from_user(g_aucprocbuf , buf, len)) {
        printk("copy to user failed\n");
        ERROR_DEBUG_OUT("copy to user failed\n");
        return -EFAULT;
    }
    g_aucprocbuf[len - 1] = '\0';

    if (strncmp(g_aucprocbuf, CFG_BMFMINFO_1, strlen(CFG_BMFMINFO_1)) == 0) {
        return count;
    }

#if defined(SU_BF) || defined(MU_BF)
    if (strncmp(g_aucprocbuf, CFG_BMFMINFO_2, strlen(CFG_BMFMINFO_2)) == 0) {
        if (g_aucprocbuf[len - 2] == '1')
            bmfm_enable |= BIT(0);
        else
            bmfm_enable &= ~ BIT(0);
        AML_OUTPUT("kernel change the %s to %c\n", CFG_BMFMINFO_2, g_aucprocbuf[len - 2]);
    }
    if (strncmp(g_aucprocbuf, CFG_BMFMINFO_3, strlen(CFG_BMFMINFO_3)) == 0) {
        if (g_aucprocbuf[len - 2] == '1')
            bmfm_enable |= BIT(1);
        else
            bmfm_enable &= ~ BIT(1);
        AML_OUTPUT("kernel change the %s to %c\n", CFG_BMFMINFO_3, g_aucprocbuf[len - 2]);
    }
    aml_set_beamforming(wnet_vif, bmfm_enable, CFG_SPATIAL_NUM);
#endif
    if (strncmp(g_aucprocbuf, CFG_BWINFO, strlen(CFG_BWINFO)) == 0) {
        if (g_aucprocbuf[len - 2] == '0') {
            wnet_vif->vm_bandwidth = WIFINET_BWC_WIDTH20;
        }
        else if (g_aucprocbuf[len - 2] == '1') {
            wnet_vif->vm_bandwidth = WIFINET_BWC_WIDTH40;
        }
        else if (g_aucprocbuf[len - 2] == '2') {
            wnet_vif->vm_bandwidth = WIFINET_BWC_WIDTH80;
        }
        AML_OUTPUT("kernel change the %s to %c\n", CFG_BWINFO, g_aucprocbuf[len - 2]);
        return count;
    }
    else if (strncmp(g_aucprocbuf, CFG_TXLIMIT, strlen(CFG_TXLIMIT)) == 0) {
        retrydata = g_aucprocbuf + strlen(CFG_TXLIMIT) + 1;
        for (i = 0; i < strlen(retrydata); i++) {
            retrynum = retrynum * 10 + (retrydata[i] - '0');
        }
        wifimac->drv_priv->drv_config.cfg_retrynum = retrynum;
        AML_OUTPUT("kernel change the %s to %d\n", CFG_TXLIMIT, retrynum);
    }
    return count;
}


static const struct file_operations fwcfg_ops = {
    .owner = THIS_MODULE,
    .read = cfgRead,
    .write = cfgWrite,
};


unsigned char drv_version[50] = {"v1.0.1_20210327-b"};
static ssize_t driverRead(struct file *filp, char __user *buf, size_t count,
    loff_t *f_pos)
{
    unsigned int len;
    struct wlan_net_vif *wnet_vif = NULL;
    unsigned char *out, *out_dfs;
    struct wifi_mac *wifimac;
    unsigned int i;
    unsigned char channel, pre_channel, bw, pre_bw;
    unsigned char tmp_buf[LEN_DRV_BUFF] = {0}, dfs_channel[LEN_DRV_DFSINFO_BUFF] = {0};

    wnet_vif = g_wnet_vif0;
    wifimac = wifi_mac_get_mac_handle();
    pre_channel = 0;
    pre_bw = 0;

    if (*f_pos > 0)
        return 0;

    out = tmp_buf;
    out_dfs = dfs_channel;
    out += sprintf(out, "drv_version:%s\n", drv_version);
    out += sprintf(out, "country:%s\n", wifimac->wm_country.iso);
    for (i = 0; i < wifimac->wm_nchans; i++) {
        if ((LEN_DRV_BUFF - strlen(tmp_buf)) < LEN_DRV_MAX_CHANINFO) {
            AML_OUTPUT("Buffer tmp_buf[] is not enough! Channel info stop to print.\n");
            break;
        }
        channel = wifimac->wm_channels[i].chan_pri_num;
        bw = (wifimac->wm_channels[i].chan_bw == 0) ? 20 : (wifimac->wm_channels[i].chan_bw == 1) ? 40 : 80;
        if (g_DFS_on || !(wifimac->wm_channels[i].chan_flags & WIFINET_CHAN_DFS)) {
            if (channel != pre_channel)
                out += sprintf(out, "CH-%d:\tBW_%dMHz\t(flag=0x%x)\n", channel, bw, wifimac->wm_channels[i].chan_flags);
            if (channel == pre_channel && bw > pre_bw)
                sprintf(out - DRV_PRINT_OFFT, "BW_%dMHz\t(flag=0x%x)\n", bw, wifimac->wm_channels[i].chan_flags);
            if ((wifimac->wm_channels[i].chan_flags & WIFINET_CHAN_DFS) && channel != pre_channel) {
                if (strlen(dfs_channel) == 0) {
                    out_dfs += sprintf(out_dfs, "DFS channel:%d", channel);
                }
                else {
                    if ((LEN_DRV_DFSINFO_BUFF - strlen(dfs_channel)) < LEN_DRV_MAX_DFSINFO) {
                        AML_OUTPUT("Buffer dfs_channel[] is not enough! DFS channel info stop to print.\n");
                        break;
                    }
                    out_dfs += sprintf(out_dfs, ",%d", channel);
                }
            }
        }
        pre_channel = channel;
        pre_bw = bw;
    }
    if ((LEN_DRV_DFSINFO_BUFF - strlen(tmp_buf)) >= LEN_DRV_BYPASSDFS + strlen(dfs_channel)) {
        out += sprintf(out, "bypassdfs:%d\n", g_DFS_on);
        if (strlen(dfs_channel) != 0) {
            out_dfs += sprintf(out_dfs, "\n");
            out += sprintf(out, "%s\n", dfs_channel);
        }
    }
    else {
        AML_OUTPUT("Buffer tmp_buf[] is not enough! bypassdfs & DFS channel info stop to print.\n");
    }
    len = strlen(tmp_buf);

    if (copy_to_user(buf, tmp_buf, len)) {
        ERROR_DEBUG_OUT("copy to user failed\n");
        return -EFAULT;
    }

    *f_pos += len;

    return len;
}

static ssize_t driverWrite(struct file *file, const char __user *buffer,
    size_t count, loff_t *data)
{
    unsigned int len = 0;

    memset(g_aucprocbuf, 0, MAX_BUF_LENGTH);
    len = (count < MAX_BUF_LENGTH) ? count : (MAX_BUF_LENGTH - 1);

    if (copy_from_user(g_aucprocbuf , buffer, len)) {
        printk("copy to user failed\n");
        ERROR_DEBUG_OUT("copy to user failed\n");
        return -EFAULT;
    }
    g_aucprocbuf[len - 1] = '\0';
    //memcpy(drv_version, g_aucprocbuf, len);

    if (strncmp(g_aucprocbuf, DRV_DFS_SWITCH, strlen(DRV_DFS_SWITCH)) == 0) {
        if (g_aucprocbuf[len - 2] == '1') {
            g_DFS_on = true;
        }
        else if (g_aucprocbuf[len - 2] == '0') {
            g_DFS_on = false;
        }
        else {
            AML_OUTPUT("bypassdfs info input error!\n");
        }
    }

    return count;
}


static const struct file_operations fwdriver_ops = {
    .owner = THIS_MODULE,
    .read = driverRead,
    .write = driverWrite,
};


extern unsigned long long g_dbg_modules;
static ssize_t dbglevelRead(struct file *filp, char __user *buf, size_t count,
    loff_t *f_pos)
{
    unsigned int len;

    if (*f_pos > 0)
        return 0;

    memset(g_aucprocbuf, 0, MAX_BUF_LENGTH);
    snprintf(g_aucprocbuf, MAX_BUF_LENGTH, "deglevel %x\n", g_dbg_modules);
    len = strlen(g_aucprocbuf);

    if (copy_to_user(buf, g_aucprocbuf, len)) {
        ERROR_DEBUG_OUT("copy to user failed\n");
        return -EFAULT;
    }
    *f_pos += len;

    return len;
}

extern int aml_set_debug_modules(char *debug_str);
static ssize_t dbglevelWrite(struct file *file, const char __user *buffer,
    size_t count, loff_t *data)
{
    unsigned int len = 0;

    memset(g_aucprocbuf, 0, MAX_BUF_LENGTH);
    len = (count < MAX_BUF_LENGTH) ? count : (MAX_BUF_LENGTH - 1);

    if (copy_from_user(g_aucprocbuf , buffer, len)) {
        ERROR_DEBUG_OUT("copy to user failed\n");
        return -EFAULT;
    }
    g_aucprocbuf[len] = '\0';
    aml_set_debug_modules(g_aucprocbuf);

    return count;
}


static const struct file_operations fwdbglevel_ops = {
    .owner = THIS_MODULE,
    .read = dbglevelRead,
    .write = dbglevelWrite,
};


static ssize_t countryRead(struct file *filp, char __user *buf, size_t count,
    loff_t *f_pos)
{
    unsigned int len = 0;
    unsigned char country[3] = {0};
    struct drv_private *drv_priv = drv_get_drv_priv();
    if (*f_pos > 0)
        return 0;

    memset(g_aucprocbuf, 0, MAX_BUF_LENGTH);
    drv_priv->drv_ops.get_current_country(drv_priv, country);
    snprintf(g_aucprocbuf, MAX_BUF_LENGTH, "country %s\n", country);
    len = strlen(g_aucprocbuf);

    if (copy_to_user(buf, g_aucprocbuf, len)) {
        ERROR_DEBUG_OUT("copy to user failed\n");
        return -EFAULT;
    }
    *f_pos += len;

    return len;
}


extern void wifi_mac_set_country_code(char* arg);
static ssize_t countryWrite(struct file *file, const char __user *buffer,
    size_t count, loff_t *data)
{
    unsigned int len = 0;

    memset(g_aucprocbuf, 0, MAX_BUF_LENGTH);
    len = (count < MAX_BUF_LENGTH) ? count : (MAX_BUF_LENGTH - 1);

    if (copy_from_user(g_aucprocbuf , buffer, len)) {
        ERROR_DEBUG_OUT("copy to user failed\n");
        return -EFAULT;
    }
    g_aucprocbuf[len - 1] = '\0';

    wifi_mac_set_country_code(g_aucprocbuf);

    return count;
}


static const struct file_operations fwcountry_ops = {
    .owner = THIS_MODULE,
    .read = countryRead,
    .write = countryWrite,
};

static ssize_t disconnectRead(struct file *filp, char __user *buf, size_t count,
    loff_t *f_pos)
{
    unsigned int len;
    unsigned char *out;
    struct wifi_mac *wifimac;

    if (*f_pos > 0)
        return 0;

    wifimac = wifi_mac_get_mac_handle();

    memset(g_aucprocbuf, 0, MAX_BUF_LENGTH);
    out = g_aucprocbuf;
    out += sprintf(out, "Disconnect reason code:%d\n", wifimac->wm_disconnect_code);
    len = strlen(g_aucprocbuf);

    if (copy_to_user(buf, g_aucprocbuf, len)) {
        ERROR_DEBUG_OUT("copy to user failed\n");
        return -EFAULT;
    }
    *f_pos += len;

    return len;
}

static const struct file_operations fwdisconnect_ops = {
    .owner = THIS_MODULE,
    .read = disconnectRead,
};


int32_t RemoveProcEntry(void)
{
    remove_proc_entry(AML_CFG_NAME, g_proc);
    remove_proc_entry(AML_DRIVER_NAME, g_proc);
    remove_proc_entry(AML_COUNTRY_NAME, g_proc);
    remove_proc_entry(AML_DBGLEVEL_NAME, g_proc);
    remove_proc_entry(AML_DISCONNECT_NAME, g_proc);
    return 0;
}


int32_t CreateProcEntry(void)
{
    struct proc_dir_entry *aml_proc;
    struct wifi_mac *wifimac = wifi_mac_get_mac_handle();
    struct wlan_net_vif *wnet_vif = NULL;

    wnet_vif = wifi_mac_get_wnet_vif_by_vid(wifimac, 0);
    wnet_vif->vm_proc = g_proc;

    aml_proc = proc_create(AML_CFG_NAME, 0664, g_proc, &fwcfg_ops);
    if (aml_proc == NULL) {
        ERROR_DEBUG_OUT("Unable to create /proc entry cfg\n\r");
        return -1;
    }

    aml_proc = proc_create(AML_DRIVER_NAME, 0664, g_proc, &fwdriver_ops);
    if (aml_proc == NULL) {
        ERROR_DEBUG_OUT("Unable to create /proc entry driver\n\r");
        return -1;
    }

    aml_proc = proc_create(AML_COUNTRY_NAME, 0664, g_proc, &fwcountry_ops);
    if (aml_proc == NULL) {
        ERROR_DEBUG_OUT("Unable to create /proc entry country\n\r");
        return -1;
    }

    aml_proc = proc_create(AML_DBGLEVEL_NAME, 0664, g_proc, &fwdbglevel_ops);
    if (aml_proc == NULL) {
        ERROR_DEBUG_OUT("Unable to create /proc entry dbglevel\n\r");
        return -1;
    }

    aml_proc = proc_create(AML_DISCONNECT_NAME, 0664, g_proc, &fwdisconnect_ops);
    if (aml_proc == NULL) {
        ERROR_DEBUG_OUT("Unable to create /proc entry disconnect_info\n\r");
        return -1;
    }
    return 0;
}

