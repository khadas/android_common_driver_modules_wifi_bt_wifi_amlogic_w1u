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
#define AML_DRVSTATE_NAME "drv_state"
#define AML_RVRINFO_NAME "rvr_info"
#define AML_SCANPARAM_NAME "scan_param"
#define AML_WOW_WAKE_REASON "wow_reason"
#define DRV_PRINT_OFFT 23
#define CFG_BWINFO "Sta5gBw"
#define CFG_TXLIMIT "TxRetryLimit"
#define LEN_DRV_BUFF 1500
#define LEN_DRV_DFSINFO_BUFF 150
#define LEN_DRV_MAX_CHANINFO 32
#define LEN_DRV_MAX_DFSINFO 5
#define LEN_DRV_BYPASSDFS 12
#define MAX_BUF_LENGTH 200
#define CFG_BMFMINFO_1 "StaHTBfee"
#define CFG_BMFMINFO_2 "StaVHTBfee"
#define CFG_BMFMINFO_3 "StaVHTMuBfee"
#define CFG_SPATIAL_NUM 1
#define RX_AMPDU_ACCEPT 255
#define RX_AMPDU_SIZE 255
#define BACKOP_MS 100
#define SCAN_CNT_MAX 1
#define SCAN_NUM_EACH_CH 255
#define PROBE_NUM_EACH_SCAN 2
#define PROBE_SSID_NUM_WACH_SCAN 2

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
#define aml_proc_ops proc_ops
#else
#define aml_proc_ops file_operations
#endif


#define AML_PROC_HDL_TYPE_SEQ	0
#define AML_PROC_HDL_TYPE_SSEQ	1
#define AML_PROC_HDL_TYPE_SZSEQ	2

struct aml_proc_hdl {
    char *name;
    u8 type;
    union {
        int (*show)(struct seq_file *, void *);
        struct seq_operations *seq_op;
        struct {
            int (*show)(struct seq_file *, void *);
            size_t size;
        } sz;
    } u;
    ssize_t (*write)(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
};

#define AML_PROC_HDL_SSEQ(_name, _show, _write) \
    { .name = _name, .type = AML_PROC_HDL_TYPE_SSEQ, .u.show = _show, .write = _write}

static int proc_get_dummy(struct seq_file *m, void *v)
{
    return 0;
}

static unsigned char g_aucprocbuf[MAX_BUF_LENGTH];
bool g_DFS_on = true;
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
    unsigned short bmfm_enable = 0;
    unsigned short status_vht_bfee = 0;
    unsigned short status_vht_mubfee = 0;
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
    status_vht_bfee = !((wifimac->wm_flags_ext2 & WIFINET_VHTCAP_SU_BFMEE) == 0);
    status_vht_mubfee = !((wifimac->wm_flags_ext2 & WIFINET_VHTCAP_MU_BFMEE) == 0);
    bmfm_enable = status_vht_bfee | (status_vht_mubfee << 1);
    if (strncmp(g_aucprocbuf, CFG_BMFMINFO_2, strlen(CFG_BMFMINFO_2)) == 0) {
        if (g_aucprocbuf[len - 2] == '1')
            bmfm_enable |= BIT(0);
        else
            bmfm_enable &= ~ BIT(0);
        AML_OUTPUT("kernel change the %s to %c\n", CFG_BMFMINFO_2, g_aucprocbuf[len - 2]);
        aml_set_beamforming(wnet_vif, bmfm_enable, CFG_SPATIAL_NUM);
    }
    if (strncmp(g_aucprocbuf, CFG_BMFMINFO_3, strlen(CFG_BMFMINFO_3)) == 0) {
        if (g_aucprocbuf[len - 2] == '1')
            bmfm_enable |= BIT(1);
        else
            bmfm_enable &= ~ BIT(1);
        AML_OUTPUT("kernel change the %s to %c\n", CFG_BMFMINFO_3, g_aucprocbuf[len - 2]);
        aml_set_beamforming(wnet_vif, bmfm_enable, CFG_SPATIAL_NUM);
    }
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

    DPRINTF(AML_DEBUG_WARNING, "<%s> set country <%s> by file_node\n", __func__, g_aucprocbuf);
    wifi_mac_set_country_code(g_aucprocbuf);

    return count;
}

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

