#ifndef __PATCH_FI_CMD__
#define __PATCH_FI_CMD__
#include "fi_sdio.h"
#include "fi_cmd.h"

#define TXPWR_CFFC_CFG_CMD 0x2e

typedef struct patch_Cali_Param
{
    unsigned char Cmd;
    unsigned int version;
    unsigned short cali_config;
    unsigned char freq_offset;
    unsigned char htemp_freq_offset;
    unsigned char cca_ed_det;
    unsigned char tssi_2g_offset;
    unsigned char wf2g_spur_rmen;
    unsigned char rf_num;
    unsigned char tssi_5g_offset[6];
    unsigned char wftx_pwrtbl_en;
    unsigned char resv;
    txtssi_ratio_limit_param txtssi_ratio_limit;
    digital_gain_limit_param digital_gain_limit;
    unsigned short spur_freq;
    unsigned short platform_versionid;
} patch_Cali_Param;

struct tx_trb_info_ex
{
    /* The number of pages needed for a single transfer */
    unsigned int packet_num;
    /* Actual size used for each page */
    unsigned short buffer_size[128];
};

#if 0
struct tx_trb_info
{
    /* The number of pages needed for a single transfer */
    unsigned int trb_num;
    /* Total length of transmission */
    unsigned int total_len;
    /* Actual size used for each page */
    unsigned short buffer_size[128];
};

typedef struct Txpwr_Cffc_Cfg_Param
{
    unsigned char Cmd;
    unsigned char coefficient[57];
    unsigned char reserve[2];
    unsigned char band[4];
} Txpwr_Cffc_Cfg_Param;

#endif

#endif