extern unsigned char aml_insmod_flag;
static ssize_t drvstateRead(struct file *filp, char __user *buf, size_t count,
    loff_t *f_pos)
{
    unsigned int len = 0;
#ifdef CHIP_RESET_SUPPORT
    struct wifi_mac * wifimac = wifi_mac_get_mac_handle();
    unsigned char upper_recovery_request = wifimac->request_upper_recovery;
#else
    unsigned char upper_recovery_request = 0;
#endif

    if (*f_pos > 0)
        return 0;

    memset(g_aucprocbuf, 0, MAX_BUF_LENGTH);
    if (!aml_insmod_flag) {
        snprintf(g_aucprocbuf, MAX_BUF_LENGTH, "not_ready\n");
    } else if (!upper_recovery_request) {
        snprintf(g_aucprocbuf, MAX_BUF_LENGTH, "ready\n");
    } else {
        snprintf(g_aucprocbuf, MAX_BUF_LENGTH, "recovery\n");
    }
    len = strlen(g_aucprocbuf);

    if (copy_to_user(buf, g_aucprocbuf, len)) {
        ERROR_DEBUG_OUT("copy to user failed\n");
        return -EFAULT;
    }
    *f_pos += len;

    return len;
}

void get_rate_name(unsigned char rate, unsigned char *name) {
    unsigned char *out = name;

    if (IS_HT_RATE(rate))
        out += sprintf(out, "11N_MSC%d", GET_HT_MCS(rate));
    else if (IS_VHT_RATE((rate)))
        out += sprintf(out, "11AC_MSC%d", GET_HT_MCS(rate));
    else {
        switch (rate) {
            case WIFI_11B_1M:
                out += sprintf(out, "11B_1M");
                break;
            case WIFI_11B_2M:
                out += sprintf(out, "11B_2M");
                break;
            case WIFI_11B_5M:
                out += sprintf(out, "11B_5M");
                break;
            case WIFI_11B_11M:
                out += sprintf(out, "11B_11M");
                break;
            case WIFI_11G_6M:
                out += sprintf(out, "11G_6M");
                break;
            case WIFI_11G_9M:
                out += sprintf(out, "11G_9M");
                break;
            case WIFI_11G_12M:
                out += sprintf(out, "11G_12M");
                break;
            case WIFI_11G_18M:
                out += sprintf(out, "11G_18M");
                break;
            case WIFI_11G_24M:
                out += sprintf(out, "11G_24M");
                break;
            case WIFI_11G_36M:
                out += sprintf(out, "11G_36M");
                break;
            case WIFI_11G_48M:
                out += sprintf(out, "11G_48M");
                break;
            case WIFI_11G_54M:
                out += sprintf(out, "11G_54M");
                break;
        }
    }
    out += sprintf(out, "\0");
}

extern void get_phy_stc_info(unsigned int *arr);
static ssize_t rvrinfoRead(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    unsigned int len = 0;
    struct wlan_net_vif *wnet_vif = g_wnet_vif0;
    struct drv_private *drv_priv = drv_get_drv_priv();
    unsigned char *out;
    unsigned int arr[8] = {0};
    unsigned char bw, rate_name[10];

    memset(g_aucprocbuf, 0, MAX_BUF_LENGTH);
    out = g_aucprocbuf;

    if (*f_pos > 0)
        return 0;

    if (wnet_vif->vm_state == WIFINET_S_CONNECTED) {
        get_phy_stc_info(arr);
        bw = wnet_vif->vm_wmac->wm_curchan->chan_bw;

        out += sprintf(out, "avg_rssi:%d\navg_bcn_rssi:%d\navg_snr:%d\n", wnet_vif->vm_mainsta->sta_avg_rssi - 256, wnet_vif->vm_mainsta->sta_avg_bcn_rssi, arr[1]);
        out += sprintf(out, "snr_qdb:%d\nnoise_f:%d\n", arr[5], arr[4]);
        get_rate_name(wnet_vif->vm_mainsta->sta_vendor_rate_code, &rate_name);
        out += sprintf(out, "txRate:%s\n", rate_name);
        get_rate_name(drv_priv->drv_currratetable->info[wnet_vif->vm_mainsta->sta_rxrate_index].vendor_rate_code, &rate_name);
        out += sprintf(out, "rxRate:%s\n", rate_name);
        out += sprintf(out, "BW:%dMHz\n", (bw == 0) ? 20 : (bw == 1) ? 40 : 80);
    }
    len = strlen(g_aucprocbuf);

    if (copy_to_user(buf, g_aucprocbuf, len)) {
        ERROR_DEBUG_OUT("copy to user failed\n");
        return -EFAULT;
    }
    *f_pos += len;

    return len;
}

static ssize_t scanparamRead(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    unsigned int len = 0;
    unsigned char *out;
    struct wifi_mac *wifimac;

    if (*f_pos > 0)
        return 0;

    wifimac = wifi_mac_get_mac_handle();

    memset(g_aucprocbuf, 0, MAX_BUF_LENGTH);
    out = g_aucprocbuf;
    out += sprintf(out, "scan_ch_ms\t\t\t%d\n", wifimac->wm_scan->scan_chan_wait);
    out += sprintf(out, "dfs_scan_ch_ms\t\t\t%d\n", WIFINET_SCAN_DEFAULT_INTERVAL);
    out += sprintf(out, "rx_ampdu_accept\t\t\t%d\n", RX_AMPDU_ACCEPT);
    out += sprintf(out, "rx_ampdu_size\t\t\t%d\n", RX_AMPDU_SIZE);
    out += sprintf(out, "backop_ms\t\t\t%d\n", BACKOP_MS);
    out += sprintf(out, "scan_cnt_max\t\t\t%d\n", SCAN_CNT_MAX);
    out += sprintf(out, "scan_num_each_ch\t\t%d\n", SCAN_NUM_EACH_CH);
    out += sprintf(out, "probe_num_each_scan\t\t%d\n", PROBE_NUM_EACH_SCAN);
    out += sprintf(out, "probe_ssid_num_each_scan\t%d\n", PROBE_SSID_NUM_WACH_SCAN);
    len = strlen(g_aucprocbuf);

    if (copy_to_user(buf, g_aucprocbuf, len)) {
        ERROR_DEBUG_OUT("copy to user failed\n");
        return -EFAULT;
    }
    *f_pos += len;

    return len;

}

const struct aml_proc_hdl drv_proc_hdls[] = {
    AML_PROC_HDL_SSEQ(AML_DRIVER_NAME, driverRead, NULL),
    AML_PROC_HDL_SSEQ(AML_CFG_NAME, cfgRead, NULL),
    AML_PROC_HDL_SSEQ(AML_COUNTRY_NAME, countryRead, NULL),
    AML_PROC_HDL_SSEQ(AML_DBGLEVEL_NAME, dbglevelRead, NULL),
    AML_PROC_HDL_SSEQ(AML_DISCONNECT_NAME, disconnectRead, NULL),
    AML_PROC_HDL_SSEQ(AML_DRVSTATE_NAME, drvstateRead, NULL),
    AML_PROC_HDL_SSEQ(AML_RVRINFO_NAME, rvrinfoRead, NULL),
    AML_PROC_HDL_SSEQ(AML_SCANPARAM_NAME, scanparamRead, NULL),
};

static int aml_drv_proc_open(struct inode *inode, struct file *file)
{
    /* struct net_device *dev = proc_get_parent_data(inode); */
    ssize_t index = (ssize_t)PDE_DATA(inode);
    const struct aml_proc_hdl *hdl = drv_proc_hdls + index;
    void *private = NULL;

    if (hdl->type == AML_PROC_HDL_TYPE_SEQ) {
        int res = seq_open(file, hdl->u.seq_op);

        if (res == 0)
            ((struct seq_file *)file->private_data)->private = private;

        return res;
    } else if (hdl->type == AML_PROC_HDL_TYPE_SSEQ) {
        int (*show)(struct seq_file *, void *) = hdl->u.show ? hdl->u.show : proc_get_dummy;

        return single_open(file, show, private);
    } else if (hdl->type == AML_PROC_HDL_TYPE_SZSEQ) {
        int (*show)(struct seq_file *, void *) = hdl->u.sz.show ? hdl->u.sz.show : proc_get_dummy;
        #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
        return single_open_size(file, show, private, hdl->u.sz.size);
        #else
        return single_open(file, show, private);
        #endif
    } else {
        return -EROFS;
    }
}

static const struct aml_proc_ops fwcfg_ops = {

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
        .proc_open = aml_drv_proc_open,
        .proc_read = cfgRead,
        .proc_lseek = seq_lseek,
        .proc_release = seq_release,
        .proc_write = cfgWrite,
#else
        .owner = THIS_MODULE,
        .open = aml_drv_proc_open,
        .read = cfgRead,
        .write = cfgWrite,
#endif
};

static const struct aml_proc_ops fwdriver_ops = {

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
        .proc_open = aml_drv_proc_open,
        .proc_read = driverRead,
        .proc_lseek = seq_lseek,
        .proc_release = seq_release,
        .proc_write = driverWrite,
#else
        .owner = THIS_MODULE,
        .open = aml_drv_proc_open,
        .read = driverRead,
        .write = driverWrite,
#endif
};

static const struct aml_proc_ops fwdbglevel_ops = {

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
        .proc_open = aml_drv_proc_open,
        .proc_read = dbglevelRead,
        .proc_lseek = seq_lseek,
        .proc_release = seq_release,
        .proc_write = dbglevelWrite,
#else
        .owner = THIS_MODULE,
        .open = aml_drv_proc_open,
        .read = dbglevelRead,
        .write = dbglevelWrite,
#endif
};

static const struct aml_proc_ops fwcountry_ops = {

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
        .proc_open = aml_drv_proc_open,
        .proc_read = countryRead,
        .proc_lseek = seq_lseek,
        .proc_release = seq_release,
        .proc_write = countryWrite,
#else
        .owner = THIS_MODULE,
        .open = aml_drv_proc_open,
        .read = countryRead,
        .write = countryWrite,
#endif
};

static const struct aml_proc_ops fwdisconnect_ops = {

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
        .proc_open = aml_drv_proc_open,
        .proc_read = disconnectRead,
        .proc_lseek = seq_lseek,
        .proc_release = seq_release,
#else
        .owner = THIS_MODULE,
        .open = aml_drv_proc_open,
        .read = disconnectRead,
#endif
};

static const struct aml_proc_ops drvstate_ops = {

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
        .proc_open = aml_drv_proc_open,
        .proc_read = drvstateRead,
        .proc_lseek = seq_lseek,
        .proc_release = seq_release,
#else
        .owner = THIS_MODULE,
        .open = aml_drv_proc_open,
        .read = drvstateRead,
#endif
};

static const struct aml_proc_ops rvrinfo_ops = {

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
        .proc_open = aml_drv_proc_open,
        .proc_read = rvrinfoRead,
        .proc_lseek = seq_lseek,
        .proc_release = seq_release,
#else
        .owner = THIS_MODULE,
        .open = aml_drv_proc_open,
        .read = rvrinfoRead,
#endif
};

static const struct aml_proc_ops scanparam_ops = {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
            .proc_open = aml_drv_proc_open,
            .proc_read = scanparamRead,
            .proc_lseek = seq_lseek,
            .proc_release = seq_release,
#else
            .owner = THIS_MODULE,
            .open = aml_drv_proc_open,
            .read = scanparamRead,
#endif
};

int32_t RemoveProcEntry(void)
{
    remove_proc_entry(AML_CFG_NAME, g_proc);
    remove_proc_entry(AML_DRIVER_NAME, g_proc);
    remove_proc_entry(AML_COUNTRY_NAME, g_proc);
    remove_proc_entry(AML_DBGLEVEL_NAME, g_proc);
    remove_proc_entry(AML_DISCONNECT_NAME, g_proc);
    remove_proc_entry(AML_DRVSTATE_NAME, g_proc);
    remove_proc_entry(AML_RVRINFO_NAME, g_proc);
    remove_proc_entry(AML_SCANPARAM_NAME, g_proc);
    remove_proc_entry(AML_WOW_WAKE_REASON, g_proc);
    return 0;
}

static ssize_t wowstateRead(struct file *filp, char __user *buf, size_t count,
    loff_t *f_pos)
{
    unsigned int len = 0;
    struct wifi_mac * wifimac = wifi_mac_get_mac_handle();
    if (*f_pos > 0)
        return 0;

    memset(g_aucprocbuf, 0, MAX_BUF_LENGTH);
    snprintf(g_aucprocbuf, MAX_BUF_LENGTH, "wakeup_reason: %d\n",wifimac->wow_wakeup_reason);
    len = strlen(g_aucprocbuf);

    if (copy_to_user(buf, g_aucprocbuf, len)) {
        ERROR_DEBUG_OUT("copy to user failed\n");
        return -EFAULT;
    }
    *f_pos += len;

    return len;
}

static const struct file_operations wowstate_ops = {
    .owner = THIS_MODULE,
    .read = wowstateRead,
};

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

    aml_proc = proc_create(AML_DRVSTATE_NAME, 0664, g_proc, &drvstate_ops);
    if (aml_proc == NULL) {
        ERROR_DEBUG_OUT("Unable to create /proc entry drv_state\n\r");
        return -1;
    }

    aml_proc = proc_create(AML_RVRINFO_NAME, 0664, g_proc, &rvrinfo_ops);
    if (aml_proc == NULL) {
        ERROR_DEBUG_OUT("Unable to create /proc entry rvr_info\n\r");
        return -1;
    }

    aml_proc = proc_create(AML_SCANPARAM_NAME, 0664, g_proc, &scanparam_ops);
    if (aml_proc == NULL) {
        ERROR_DEBUG_OUT("Unable to create /proc entry scan_param\n\r");
        return -1;
    }

    aml_proc = proc_create(AML_WOW_WAKE_REASON, 0664, g_proc, &wowstate_ops);
    if (aml_proc == NULL) {
        ERROR_DEBUG_OUT("Unable to create /proc entry wow_wakeup_reason\n\r");
        return -1;
    }
    return 0;
}

