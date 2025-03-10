/*
 ****************************************************************************************
 *
 * Copyright (C) Amlogic 2010-2014
 *
 * Project: 11N 80211 HAL  layer Software
 *
 * Description:
 *   sdio interface function,used by HAL write/read sdio function
 *
 *
 ****************************************************************************************
 */

#ifdef HAL_SIM_VER
#ifdef FW_NAME
namespace FW_NAME
{
#endif
#endif

#include "wifi_hal.h"
#include "wifi_hif.h"

#include "wifi_pt_init.h"
#include "wifi_pt_network.h"

#if defined (HAL_FPGA_VER)
struct amlw_hwif_sdio g_amlw_hwif_sdio;

static inline struct amlw_hwif_sdio *aml_sdio_priv(void)
{
#ifndef SDIO_BUILD_IN
    return &g_amlw_hwif_sdio;
#else
    return &g_w1_hwif_sdio;
#endif
}
#endif

#if defined (HAL_FPGA_VER)
#include "wifi_mac_com.h"
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/card.h>

#define HIF_SDIO_UNIT_MULTIBLKSZ

#ifdef    DRIVER_FOR_BT  // access bt domain need more slower clk for cross the AHB bridge
int sdioclk = 3000000;
#else
#ifdef RF_T9026
    int sdioclk = 200000000;
#else
    int sdioclk = 170000000;
#endif
#endif

struct sdio_func *aml_priv_to_func(int func_n)
{
    ASSERT(func_n >= 0 &&  func_n < SDIO_FUNCNUM_MAX);

#ifndef SDIO_BUILD_IN
    return g_amlw_hwif_sdio.sdio_func_if[func_n];
#else
    return g_w1_hwif_sdio.sdio_func_if[func_n];
#endif
}
#endif//HAL_FPGA_VER


extern unsigned char set_wifi_bt_sdio_driver_bit(bool is_register, int shift);
extern void set_usb_bt_power(int is_on);
extern void set_usb_wifi_power(int is_on);
extern int  aml_sdio_init(void);
extern void  aml_sdio_exit(void);
extern unsigned char  chip_en_access;
extern unsigned char wifi_sdio_access;
extern unsigned char g_sdio_driver_insmoded;
extern unsigned char g_sdio_wifi_bt_alive;
extern unsigned char g_sdio_after_porbe;



#ifndef SDIO_BUILD_IN
#if defined (HAL_FPGA_VER)
static int _aml_sdio_request_byte(unsigned char   func_num,
                                    unsigned char   write,
                                    unsigned int      reg_addr,
                                    unsigned char  *byte)
{
    int err_ret;
    struct sdio_func * func = aml_priv_to_func(func_num );

#if defined(DBG_PRINT_COST_TIME)
    struct timespec now, before;
    getnstimeofday(&before);
#endif /* End of DBG_PRINT_COST_TIME */

    ASSERT(func != NULL);
    ASSERT(byte != NULL);
    ASSERT(func->num == func_num);

    /* Claim host controller */
    sdio_claim_host(func);

    if (write) {
        /* CMD52 Write */
        sdio_writeb(func, *byte, reg_addr, &err_ret);
    }
    else {
        /* CMD52 Read */
        *byte = sdio_readb(func, reg_addr, &err_ret);
    }

    /* Release host controller */
    sdio_release_host(func);

#if defined(DBG_PRINT_COST_TIME)
    getnstimeofday(&now);

    AML_PRINT_LOG_INFO("[sdio byte]: len=1 cost=%lds %luus\n",
        now.tv_sec-before.tv_sec, now.tv_nsec/1000 - before.tv_nsec/1000);
#endif /* End of DBG_PRINT_COST_TIME */

    return (err_ret == 0) ? SDIOH_API_RC_SUCCESS : SDIOH_API_RC_FAIL;
}

static int _aml_sdio_request_buffer(unsigned char func_num,
                                    unsigned int    fix_incr,
                                    unsigned char write,
                                    unsigned int    addr,
                                    void	            *buf,
                                    unsigned int    nbytes)
{
    int err_ret;
    int align_nbytes = nbytes;
    struct sdio_func * func = aml_priv_to_func(func_num);
    bool fifo = (fix_incr == SDIO_OPMODE_FIXED);

#if defined(DBG_PRINT_COST_TIME)
    struct timespec now, before;

    getnstimeofday(&before);
#endif /* End of DBG_PRINT_COST_TIME */


    ASSERT(buf != NULL);
    ASSERT(func != NULL);
    ASSERT(fix_incr == SDIO_OPMODE_FIXED|| fix_incr == SDIO_OPMODE_INCREMENT);
    ASSERT(func->num == func_num);

    /* Claim host controller */
    sdio_claim_host(func);

    if (write && !fifo)
    {
        /* write, increment */
        align_nbytes = sdio_align_size(func, nbytes);
        err_ret = sdio_memcpy_toio(func, addr, buf, align_nbytes);
    }
    else if (write)
    {
        /* write, fifo */
        err_ret = sdio_writesb(func, addr, buf, align_nbytes);
    }
    else if (fifo)
    {
        /* read */
        err_ret = sdio_readsb(func, buf, addr, align_nbytes);
    }
    else
    {
        /* read */
        align_nbytes = sdio_align_size(func, nbytes);
        err_ret = sdio_memcpy_fromio(func, buf, addr, align_nbytes);
    }

    /* Release host controller */
    sdio_release_host(func);

#if defined(DBG_PRINT_COST_TIME)
    getnstimeofday(&now);

    AML_PRINT_LOG_INFO("[sdio buffer]: len=%d cost=%lds %luus\n",
        nbytes, now.tv_sec-before.tv_sec, now.tv_nsec/1000 - before.tv_nsec/1000);
#endif /* End of DBG_PRINT_COST_TIME */

    return (err_ret == 0) ? SDIOH_API_RC_SUCCESS : SDIOH_API_RC_FAIL;
}
#endif//HAL_FPGA_VER

void aml_sdio_write_word(unsigned int addr, unsigned int data)
{
    unsigned char* sdio_kmm  = (unsigned char*)ZMALLOC(sizeof(unsigned int), "sdio_write_word", GFP_DMA|GFP_ATOMIC);
    ASSERT(sdio_kmm);
    memcpy(sdio_kmm, &data, sizeof(data));
    // for bt access always on reg
    if ((addr & 0x00f00000) == 0x00f00000)
    {
        aml_aon_write_reg(addr, data);
    }
    else if (((addr & 0x00f00000) == 0x00b00000)||
        ((addr & 0x00f00000) == 0x00d00000)||
        ((addr & 0x00f00000) == 0x00900000))
    {
        aml_aon_write_reg(addr, data);
    }
    else if (((addr & 0x00f00000) == 0x00200000)||
        ((addr & 0x00f00000) == 0x00300000)||
        ((addr & 0x00f00000) == 0x00400000))
    {
        aml_aon_write_reg((addr), data);
    }
    else
    {
        aml_sdio_write_sram(sdio_kmm,
                                    (unsigned char*)(SYS_TYPE)(addr),
                                    sizeof(unsigned int));
    }

    FREE(sdio_kmm, "sdio_write_word");
}

unsigned int aml_sdio_read_word(unsigned int addr)
{
    unsigned int regdata = 0;

// for bt access always on reg
    if ((addr & 0x00f00000) == 0x00f00000)
    {
        regdata = aml_aon_read_reg(addr);
    }
    else if (((addr & 0x00f00000) == 0x00b00000)||
        ((addr & 0x00f00000) == 0x00d00000)||
        ((addr & 0x00f00000) == 0x00900000))
    {
        regdata = aml_aon_read_reg(addr);
    }
    else if (((addr & 0x00f00000) == 0x00200000)||
        ((addr & 0x00f00000) == 0x00300000)||
        ((addr & 0x00f00000) == 0x00400000))
    {
        regdata = aml_aon_read_reg(addr);
    }
    else
    {
        aml_sdio_read_sram((unsigned char*)(SYS_TYPE)&regdata,
                                        (unsigned char*)(SYS_TYPE)(addr),
                                        sizeof(unsigned int));
    }
    return regdata;
}

void aml_sdio_write_cmd32(unsigned long sram_addr, unsigned long sramdata)
{
    struct  hw_interface* hif = hif_get_hw_interface();

#if defined (HAL_SIM_VER)
    aml_sdio_read_write(hif, sram_addr&SDIO_ADDR_MASK,	(unsigned char *)&sramdata, 4,
        SDIO_FUNC3,SDIO_RW_FLAG_WRITE,SDIO_F_SYNCHRONOUS);
#elif defined (HAL_FPGA_VER)
    ASSERT(hif != NULL);

    hif->hif_ops.hi_bottom_write(SDIO_FUNC3, sram_addr&SDIO_ADDR_MASK,
        (unsigned char *)&sramdata, sizeof(unsigned long), SDIO_OPMODE_INCREMENT);

#endif /* End of HAL_SIM_VER */
}

int aml_sdio_suspend(unsigned int suspend_enable)
{
#if defined (HAL_FPGA_VER)
    mmc_pm_flag_t flags;
    struct sdio_func *func = NULL;
    int ret = 0, i;

    if (suspend_enable == 0)
    {
        /* when enable == 0 that's resume operation
        and we do nothing for resume now. */
        return ret;
    }

    /* just clear sdio clock value for emmc init when resume */
    amlwifi_set_sdio_host_clk(0);

    /* we shall suspend all card for sdio. */
    for (i = SDIO_FUNC1; i <= FUNCNUM_SDIO_LAST; i++)
    {
        func = aml_priv_to_func(i);
        if (func == NULL)
            continue;
        flags = sdio_get_host_pm_caps(func);

        if ((flags & MMC_PM_KEEP_POWER) != 0)
            ret = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);

        if (ret != 0)
            return -1;

        /*
         * if we don't use sdio irq, we can't get functions' capability with
         * MMC_PM_WAKE_SDIO_IRQ, so we don't need set irq for wake up
         * sdio for upcoming suspend.
         */
        if ((flags & MMC_PM_WAKE_SDIO_IRQ) != 0)
            ret = sdio_set_host_pm_flags(func, MMC_PM_WAKE_SDIO_IRQ);

        if (ret != 0)
            return -1;
    }
    return ret;
#elif defined (HAL_SIM_VER)
    return 0;
#endif
}

unsigned long  aml_sdio_read_reg8(unsigned long sram_addr )
{
    struct hw_interface* hif = hif_get_hw_interface();
    unsigned char regdata[8] = {0};

    ASSERT(hif != NULL);

#if defined (HAL_SIM_VER)
    aml_sdio_read_write(hif, sram_addr,  regdata, 1,
                    SDIO_FUNC1,SDIO_RW_FLAG_READ,SDIO_F_SYNCHRONOUS);
#elif defined (HAL_FPGA_VER)
    hif->hif_ops.hi_bottom_read(SDIO_FUNC1, sram_addr&SDIO_ADDR_MASK, regdata, 1, SDIO_OPMODE_INCREMENT);
#endif /* End of HAL_SIM_VER */

    return regdata[0];
}

void aml_sdio_write_reg8(unsigned long sram_addr, unsigned long sramdata)
{
    struct  hw_interface* hif = hif_get_hw_interface();

#if defined (HAL_SIM_VER)
    unsigned char regdata[8] = {0};
    regdata[0] =sramdata;
    aml_sdio_read_write(hif, sram_addr,  regdata, 1,
                SDIO_FUNC1,SDIO_RW_FLAG_WRITE, SDIO_F_SYNCHRONOUS);
#elif defined (HAL_FPGA_VER)

    hif->hif_ops.hi_bottom_write(SDIO_FUNC1, sram_addr&SDIO_ADDR_MASK,
        (unsigned char *)&sramdata, sizeof(unsigned long), SDIO_OPMODE_INCREMENT);
#endif /* End of HAL_SIM_VER */
}

unsigned long aml_sdio_read_reg32(unsigned long sram_addr)
{
    struct  hw_interface* hif = hif_get_hw_interface();

#if defined (HAL_SIM_VER)
    unsigned int data =0;
    unsigned char regdata[8] = {0};
    aml_sdio_read_write(hif, sram_addr++, regdata, 1,
                    SDIO_FUNC1, SDIO_RW_FLAG_READ, SDIO_F_SYNCHRONOUS);
    data = regdata[0];

    aml_sdio_read_write(hif, sram_addr++, regdata, 1,
                    SDIO_FUNC1, SDIO_RW_FLAG_READ, SDIO_F_SYNCHRONOUS);
    data |= regdata[0]<<8;

    aml_sdio_read_write(hif, sram_addr++, regdata, 1,
                    SDIO_FUNC1, SDIO_RW_FLAG_READ, SDIO_F_SYNCHRONOUS);
    data |= regdata[0]<<16;

    aml_sdio_read_write(hif, sram_addr++, regdata, 1,
                    SDIO_FUNC1, SDIO_RW_FLAG_READ, SDIO_F_SYNCHRONOUS);
    data |= regdata[0]<<24;
    return data;
#elif defined (HAL_FPGA_VER)
    unsigned long sramdata;

    ASSERT(hif != NULL);

    hif->hif_ops.hi_bottom_read(SDIO_FUNC1, sram_addr&SDIO_ADDR_MASK, &sramdata, 4, SDIO_OPMODE_INCREMENT);

    return sramdata;
#endif /* End of HAL_SIM_VER */
}

int aml_sdio_write_reg32(unsigned long sram_addr, unsigned long sramdata)
{
    struct  hw_interface* hif = hif_get_hw_interface();

#if defined (HAL_SIM_VER)
    unsigned char regdata[8] = {0};
    regdata[0] =sramdata&0xff;
    aml_sdio_read_write(hif, sram_addr++, regdata, 1,
                    SDIO_FUNC1, SDIO_RW_FLAG_WRITE, SDIO_F_SYNCHRONOUS);

    regdata[0] =(sramdata>>8)&0xff;
    aml_sdio_read_write(hif, sram_addr++, regdata, 1,
                    SDIO_FUNC1, SDIO_RW_FLAG_WRITE, SDIO_F_SYNCHRONOUS);

    regdata[0] =(sramdata>>16)&0xff;
    aml_sdio_read_write(hif, sram_addr++, regdata, 1,
                    SDIO_FUNC1, SDIO_RW_FLAG_WRITE, SDIO_F_SYNCHRONOUS);

    regdata[0] =(sramdata>>24)&0xff;
    aml_sdio_read_write(hif, sram_addr++, regdata, 1,
                    SDIO_FUNC1, SDIO_RW_FLAG_WRITE, SDIO_F_SYNCHRONOUS);
#elif defined (HAL_FPGA_VER)
    int ret = 0;
    ASSERT(hif != NULL);

    ret=  hif->hif_ops.hi_bottom_write(SDIO_FUNC1, sram_addr&SDIO_ADDR_MASK,
        (unsigned char *)&sramdata,  sizeof(unsigned long), SDIO_OPMODE_INCREMENT);

    return ret;
#endif /* End of HAL_SIM_VER */
}

void aml_sdio_write_sram (unsigned char *buf, unsigned char *addr, SYS_TYPE len)
{
    struct hw_interface* hif = hif_get_hw_interface();

#if defined (HAL_SIM_VER)
    aml_sdio_read_write(hif, (SYS_TYPE)addr,buf,len,
                    SDIO_FUNC2,SDIO_RW_FLAG_WRITE,SDIO_F_SYNCHRONOUS);
#elif defined (HAL_FPGA_VER)
    ASSERT(hif != NULL);

    hif->hif_ops.hi_bottom_write(SDIO_FUNC2, (SYS_TYPE)addr&SDIO_ADDR_MASK,
        buf, len,
        (len > 8 ? SDIO_OPMODE_INCREMENT : SDIO_OPMODE_FIXED));

#endif /* End of HAL_SIM_VER */
}

void aml_sdio_read_sram (unsigned char *buf, unsigned char *addr, SYS_TYPE len)
{
    struct hw_interface* hif = hif_get_hw_interface();

#if defined (HAL_SIM_VER)
    aml_sdio_read_write(hif, (SYS_TYPE)addr,buf,len,
                    SDIO_FUNC2,SDIO_RW_FLAG_READ,SDIO_F_SYNCHRONOUS);
#elif defined (HAL_FPGA_VER)
    ASSERT(hif != NULL);

    hif->hif_ops.hi_bottom_read(SDIO_FUNC2, (SYS_TYPE)addr&SDIO_ADDR_MASK,
        buf, len,
        (len > 8 ? SDIO_OPMODE_INCREMENT : SDIO_OPMODE_FIXED));
#endif /* End of HAL_SIM_VER */
}

#if defined (HAL_SIM_VER)
void aml_sdio_send_frame (unsigned char *buf, unsigned char *addr, SYS_TYPE len)
{
    struct hw_interface* hif = hif_get_hw_interface();
    aml_sdio_read_write(hif, (SYS_TYPE)addr,buf,len,
                    SDIO_FUNC4,SDIO_RW_FLAG_WRITE,SDIO_F_SYNCHRONOUS);
}
#endif

void aml_sdio_recv_frame (unsigned char *buf, unsigned char *addr, SYS_TYPE len)
{
    struct hw_interface* hif = hif_get_hw_interface();

#if defined (HAL_SIM_VER)
    aml_sdio_read_write(hif, (SYS_TYPE)addr,buf,len,
                    SDIO_FUNC6,SDIO_RW_FLAG_READ,SDIO_F_SYNCHRONOUS);
#elif defined (HAL_FPGA_VER)

    hif->hif_ops.hi_bottom_read(SDIO_FUNC6, ((SYS_TYPE)addr & SDIO_ADDR_MASK),
        buf, len, SDIO_OPMODE_INCREMENT);
#endif /* End of HAL_SIM_VER */
}

#if defined (HAL_FPGA_VER)
static int amlw_sdio_alloc_prep_scat_req(struct amlw_hwif_sdio *hif_sdio)
{
    struct amlw_hif_scatter_req * scat_req = NULL;

    ASSERT(hif_sdio != NULL);

    /* allocate the scatter request */
    scat_req = ZMALLOC(sizeof(struct amlw_hif_scatter_req), "sdio_alloc_prep_scat_req", GFP_ATOMIC|GFP_DMA);
    if (scat_req == NULL)
    {
        AML_PRINT_LOG_ERR("[sdio sg alloc_scat_req]: no mem\n");
        return 1;
    }

    scat_req->free = true;
    hif_sdio->scat_req = scat_req;

    return 0;
}

struct amlw_hif_scatter_req *aml_sdio_scatter_req_get(void)
{
    struct hw_interface * hif = hif_get_hw_interface();
    struct amlw_hwif_sdio *hif_sdio = aml_sdio_priv();

    struct amlw_hif_scatter_req *scat_req = NULL;

    ASSERT(hif != NULL);
    ASSERT(hif_sdio != NULL);

    scat_req = hif_sdio->scat_req;

    if (scat_req->free)
    {
        scat_req->free = false;
    }
    else if (scat_req->scat_count != 0) // get scat_req, but not build scatter list
    {
        scat_req = NULL;
    }

    return scat_req;
}

int aml_sdio_enable_scatter(void)
{
    struct hw_interface * hif = hif_get_hw_interface();
    struct amlw_hwif_sdio *hif_sdio = aml_sdio_priv();
    int ret;

    ASSERT(hif != NULL);
    ASSERT(hif_sdio != NULL);

    if (hif_sdio->scatter_enabled)
        return 0;

    // TODO : getting hw_config to configure scatter number;

    hif_sdio->scatter_enabled = true;

    ret = amlw_sdio_alloc_prep_scat_req(hif_sdio);

    return ret;
}

int aml_sdio_scat_rw(struct scatterlist *sg_list, unsigned int sg_num, unsigned int blkcnt,
        unsigned char func_num, unsigned int addr, unsigned char write)
{
    struct mmc_request mmc_req;
    struct mmc_command mmc_cmd;
    struct mmc_data    mmc_dat;
    struct sdio_func *func = aml_priv_to_func(func_num);
    int ret = 0;

    memset(&mmc_req, 0, sizeof(struct mmc_request));
    memset(&mmc_cmd, 0, sizeof(struct mmc_command));
    memset(&mmc_dat, 0, sizeof(struct mmc_data));

    mmc_dat.sg     = sg_list;
    mmc_dat.sg_len = sg_num;
    mmc_dat.blksz  = FUNC4_BLKSIZE;
    mmc_dat.blocks = blkcnt;
    mmc_dat.flags  = write ? MMC_DATA_WRITE : MMC_DATA_READ;

    mmc_cmd.opcode = SD_IO_RW_EXTENDED;
    mmc_cmd.arg    = write ? 1 << 31 : 0;
    mmc_cmd.arg   |= (func_num & 0x7) << 28;
    mmc_cmd.arg   |= 1 << 27;	/* block basic */
    mmc_cmd.arg   |= 0 << 26;	/* 1 << 26;*/   	/*0 fix address */
    mmc_cmd.arg   |= (addr & 0x1ffff)<< 9;
    mmc_cmd.arg   |= blkcnt & 0x1ff;
    mmc_cmd.flags  = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_ADTC;

    mmc_req.cmd = &mmc_cmd;
    mmc_req.data = &mmc_dat;

    sdio_claim_host(func);
    mmc_set_data_timeout(&mmc_dat, func->card);
    mmc_wait_for_req(func->card->host, &mmc_req);
    sdio_release_host(func);

    if (mmc_cmd.error || mmc_dat.error) {
        AML_PRINT_LOG_INFO("ERROR CMD53 %s cmd_error = %d data_error=%d\n",
        write ? "write" : "read", mmc_cmd.error, mmc_dat.error);
        ret  = mmc_cmd.error;
    }

    return ret;
}

void aml_sdio_cleanup_scatter(void)
{
    struct hw_interface * hif = hif_get_hw_interface();
    struct amlw_hwif_sdio *hif_sdio = aml_sdio_priv();

    AML_PRINT_LOG_INFO("[sdio sg cleanup]: enter\n");

    ASSERT(hif != NULL);
    ASSERT(hif_sdio != NULL);

    if (!hif_sdio->scatter_enabled)
        return;

    hif_sdio->scatter_enabled = false;

    /* empty the free list */
    FREE(hif_sdio->scat_req, "sdio_alloc_prep_scat_req");

    AML_PRINT_LOG_INFO("[sdio sg cleanup]: exit\n");

    return;
}
#endif //end of HAL_FPGA_VER

/*For BT use only start */
void aml_bt_sdio_write_sram (unsigned char *buf, unsigned char *addr, SYS_TYPE len)
{
    struct hw_interface* hif = hif_get_hw_interface();

#if defined (HAL_SIM_VER)
    aml_sdio_read_write(hif, (SYS_TYPE)addr, buf,len, SDIO_FUNC5,
        SDIO_RW_FLAG_WRITE, SDIO_F_SYNCHRONOUS);
#elif defined (HAL_FPGA_VER)
    hif->hif_ops.hi_bottom_write(SDIO_FUNC5, ((SYS_TYPE)addr & SDIO_ADDR_MASK),
        buf, len, (len > 8 ? SDIO_OPMODE_INCREMENT : SDIO_OPMODE_FIXED));
#endif
}

void aml_bt_sdio_read_sram (unsigned char *buf, unsigned char *addr, SYS_TYPE len)
{
    struct hw_interface* hif = hif_get_hw_interface();

#if defined (HAL_SIM_VER)
    aml_sdio_read_write(hif, (SYS_TYPE)addr, buf,len,
        SDIO_FUNC5, SDIO_RW_FLAG_READ, SDIO_F_SYNCHRONOUS);
#elif defined (HAL_FPGA_VER)

    hif->hif_ops.hi_bottom_read(SDIO_FUNC5, ((SYS_TYPE)addr & SDIO_ADDR_MASK),
        buf, len, (len > 8 ? SDIO_OPMODE_INCREMENT : SDIO_OPMODE_FIXED));
#endif /* HAL_SIM_VER */
}

void aml_bt_hi_write_word(unsigned int addr,unsigned int data)
{
    unsigned int reg_tmp;
    struct hw_interface* hif = hif_get_hw_interface();

    unsigned char* sdio_kmm = (unsigned char*)ZMALLOC(sizeof(data), "bt_hi_write_word", GFP_DMA|GFP_ATOMIC);
    ASSERT(sdio_kmm);

    memcpy(sdio_kmm, &data, sizeof(data));
    /*
     * make sure function 5 section address-mapping feature is disabled,
     * when this feature is disabled,
     * all 128k space in one sdio-function use only
     * one address-mapping: 32-bit AHB Address = BaseAddr + cmdRegAddr
     */
    reg_tmp = hif->hif_ops.hi_read_word(RG_SDIO_IF_MISC_CTRL);

    if ((reg_tmp & BIT(23)) != 1)
    {
        reg_tmp |= BIT(23);
        hif->hif_ops.hi_write_word(RG_SDIO_IF_MISC_CTRL , reg_tmp);
    }
    /*config msb 15 bit address in BaseAddr Register*/
    hif->hif_ops.hi_write_reg32(RG_SCFG_FUNC5_BADDR_A,addr & 0xfffe0000);

    hif->hif_ops.bt_hi_write_sram(sdio_kmm,
        /*sdio cmd 52/53 can only take 17 bit address*/
        (unsigned char*)(SYS_TYPE)(addr & 0x1ffff), sizeof(unsigned int));

    FREE(sdio_kmm, "bt_hi_write_word");
}

unsigned int aml_bt_hi_read_word(unsigned int addr)
{
    unsigned int regdata = 0;
    unsigned int reg_tmp;
    struct hw_interface* hif = hif_get_hw_interface();
    /*
     * make sure function 5 section address-mapping feature is disabled,
     * when this feature is disabled,
     * all 128k space in one sdio-function use only
     * one address-mapping: 32-bit AHB Address = BaseAddr + cmdRegAddr
     */

    reg_tmp = hif->hif_ops.hi_read_word( RG_SDIO_IF_MISC_CTRL);

    if ((reg_tmp & BIT(23)) != 1)
    {
        reg_tmp |= BIT(23);
        hif->hif_ops.hi_write_word( RG_SDIO_IF_MISC_CTRL, reg_tmp);
    }

    /*config msb 15 bit address in BaseAddr Register*/
    hif->hif_ops.hi_write_reg32(RG_SCFG_FUNC5_BADDR_A,addr & 0xfffe0000);
    hif->hif_ops.bt_hi_read_sram((unsigned char*)(SYS_TYPE)&regdata,
        /*sdio cmd 52/53 can only take 17 bit address*/
        (unsigned char*)(SYS_TYPE)(addr & 0x1ffff), sizeof(unsigned int));
    return regdata;
}
/*For BT use only end */

#if defined (HAL_FPGA_VER)
//cmd53
int aml_sdio_bottom_read(unsigned char  func_num, int addr, void *buf, size_t len,int incr_addr)
{
    void *kmalloc_buf = NULL;
    int result;
    int align_len = 0;

    ASSERT(func_num != SDIO_FUNC0);

    if (hal_wake_fw_req() == 0)
    {
        AML_PRINT_LOG_ERR("wake up failed\n");
        return -1;
    }

    /* read block mode */
    if (func_num != SDIO_FUNC6)
    {
        if (incr_addr == SDIO_OPMODE_INCREMENT)
        {
            struct sdio_func * func = aml_priv_to_func(func_num);

            align_len = sdio_align_size(func, len);
        }
        else
            align_len = len;

        kmalloc_buf = (unsigned char *)ZMALLOC(align_len, "sdio_bottom_read", GFP_DMA|GFP_ATOMIC);
    }
    else
    {
        kmalloc_buf = buf;
    }
    if (kmalloc_buf == NULL)
    {
        AML_PRINT_LOG_ERR("kmalloc buf fail\n");
        return SDIOH_API_RC_FAIL;
    }

    result = _aml_sdio_request_buffer(func_num, incr_addr, SDIO_READ, addr, kmalloc_buf, len);

    if (func_num != SDIO_FUNC6)
    {
        memcpy(buf, kmalloc_buf, len);
        FREE(kmalloc_buf, "sdio_bottom_read");
    }
    return result;
}

//cmd53
int aml_sdio_bottom_write(unsigned char  func_num,int addr, void *buf, size_t len,int incr_addr)
{
    void *kmalloc_buf;
    int result;

    ASSERT(func_num != SDIO_FUNC0);

    if (hal_wake_fw_req() == 0)
    {
        AML_PRINT_LOG_ERR("wake up failed\n");
        return -1;
    }

    kmalloc_buf =  (unsigned char *)ZMALLOC(len, "sdio_bottom_write", GFP_DMA|GFP_ATOMIC);//virt_to_phys(fwICCM);
    if (kmalloc_buf == NULL)
    {
        AML_PRINT_LOG_ERR("kmalloc buf fail\n");
        return SDIOH_API_RC_FAIL;
    }
    memcpy(kmalloc_buf, buf, len);
    //AML_PRINT_LOG_INFO("%s, buf:%p\n", kmalloc_buf);

    result = _aml_sdio_request_buffer(func_num, incr_addr, SDIO_WRITE, addr, kmalloc_buf, len);

    FREE(kmalloc_buf, "sdio_bottom_write");
    return result;
}

/* cmd52
   buff[7:0],     // funcIoNum
   buff[25:8],    // regAddr
   buff[39:32]);  // regValue
*/
void aml_sdio_bottom_write8_func0(unsigned long sram_addr, unsigned char sramdata)
{
    struct  hw_interface* hif = hif_get_hw_interface();
    unsigned char * sdio_kmm = (unsigned char *)ZMALLOC(sizeof(unsigned char), "sdio_bottom_write8_func0", GFP_DMA |GFP_ATOMIC);
    ASSERT(sdio_kmm);
    ASSERT(hif != NULL);

    //__virt_to_phys((unsigned long)&sramdata);
    //__phys_addr_symbol((unsigned long)&sramdata);

    memcpy(sdio_kmm, &sramdata, sizeof(unsigned char));
    _aml_sdio_request_byte(SDIO_FUNC0, SDIO_WRITE, sram_addr, sdio_kmm);

    FREE(sdio_kmm, "sdio_bottom_write8_func0");
}

//cmd52
unsigned char aml_sdio_bottom_read8_func0(unsigned long sram_addr)
{
    struct  hw_interface* hif = hif_get_hw_interface();
    unsigned char sramdata;
    unsigned char* sdio_kmm = (unsigned char*)ZMALLOC(sizeof(unsigned char), "sdio_bottom_read8_func0", GFP_DMA |GFP_ATOMIC);
    ASSERT(sdio_kmm);
    ASSERT(hif != NULL);

    _aml_sdio_request_byte(SDIO_FUNC0, SDIO_READ, sram_addr, sdio_kmm);

    memcpy( &sramdata, sdio_kmm,sizeof(unsigned char));

    FREE(sdio_kmm, "sdio_bottom_read8_func0");
    return sramdata;
}


//cmd52
int aml_sdio_bottom_write8(unsigned char  func_num,int addr, unsigned char data)
{
    int ret = 0;
    unsigned char *sdio_kmm = (unsigned char*)ZMALLOC(sizeof(data), "sdio_bottom_write8", GFP_DMA |GFP_ATOMIC);

    ASSERT(sdio_kmm != NULL);
    ASSERT(func_num != SDIO_FUNC0);

    //__virt_to_phys((unsigned long)&data);
    //__phys_addr_symbol((unsigned long)&data);

    memcpy(sdio_kmm, &data, sizeof(char));
    ret =  _aml_sdio_request_byte(func_num, SDIO_WRITE, addr, sdio_kmm);
    FREE(sdio_kmm, "sdio_bottom_write8");

    return ret;
}

//cmd52
unsigned char aml_sdio_bottom_read8(unsigned char  func_num, int addr)
{
    unsigned char sramdata;
    unsigned char* sdio_kmm = (unsigned char*)ZMALLOC(sizeof(unsigned char), "sdio_bottom_read8", GFP_DMA |GFP_ATOMIC);
    ASSERT(sdio_kmm);

    _aml_sdio_request_byte(func_num, SDIO_READ, addr, sdio_kmm);

    memcpy(&sramdata, sdio_kmm, sizeof(unsigned char));
    FREE(sdio_kmm, "sdio_bottom_read8");

    return sramdata;
}
#endif//HAL_FPGA_VER
#endif//SDIO_BUILD_IN

void aml_sdio_scat_complete (struct amlw_hif_scatter_req * scat_req)
{
    int  i;
    struct hw_interface * hif = hif_get_hw_interface();
    struct amlw_hwif_sdio *hif_sdio = aml_sdio_priv();

    ASSERT(scat_req != NULL);
    ASSERT(hif != NULL);
    ASSERT(hif_sdio != NULL);

    if (scat_req->complete)
    {
        for (i = 0; i < scat_req->scat_count; i++)
        {
            (scat_req->complete)(scat_req->scat_list[i].skbbuf);
            scat_req->scat_list[i].skbbuf = NULL;
        }
    }
    else
    {
        AML_PRINT_LOG_ERR("error: no complete function\n");
    }

    scat_req->free = true;
    scat_req->scat_count = 0;
    scat_req->len = 0;
    scat_req->addr = 0;
    memset(scat_req->sgentries, 0, MAX_SG_ENTRIES * sizeof(struct scatterlist));
}

int aml_sdio_scat_req_rw(struct amlw_hif_scatter_req *scat_req)
{
    struct hw_interface *hif = hif_get_hw_interface();
    struct amlw_hwif_sdio *hif_sdio = aml_sdio_priv();
    struct sdio_func *func = NULL;
    struct mmc_host *host = NULL;

    unsigned int blk_size, blk_num;
    unsigned int max_blk_count, max_req_size;
    unsigned int func_num;

    struct scatterlist *sg;
    int sg_count, sgitem_count;
    int ttl_len, pkt_offset, ttl_page_num;

    struct mmc_request mmc_req;
    struct mmc_command mmc_cmd;
    struct mmc_data mmc_dat;

    int result = SDIOH_API_RC_FAIL;

    ASSERT(scat_req != NULL);
    if (scat_req->req & HIF_WRITE)
        func_num = SDIO_FUNC4;
    else
        func_num = SDIO_FUNC6;

    func = hif_sdio->sdio_func_if[func_num];
    host = func->card->host;

    blk_size = func->cur_blksize;
    max_blk_count = MIN(host->max_blk_count, SDIO_MAX_BLK_CNT);
    max_req_size = MIN(max_blk_count * blk_size, host->max_req_size);

    /* fill SG entries */
    sg = scat_req->sgentries;
    pkt_offset = 0;	    // reminder
    sgitem_count = 0; // count of scatterlist

    while (sgitem_count < scat_req->scat_count)
    {
        ttl_len = 0;
        sg_count = 0;
        ttl_page_num = 0;

        sg_init_table(sg, MAXSG_SIZE);

        /* assemble SG list */
        while ((sgitem_count < scat_req->scat_count) && (ttl_len < max_req_size))
        {
            int packet_len = 0;
            int sg_data_size = 0;
            unsigned char *pdata = NULL;

            if (sg_count >= MAXSG_SIZE)
                break;

            packet_len = scat_req->scat_list[sgitem_count].len;
            pdata = scat_req->scat_list[sgitem_count].packet;

#if defined(HIF_SDIO_UNIT_MULTIBLKSZ)
            // sg len must be aligned with block size
            sg_data_size = ALIGN(packet_len, blk_size);
            if (sg_data_size > (max_req_size - ttl_len))
            {
                AML_PRINT_LOG_INFO(" setup scat-data: sg_data_size %d, remain %d \n",
                    sg_data_size, max_req_size - ttl_len);
                break;
            }
#else
            pdata += pkt_offset;
            sg_data_size = packet_len - pkt_offset;
            // last sg
            if (sg_data_size > max_req_size - ttl_len)
                sg_data_size = max_req_size - ttl_len;

            pkt_offset += sg_data_size; // actually length
#endif

            sg_set_buf(&scat_req->sgentries[sg_count], pdata, sg_data_size);
            sg_count++;
            ttl_len += sg_data_size;

#if defined(HIF_SDIO_UNIT_MULTIBLKSZ)
            ttl_page_num += scat_req->scat_list[sgitem_count].page_num;
            sgitem_count++;
#else
            if (pkt_offset == packet_len)
            {
                ttl_page_num += scat_req->scat_list[sgitem_count].page_num;
                //move to next
                sgitem_count++;
                pkt_offset = 0;

                if (sgitem_count == scat_req->scat_count)
                    ttl_len = ALIGN(ttl_len, blk_size);
            }
#endif
            /*
            AML_PRINT_LOG_INFO("setup scat-data: offset: %d: ttl: %d, datalen:%d\n",
                pkt_offset, ttl_len, sg_data_size);
            */
        }

        if ((ttl_len == 0) || (ttl_len % blk_size != 0))
        {
            AML_PRINT_LOG_INFO(" setup scat-data: ttl_len %d \n", ttl_len);
            return result;
        }

        memset(&mmc_req, 0, sizeof(struct mmc_request));
        memset(&mmc_cmd, 0, sizeof(struct mmc_command));
        memset(&mmc_dat, 0, sizeof(struct mmc_data));

        /* set scatter-gather table for request */
        blk_num = ttl_len / blk_size;
        mmc_dat.flags = (scat_req->req & HIF_WRITE) ? MMC_DATA_WRITE : MMC_DATA_READ;
        mmc_dat.sg = scat_req->sgentries;
        mmc_dat.sg_len = sg_count;
        mmc_dat.blksz = blk_size;
        mmc_dat.blocks = blk_num;

        mmc_cmd.opcode = SD_IO_RW_EXTENDED;
        mmc_cmd.arg = (scat_req->req & HIF_WRITE) ? 1 << 31 : 0;
        mmc_cmd.arg |= (func_num & 0x7) << 28;
        /* block basic */
        mmc_cmd.arg |= 1 << 27;
        /* 0, fix address */
        mmc_cmd.arg |= SDIO_OPMODE_FIXED << 26;
        mmc_cmd.arg |= (scat_req->addr & 0x1ffff)<< 9;
        mmc_cmd.arg |= mmc_dat.blocks & 0x1ff;
        mmc_cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_ADTC;

        mmc_req.cmd = &mmc_cmd;
        mmc_req.data = &mmc_dat;

        sdio_claim_host(func);
        mmc_set_data_timeout(&mmc_dat, func->card);
        mmc_wait_for_req(func->card->host, &mmc_req);
        sdio_release_host(func);

        /*
        AML_PRINT_LOG_INFO("setup scat-data: (%s) ====addr: 0x%X, (blksz: %d, blocks: %d) , (ttl:%d,sg:%d,scat_count:%d,ttl_page:%d)====\n",
            (scat_req->req & HIF_WRITE) ? "wr" : "rd", scat_req->addr,
            mmc_dat.blksz, mmc_dat.blocks, ttl_len,
            sg_count, scat_req->scat_count, ttl_page_num);
        */
        if (mmc_cmd.error || mmc_dat.error)
        {
            AML_PRINT_LOG_ERR("ERROR CMD53 %s cmd_error = %d data_error=%d\n",
                (scat_req->req & HIF_WRITE) ? "write" : "read", mmc_cmd.error, mmc_dat.error);
        }
        else
        {
#if (SDIO_UPDATE_HOST_WRPTR == 0)
            // update sdio write ptr by host.
            struct hal_private * hal_priv = hal_get_priv();

            hal_priv->tx_page_offset = CIRCLE_Addition2(ttl_page_num, hal_priv->tx_page_offset, TX_ADDRESSTABLE_NUM);
            ttl_page_num = 0;

            hif->hif_ops.hi_write_word(RG_WIFI_IF_MAC_TXTABLE_RD_ID, hal_priv->tx_page_offset);
#endif
        }
    }

    result = mmc_cmd.error ? mmc_cmd.error : mmc_dat.error;

    scat_req->result = result;

    if (scat_req->result)
        AML_PRINT_LOG_ERR("Scatter write request failed:%d\n", scat_req->result);

    if (scat_req->req & HIF_ASYNCHRONOUS)
        aml_sdio_scat_complete(scat_req);

    return result;
}

#if defined (HAL_FPGA_VER)
void aml_sdio_wake_up_int(void)
{
    hal_irq_top(0,0);
    return;
}

void aml_sdio_reset()
{
    unsigned char reg = 1;
    struct hw_interface* hif = hif_get_hw_interface();

    AML_PRINT_LOG_INFO("aml_sdio_reset \n");
    ASSERT(hif != NULL && hif->hif_ops.hi_bottom_write8 != NULL);

    hif->hif_ops.hi_bottom_write8(SDIO_FUNC0, SDIO_CCCR_IOABORT, reg);
}

void aml_sdio_irq_path(unsigned char b_gpio)
{
    unsigned int regdata = 0;
    unsigned int ck_reg = 0;
    struct hw_interface* hif = hif_get_hw_interface();

    AML_PRINT_LOG_INFO("b_gpio=%d \n", b_gpio);

    if (b_gpio)
    {
        if (!aml_bus_type) {
            regdata= hif->hif_ops.hi_read8_func0(SDIO_CCCR_IEN);

            AML_PRINT_LOG_INFO(" SDIO_CCCR_IEN=0x%x \n", regdata);
                regdata &= ~0x3;

            hif->hif_ops.hi_write8_func0(SDIO_CCCR_IEN,regdata);
            AML_PRINT_LOG_INFO(" SDIO CCCR IEN=0x%x \n", regdata);
        }

        regdata = hif->hif_ops.hi_read_word(RG_WIFI_IF_FW2HST_CLR);

        regdata |= SDIO_FW2HOST_EN;
        hif->hif_ops.hi_write_word(RG_WIFI_IF_FW2HST_CLR, regdata);

        ck_reg = hif->hif_ops.hi_read_word(RG_WIFI_IF_FW2HST_CLR);
        AML_PRINT_LOG_INFO("ck_reg 0x%x\n", ck_reg);
    }
    else
    {
        regdata= hif->hif_ops.hi_read8_func0(SDIO_CCCR_IEN);
        AML_PRINT_LOG_INFO(" SDIO_CCCR_IEN=0x%x \n", regdata);
        regdata |= 0x3;

        hif->hif_ops.hi_write8_func0(SDIO_CCCR_IEN,regdata);
        AML_PRINT_LOG_INFO(" SDIO_CCCR_IEN=0x%x \n", regdata);

        regdata = hif->hif_ops.hi_read_word(RG_WIFI_IF_FW2HST_CLR);
        regdata &= ~SDIO_FW2HOST_GPIO_EN;

        hif->hif_ops.hi_write_word(RG_WIFI_IF_FW2HST_CLR, regdata);

        //set interupt to level
        //regdata = hw_if->hif_ops.hi_read_word(RG_WIFI_IF_GPIO_IRQ_CNF);
        regdata |= SDIO_GPIO_IRQ_TRIG_MODE_LEVEL;
        //hw_if->hif_ops.hi_write_word(RG_WIFI_IF_GPIO_IRQ_CNF, regdata);
    }
}

#ifndef USE_SDIO_IRQ
int amlhal_gpio_open(struct hal_private * hal_priv)
{
    struct hw_interface* hif = hif_get_hw_interface();

    int ret = platform_wifi_request_gpio_irq(hal_priv);
    volatile unsigned int reg = 0;
    volatile unsigned int reg5c = 0;

    if (ret != 0)
    {
        AML_PRINT_LOG_ERR("request_gpio ERR!\n");
        return ret;
    }
    else
    {
        //reg = hw_if->hif_ops.hi_read_word(RG_WIFI_IF_GPIO_IRQ_CNF);

        reg5c = hif->hif_ops.hi_read_word(RG_WIFI_IF_FW2HST_CLR);

        AML_PRINT_LOG_INFO("-----------0x5C register=0x%x\n", reg5c);
        reg5c |= BIT(31);

        hif->hif_ops.hi_write_word(RG_WIFI_IF_FW2HST_CLR, reg5c);

        switch (amlhal_gpio.gpio_irq_mode)
        {
            case WIFI_GPIO_IRQ_FALLING:
                reg &= (~SDIO_GPIO_IRQ_TRIG_MODE_LEVEL);
                reg &= (~SDIO_GPIO_IRQ_EDGE_TIMEUP_MASK);
                /* change gpiox7 low power level time to resume host*/
                reg |= 0x3ff;
                break;
            case WIFI_GPIO_IRQ_LOW:
                reg &= (~SDIO_GPIO_IRQ_TRIG_MODE_LEVEL);
                reg |= (SDIO_GPIO_IRQ_EDGE_TIMEUP_MASK);
                break;
            default:
                ret = -1;
                goto err;
        }

        //hw_if->hif_ops.hi_write_word(RG_WIFI_IF_GPIO_IRQ_CNF, reg);

        //reg = hw_if->hif_ops.hi_read_word(RG_WIFI_IF_GPIO_IRQ_CNF);

        AML_PRINT_LOG_INFO("SDIO GPIO IRQ CONFIG REG=0x%x\n",reg);

        aml_sdio_irq_path(1); //  1: GPIO LINE PATH. 0: SDIO DATA LINE PATH
    }

    hal_priv->use_irq = 1;
    return ret;

err:
    platform_wifi_free_gpio_irq(hal_priv);
    return ret;
}

int amlhal_gpio_close(struct hal_private * hal_priv )
{
    if (hal_priv->use_irq)
        hal_priv->use_irq = 0;
    else
        return -1;
    platform_wifi_free_gpio_irq(hal_priv);

    return 0;
}
#endif //USE_GPIO_IRQ
void aml_sdio_interrupt(struct sdio_func * func) {
    struct hal_private *hal_priv = hal_get_priv();
    if (hal_priv->bhalOpen) {
        hal_irq_top(0, hal_priv);
    }

}

extern unsigned char wifi_irq_enable;
void aml_sdio_enable_irq(int func_n)
{
    struct hal_private *hal_priv = hal_get_priv();
    if (!hal_priv->hst_if_irq_en) {
#ifdef USE_SDIO_IRQ
        struct sdio_func *func = aml_priv_to_func(func_n);
        sdio_claim_host(func);
        sdio_claim_irq(func, aml_sdio_interrupt);
        sdio_release_host(func);
        aml_sdio_irq_path(0);
#else
        amlhal_gpio_open(hal_priv);
#endif
        wifi_irq_enable = 1;
        hal_priv->hst_if_irq_en = 1;
    }
}

void aml_sdio_disable_irq(int func_n)
{
    struct hal_private *hal_priv=NULL;
    hal_priv = hal_get_priv();

   if (hal_priv->hst_if_irq_en)
    {
#ifdef USE_SDIO_IRQ
        struct sdio_func *func = aml_priv_to_func(func_n);
        sdio_claim_host(func);
        sdio_release_irq(func);
        sdio_release_host(func);
#else
        amlhal_gpio_close(hal_priv);
#endif
        wifi_irq_enable = 0;
        hal_priv->hst_if_irq_en = 0;
    }
}

void aml_sdio_calibration(void)
{
    struct hw_interface *hif = hif_get_hw_interface();
    int err;
    unsigned char i, j, k, l;
    unsigned char step;

    step = 4;
    hif->hif_ops.hi_bottom_write8(SDIO_FUNC1, 0x2c0, 0);
    for (i = 0; i < 32; i += step) {
        hif->hif_ops.hi_bottom_write8(SDIO_FUNC1, 0x2c2, i);

        for (j = 0; j < 32; j += step) {
            hif->hif_ops.hi_bottom_write8(SDIO_FUNC1, 0x2c3, j);

            for (k = 0; k < 32; k += step) {
                hif->hif_ops.hi_bottom_write8(SDIO_FUNC1, 0x2c4, k);

                for (l = 0; l < 32; l += step) {
                    hif->hif_ops.hi_bottom_write8(SDIO_FUNC1, 0x2c5, l);

                    //msleep(3000);
                    err = hif->hif_ops.hi_write_reg32(RG_SCFG_SRAM_FUNC, l);

                    if (err) {
                        //msleep(3000);
                        hif->hif_ops.hi_bottom_write8(SDIO_FUNC0, SDIO_CCCR_IOABORT, 0x1);
                        AML_PRINT_LOG_INFO("error: i:%d, j:%d, k:%d, l:%d\n", i, j, k, l);

                    } else {
                        AML_PRINT_LOG_INFO("right, use this config: i:%d, j:%d, k:%d, l:%d\n", i, j, k, l);
                        return;
                    }
                }
            }
        }
    }

    hif->hif_ops.hi_bottom_write8(SDIO_FUNC1, 0x2c2, 0);
    hif->hif_ops.hi_bottom_write8(SDIO_FUNC1, 0x2c3, 0);
    hif->hif_ops.hi_bottom_write8(SDIO_FUNC1, 0x2c4, 0);
    hif->hif_ops.hi_bottom_write8(SDIO_FUNC1, 0x2c5, 0);
}

void set_reg_fragment(unsigned int addr,unsigned int bit_end,
        unsigned int bit_start,unsigned int value)
{
    unsigned int tmp;
    unsigned int bitwidth = bit_end - bit_start + 1;
    int max_value = (bitwidth == 32)? -1 : (1 << (bitwidth)) - 1;
    struct hw_interface* hif = hif_get_hw_interface();

    ASSERT((bitwidth > 0)||(bit_start <= 31)||(bit_end <= 31));

    tmp = hif->hif_ops.hi_read_word(addr);
    tmp &= ~(max_value << bit_start); // clear [bit_end: bit_start]
    tmp |= ((value & max_value) << bit_start);

    hif->hif_ops.hi_write_word(addr,tmp);
}

/* aon module address from 0x00f00000, we need read/write by sdio func5 */
void aml_aon_write_reg(unsigned int addr,unsigned int data)
{
    struct hw_interface* hif = hif_get_hw_interface();

    if (!aml_bus_type) {
        hif->hif_ops.bt_hi_write_word((addr), data);
    } else {
        hif->hif_ops.hi_write_word((addr), data);
    }
}

unsigned int aml_aon_read_reg(unsigned int addr)
{
    unsigned int regdata = 0;
    struct hw_interface* hif = hif_get_hw_interface();

    if (!aml_bus_type) {
        regdata = hif->hif_ops.bt_hi_read_word((addr));
    } else {
        regdata = hif->hif_ops.hi_read_word((addr));
    }
    return regdata;
}

/* Customer function to control hw specific wlan gpios */
static void aml_customer_gpio_wlan_ctrl(int onoff)
{
    switch (onoff)
    {
        case WLAN_POWER_OFF:
            AML_PRINT_LOG_INFO("call customer specific GPIO to turn off WL_REG_ON\n");
            break;

        case WLAN_POWER_ON:
            platform_wifi_reset();
            AML_PRINT_LOG_INFO("=========== WLAN placed in POWER ON ========\n");
            break;
    }
}

extern void set_usb_wifi_power(int is_on);
#ifdef SDIO_BUILD_IN
static void config_pmu_reg(bool is_power_on)
{
    int value_pmu_A12 = 0;
    int value_pmu_A13 = 0;
    int value_pmu_A14 = 0;
    int value_pmu_A15 = 0;
    int value_pmu_A17 = 0;
    int value_pmu_A18 = 0;
    int value_pmu_A20 = 0;
    int value_pmu_A22 = 0;

    unsigned char host_req_status;
    unsigned char wifi_pmu_status;

    struct hal_private * halpriv = hal_get_priv();
    struct hw_interface * hif = hif_get_hw_interface();

    wifi_pmu_status = halpriv->hal_ops.hal_get_fw_ps_status();
    AML_PRINT_LOG_INFO("wifi_pmu_status:0x%x\n", wifi_pmu_status);

    if (is_power_on) {
        host_req_status = 0;
        hif->hif_ops.hi_bottom_write8(SDIO_FUNC1, RG_SDIO_PMU_HOST_REQ, host_req_status);

        msleep(10);

        value_pmu_A12 = hif->hif_ops.hi_read_word(RG_PMU_A12);
        value_pmu_A13 = hif->hif_ops.hi_read_word(RG_PMU_A13);
        value_pmu_A14 = hif->hif_ops.hi_read_word(RG_PMU_A14);
        value_pmu_A15 = hif->hif_ops.hi_read_word(RG_PMU_A15);
        value_pmu_A17 = hif->hif_ops.hi_read_word(RG_PMU_A17);
        value_pmu_A18 = hif->hif_ops.hi_read_word(RG_PMU_A18);
        value_pmu_A20 = hif->hif_ops.hi_read_word(RG_PMU_A20);
        value_pmu_A22 = hif->hif_ops.hi_read_word(RG_PMU_A22);

        AML_PRINT_LOG_INFO("power on: before write A12=0x%x, A13=0x%x, A14=0x%x, A15=0x%x, A17=0x%x, A18=0x%x, A20=0x%x, A22=0x%x\n",
            value_pmu_A12, value_pmu_A13, value_pmu_A14, value_pmu_A15,value_pmu_A17,value_pmu_A18,value_pmu_A20,value_pmu_A22);

        hif->hif_ops.hi_write_word(RG_PMU_A12, 0x2a2c);
        hif->hif_ops.hi_write_word(RG_PMU_A14, 0x1);
        hif->hif_ops.hi_write_word(RG_PMU_A17, 0x700);
        hif->hif_ops.hi_write_word(RG_PMU_A20, 0x0);
        hif->hif_ops.hi_write_word(RG_PMU_A18, 0x1700);
        hif->hif_ops.hi_write_word(RG_PMU_A22, 0x704);

        host_req_status = (PMU_PWR_OFF << 1)| BIT(0);
        hif->hif_ops.hi_bottom_write8(SDIO_FUNC1, RG_SDIO_PMU_HOST_REQ, host_req_status);

        host_req_status = 0;
        hif->hif_ops.hi_bottom_write8(SDIO_FUNC1, RG_SDIO_PMU_HOST_REQ, host_req_status);
        msleep(20);

        wifi_pmu_status = halpriv->hal_ops.hal_get_fw_ps_status();



        AML_PRINT_LOG_INFO("wifi_pmu_status:0x%x\n", wifi_pmu_status);

        while ((wifi_pmu_status & 0xF) != PMU_ACT_MODE) {
            AML_PRINT_LOG_INFO("wifi_pmu_status:0x%x\n", wifi_pmu_status);
            wifi_pmu_status = halpriv->hal_ops.hal_get_fw_ps_status();
        }

        value_pmu_A15 = hif->hif_ops.hi_read_word(RG_PMU_A15);
        value_pmu_A17 = hif->hif_ops.hi_read_word(RG_PMU_A17);
        value_pmu_A18 = hif->hif_ops.hi_read_word(RG_PMU_A18);
        value_pmu_A20 = hif->hif_ops.hi_read_word(RG_PMU_A20);
        value_pmu_A22 = hif->hif_ops.hi_read_word(RG_PMU_A22);
        AML_PRINT_LOG_INFO("power on: after write A15=0x%x, A17=0x%x, A18=0x%x, A20=0x%x, A22=0x%x\n",
            value_pmu_A15, value_pmu_A17,value_pmu_A18,value_pmu_A20,value_pmu_A22);

    } else {
        value_pmu_A12 = hif->hif_ops.hi_read_word(RG_PMU_A12);
        value_pmu_A15 = hif->hif_ops.hi_read_word(RG_PMU_A15);
        value_pmu_A17 = hif->hif_ops.hi_read_word(RG_PMU_A17);
        value_pmu_A18 = hif->hif_ops.hi_read_word(RG_PMU_A18);
        value_pmu_A20 = hif->hif_ops.hi_read_word(RG_PMU_A20);
        value_pmu_A22 = hif->hif_ops.hi_read_word(RG_PMU_A22);
        AML_PRINT_LOG_INFO("power off: before write A12=0x%x, A15=0x%x, A17=0x%x, A18=0x%x, A20=0x%x, A22=0x%x\n",
            value_pmu_A12,value_pmu_A15,value_pmu_A17,value_pmu_A18,value_pmu_A20,value_pmu_A22);

        hif->hif_ops.hi_write_word(RG_PMU_A12, 0x282c); //add set dpll_val(bit16) for sdio resp_timeout
        hif->hif_ops.hi_write_word(RG_PMU_A14, 0x1);
        hif->hif_ops.hi_write_word(RG_PMU_A16, 0x0);
        hif->hif_ops.hi_write_word(RG_PMU_A17, 0x700);
        hif->hif_ops.hi_write_word(RG_PMU_A20, 0x0);
        hif->hif_ops.hi_write_word(RG_PMU_A18, 0x1700);
        hif->hif_ops.hi_write_word(RG_PMU_A22, 0x704);

        value_pmu_A12 = hif->hif_ops.hi_read_word(RG_PMU_A12);
        value_pmu_A15 = hif->hif_ops.hi_read_word(RG_PMU_A15);
        value_pmu_A17 = hif->hif_ops.hi_read_word(RG_PMU_A17);
        value_pmu_A18 = hif->hif_ops.hi_read_word(RG_PMU_A18);
        value_pmu_A20 = hif->hif_ops.hi_read_word(RG_PMU_A20);
        value_pmu_A22 = hif->hif_ops.hi_read_word(RG_PMU_A22);
        AML_PRINT_LOG_INFO("power off: before write A12=0x%x, A15=0x%x, A17=0x%x, A18=0x%x, A20=0x%x, A22=0x%x\n",
            value_pmu_A12,value_pmu_A15,value_pmu_A17,value_pmu_A18,value_pmu_A20,value_pmu_A22);

     //force wifi pmu fsm to sleep mode
        host_req_status = (PMU_SLEEP_MODE << 1)| BIT(0);
        hif->hif_ops.hi_bottom_write8(SDIO_FUNC1, RG_SDIO_PMU_HOST_REQ, host_req_status);
    }
}

extern unsigned char set_wifi_bt_sdio_driver_bit(bool is_register, int shift);
extern unsigned char g_sdio_driver_insmoded;
extern unsigned char g_sdio_after_porbe;
extern unsigned char wifi_sdio_access;

int aml_w1_init(void)
{
    int ret = 0;
    struct hw_interface * hif = hif_get_hw_interface();
    struct hal_private * hal_priv = hal_get_priv();
    struct sdio_func *func;

    AML_PRINT_LOG_INFO("SDIO_BUILD_IN \n");
    aml_customer_gpio_wlan_ctrl(WLAN_POWER_ON);

    if (!g_sdio_after_porbe) {
          AML_PRINT_LOG_INFO("sdio not probe, need set power \n");
          set_usb_wifi_power(0);
          msleep(100);
          set_usb_wifi_power(1);
          msleep(200);
    }

    if (!g_sdio_driver_insmoded) {
        aml_sdio_init();
        msleep(200);
    }

    if (!g_sdio_after_porbe) {
        AML_PRINT_LOG_ERR("can't probe sdio!\n");
        //aml_w1_sdio_exit();
        return -ENODEV;
    }

    func = aml_priv_to_func(SDIO_FUNC7);
    set_wifi_bt_sdio_driver_bit(AML_W1_WIFI_POWER_ON, WIFI_POWER_CHANGE_SHIFT);

    tx_status_list_init(&(hif->tx_status_list), WIFI_MAX_TXFRAME*2);
    skb_queue_head_init(&hif->bcn_list_head);

    ret = hal_init_priv();
    if (ret != 0)
        goto create_thread_error;

    ret = hal_create_thread();
    if (ret != 0)
        goto create_thread_error;

    /*set parent dev for net dev. */
    vm_cfg80211_set_parent_dev(&func->dev);
    if (!(hif->hif_ops.hi_bottom_write8)) {
        AML_PRINT_LOG_ERR("not found w1 wifi\n");
        goto create_thread_error;
    }
    aml_sdio_calibration();
    config_pmu_reg(AML_W1_WIFI_POWER_ON);

    if ((hal_priv->hal_call_back != NULL)
        &&(hal_priv->hal_call_back->dev_probe != NULL))
    {
        AML_PRINT_LOG_INFO("hal_priv->hal_call_back->dev_probe\n");
        /*call driver probe to create vmac0 and vmac1 eventually*/
        ret = hal_priv->hal_call_back->dev_probe();
        if (ret < 0)
        {
            goto create_thread_error;
        }
    }
    hal_priv->powersave_init_flag = 0;

    AML_PRINT_LOG_INFO("sg ops init\n");
    hif->hif_ops.hi_enable_scat();

    aml_sdio_enable_irq(SDIO_FUNC1);

    AML_PRINT_LOG_INFO("aml_sdio_probe-- ret %d\n", ret);

    if (aml_wifi_is_enable_rf_test()) {
        mib_init();
        driver_open();
    }

    return ret;

create_thread_error:
    hal_kill_thread();
    return ret;
}


extern unsigned char recovery_notify_bt;
extern unsigned char recovery_done;
void aml_sdio_disable_wifi(void)
{
    unsigned char bt_alive = 0;
    wifi_sdio_access = 0;
    AML_PRINT_LOG_INFO("wifi_sdio_access:%d, chip_en_access:%d\n", wifi_sdio_access, chip_en_access);

    //if (chip_en_access) {
    if (wifi_mac_need_chip_reset()) {
        recovery_notify_bt = 1;
        recovery_done = 0;

        /*1 remove sdio drvier*/
        bt_alive = (g_sdio_wifi_bt_alive & BIT(0))? 1:0;
        set_wifi_bt_sdio_driver_bit(0, BT_POWER_CHANGE_SHIFT);
        set_wifi_bt_sdio_driver_bit(0, WIFI_POWER_CHANGE_SHIFT);
        while (g_sdio_driver_insmoded == 1) {
            msleep(100);
        }

        /*2 chip en off and detect sdio card, disable GPIO/SDIO irq*/
        aml_sdio_disable_irq(SDIO_FUNC1);
        msleep(20);
        set_usb_bt_power(0);
        msleep(20);
        set_usb_wifi_power(0);
        msleep(20);

        /*3 chip en on and detect sdio card*/
        if (bt_alive) {
            set_usb_bt_power(1);
            msleep(20);
        }
        set_usb_wifi_power(1);
        msleep(200);

        /*4 restore sdio flag for BT and WiFi*/
        if (bt_alive) {
            set_wifi_bt_sdio_driver_bit(1, BT_POWER_CHANGE_SHIFT);
        }
        set_wifi_bt_sdio_driver_bit(1, WIFI_POWER_CHANGE_SHIFT);

        /*5 insmod sdio drvier*/
        aml_sdio_init();
        while (g_sdio_driver_insmoded == 0) {
            msleep(100);
        }

    } else {
        aml_sdio_disable_irq(SDIO_FUNC1);
        config_pmu_reg(AML_W1_WIFI_POWER_OFF);
        aml_sdio_disable_irq(SDIO_FUNC1);
        msleep(50);
    }
}



void aml_sdio_enable_wifi(void)
{
    aml_customer_gpio_wlan_ctrl(WLAN_POWER_ON);
    hal_recovery_init_priv();
    config_pmu_reg(AML_W1_WIFI_POWER_ON);
    AML_PRINT_LOG_INFO("aml_sdio_enable_wifi start sdio access %d\n", wifi_sdio_access);
    wifi_sdio_access = 1;
    hal_fw_repair();
    recovery_done = 1;
}


void aml_w1_exit(void) {
    struct hal_private *hal_priv = hal_get_priv();

    config_pmu_reg(AML_W1_WIFI_POWER_OFF);
    hal_ops_detach();
    aml_customer_gpio_wlan_ctrl(WLAN_POWER_OFF);

    aml_sdio_disable_irq(SDIO_FUNC1);

    hal_priv->hst_if_irq_en = 0;
    hal_priv->powersave_init_flag = 1;
    hal_free();

    set_wifi_bt_sdio_driver_bit(AML_W1_WIFI_POWER_OFF, WIFI_POWER_CHANGE_SHIFT);

    set_usb_wifi_power(0);
    if (aml_wifi_is_enable_rf_test())
        b2b_tx_thread_remove();

    vm_cfg80211_clear_parent_dev();
}

int aml_sdio_pm_suspend(struct device *device)
{
#if defined (HAL_FPGA_VER)
    mmc_pm_flag_t flags;
    struct sdio_func *func = NULL;
    struct hal_private * hal_priv = hal_get_priv();
    int ret = 0, cnt = 0;

    func = dev_to_sdio_func(device);

    while (atomic_read(&hal_priv->drv_suspend_cnt) == 0)
    {
        msleep(50);
        cnt++;
        if (cnt > 20)
        {
            AML_PRINT_LOG_ERR("wifi suspend fail \n");
            return -1;
        }
    }

    atomic_inc(&hal_priv->sdio_suspend_cnt);
    //AML_PRINT_LOG_INFO("wifi suspend %d, suspend cnt 0x%x, func num %d \n",
    //    hal_priv->hal_suspend_mode, atomic_read(&hal_priv->suspend_cnt) , func->num);

    /* we shall suspend all card for sdio. */
    {
        flags = sdio_get_host_pm_caps(func);

        if ((flags & MMC_PM_KEEP_POWER) != 0)
            ret = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);

        if (ret != 0)
        {
            AML_PRINT_LOG_ERR("host can't keep power while suspend \n");
            return -1;
        }

        /*
         * if we don't use sdio irq, we can't get functions' capability with
         * MMC_PM_WAKE_SDIO_IRQ, so we don't need set irq for wake up
         * sdio for upcoming suspend.
         */
        if ((flags & MMC_PM_WAKE_SDIO_IRQ) != 0)
            ret = sdio_set_host_pm_flags(func, MMC_PM_WAKE_SDIO_IRQ);

        if (ret != 0)
        {
            AML_PRINT_LOG_ERR("host can't set irq while suspend \n");
            return -1;
        }
    }
    return ret;
#elif defined (HAL_SIM_VER)
    return 0;
#endif

    return 0;
}

int aml_sdio_pm_resume(struct device *device)
{
#if defined (HAL_FPGA_VER)
    struct sdio_func *func = NULL;
    struct hal_private * hal_priv = hal_get_priv();

    func = dev_to_sdio_func(device);
//    if (func == NULL)
//        return -1;

    atomic_dec(&hal_priv->sdio_suspend_cnt);
    //AML_PRINT_LOG_INFO("func num %d, no resume 0x%x \n",
    //        func->num, atomic_read(&hal_priv->suspend_cnt));

    return 0;
#else
    return 0;
#endif
}

#else//SDIO_BUILD_IN

int aml_sdio_probe(struct sdio_func *func, const struct sdio_device_id *id)
{
    int ret = 0;
    static struct sdio_func sdio_func_0;
    struct hw_interface * hif = hif_get_hw_interface();
    struct hal_private * hal_priv = hal_get_priv();
//add by guanghua.lai
    struct amlw_hwif_sdio * hwif_sdio = aml_sdio_priv();

    sdio_claim_host(func);
    ret = sdio_enable_func(func);
    if (ret)
        goto sdio_enable_error;

    if (func->num == 4)
        sdio_set_block_size(func, FUNC4_BLKSIZE);
    else
        sdio_set_block_size(func, BLKSIZE);

    AML_PRINT_LOG_INFO("func->num %d sdio block size=%d, \n",
        func->num,  func->cur_blksize);

    if (func->num == 1)
    {
        sdio_func_0.num = 0;
        sdio_func_0.card = func->card;
        hwif_sdio->sdio_func_if[0] = &sdio_func_0;
    }
    hwif_sdio->sdio_func_if[func->num] = func;
    AML_PRINT_LOG_INFO("func->num %d sdio_func=%p, \n",
        func->num,  func);

    sdio_release_host(func);
    sdio_set_drvdata(func, hwif_sdio);
    if (func->num != FUNCNUM_SDIO_LAST)
    {
        AML_PRINT_LOG_INFO("func_num=%d, lastfuncnum=%d\n",
            func->num, FUNCNUM_SDIO_LAST);
        return 0;
    }

    tx_status_list_init(&(hif->tx_status_list), WIFI_MAX_TXFRAME*2);
    skb_queue_head_init(&hif->bcn_list_head);

    ret = hal_init_priv();
    if (ret != 0)
        goto create_thread_error;

    ret = hal_create_thread();
    if (ret != 0)
        goto create_thread_error;

    /*set parent dev for net dev. */
    vm_cfg80211_set_parent_dev(&func->dev);

    aml_sdio_calibration();

    if ((hal_priv->hal_call_back != NULL)
        &&(hal_priv->hal_call_back->dev_probe != NULL))
    {
        AML_PRINT_LOG_INFO("hal_priv->hal_call_back->dev_probe\n");
        /*call driver probe to create vmac0 and vmac1 eventually*/
        ret = hal_priv->hal_call_back->dev_probe();
        if (ret < 0)
        {
            goto create_thread_error;
        }
    }
    hal_priv->powersave_init_flag = 0;

    AML_PRINT_LOG_INFO("sg ops init\n");
    hif->hif_ops.hi_enable_scat();

    aml_sdio_enable_irq(SDIO_FUNC1);

    AML_PRINT_LOG_INFO("aml_sdio_probe-- ret %d\n", ret);


    if (aml_wifi_is_enable_rf_test()) {
        mib_init();
        driver_open();
    }

    return ret;

create_thread_error:
    hal_kill_thread();
    sdio_claim_host(func);
    sdio_disable_func(func);

sdio_enable_error:
    AML_PRINT_LOG_ERR("sdio_enable_error\n");
    sdio_release_host(func);
    return ret;
}

static void  aml_sdio_remove(struct sdio_func *func)
{
    struct hal_private * hal_priv = hal_get_priv();

    if (func== NULL)
    {
        return ;
    }

    AML_PRINT_LOG_INFO("\n==========================================\n");
    AML_PRINT_LOG_INFO("aml_sdio_remove++ func->num =%d \n",func->num);
    AML_PRINT_LOG_INFO("==========================================\n");
    aml_sdio_disable_irq(SDIO_FUNC1);
    hal_priv->powersave_init_flag = 1;
    hal_free();
    if (func->num == FUNCNUM_SDIO_LAST)
    {
        hal_kill_thread();
        if (aml_wifi_is_enable_rf_test())
            b2b_tx_thread_remove();

        vm_cfg80211_clear_parent_dev();
    }
    sdio_claim_host(func);
    sdio_disable_func(func);
    sdio_release_host(func);
}

static int aml_sdio_pm_suspend(struct device *device)
{
    return 0;
}

static int aml_sdio_pm_resume(struct device *device)
{
    return 0;
}

static SIMPLE_DEV_PM_OPS(aml_sdio_pm_ops, aml_sdio_pm_suspend,
             aml_sdio_pm_resume);

static const struct sdio_device_id aml_devices[] =
{
    {SDIO_DEVICE(VENDOR_AMLOGIC,PRODUCT_AMLOGIC) },
    {SDIO_DEVICE(VENDOR_AMLOGIC_EFUSE,PRODUCT_AMLOGIC_EFUSE)},
    {SDIO_DEVICE(W1u_VENDOR_AMLOGIC_EFUSE,W1us_PRODUCT_AMLOGIC_EFUSE)},
    {SDIO_DEVICE(W1u_VENDOR_AMLOGIC_EFUSE,W1us_A_PRODUCT_AMLOGIC_EFUSE)},
    {SDIO_DEVICE(W1u_VENDOR_AMLOGIC_EFUSE,W1us_B_PRODUCT_AMLOGIC_EFUSE)},
    {SDIO_DEVICE(W1u_VENDOR_AMLOGIC_EFUSE,W1us_C_PRODUCT_AMLOGIC_EFUSE)},
    {}
};

MODULE_DEVICE_TABLE(sdio, aml_devices);

static struct sdio_driver aml_sdio_driver =
{
    .name = "aml_sdio",
    .id_table = aml_devices,
    .probe = aml_sdio_probe,
    .remove = aml_sdio_remove,
    .drv.pm = &aml_sdio_pm_ops,
};

int  aml_w1_init(void)
{
    int err = 0;

    amlwifi_set_sdio_host_clk(sdioclk);

    set_usb_wifi_power(0);
    msleep(500);
    set_usb_wifi_power(1);
    msleep(500);

    aml_customer_gpio_wlan_ctrl(WLAN_POWER_ON);
    sdio_reinit();

    err = sdio_register_driver(&aml_sdio_driver);
    if (err)
        AML_PRINT_LOG_ERR("failed to register sdio driver: %d \n", err);

    return err;
}

void  aml_w1_exit(void)
{
    struct hw_interface * hif = hif_get_hw_interface();

    hif->hif_ops.hi_write_word(RG_PMU_A22, 0x704);
    hif->hif_ops.hi_write_word(RG_PMU_A16, 0x0);

    PRINT("aml_sdio_exit++ \n");
    sdio_unregister_driver(&aml_sdio_driver);
    PRINT("*****************amlwifi unloaded ok********************\n");

    hal_ops_detach();
    set_usb_wifi_power(0);
    aml_customer_gpio_wlan_ctrl(WLAN_POWER_OFF);
}
#endif//SDIO_BUILD_IN

#elif defined (HAL_SIM_VER)
/* queue a read/write request */
unsigned short aml_sdio_read_write(struct  hw_interface * hif, SYS_TYPE addr,
    unsigned char *buf, SYS_TYPE len, unsigned char  func, unsigned char rw_flag,unsigned char  flag)
{
        int ret = CO_FAIL;
        struct sdio_rw_desc * pDesc;
        struct sdio_syn_desc syn_rw_desc;
        unsigned long flags;

        COMMON_LOCK();
        pDesc = &syn_rw_desc.desc;
        pDesc->addr = addr;
        pDesc->buf = buf;

        pDesc->len =len;
        pDesc->func = func;
        pDesc->flag = flag;
        pDesc->rw_flag = rw_flag;
        syn_rw_desc.b_inused = 1;
        COMMON_LOCK();
        pDesc->status = _sdio_read_write(hif,pDesc);
        ret = pDesc->status;
    return ret;
}

int aml_sdio_read_reg(struct hw_interface * hif, unsigned char func_num,
    int addr, unsigned char *buf, size_t len)
{
        int ret;
        ret = sdio_read_reg(func_num,addr,(unsigned char *)buf);///////////////////////
        if (ret)
                PRINT("sdio read reg failed (%d)\n",ret);
        return ret;
}

int aml_sdio_write_reg(struct hw_interface * hif, unsigned char func_num,
    int addr, unsigned char *buf, size_t len)
{
        int ret;
        ret = sdio_write_reg(func_num,addr,(unsigned char *)buf,0);////////////////////////////
        if (ret)
                PRINT("sdio write reg failed (%d)\n",ret);
        return ret;
}

int aml_sdio_read(struct hw_interface * hif, unsigned char func_num, int addr,
    void *buf, size_t len, int incr_addr)
{
        int ret;
        ret = sdio_read_data(func_num,incr_addr,addr,len,(unsigned char *)buf);
        if (ret)
                PRINT("sdio read failed (%d)\n",ret);
        return ret;
}

int aml_sdio_write(struct hw_interface * hif, unsigned char func_num, int addr,
    void *buf, size_t len, int incr_addr)
{
        int ret;
        ret = sdio_write_data(func_num,incr_addr,addr,len,(unsigned char *)buf);
        if (ret)
                PRINT("sdio write failed (%d)\n",ret);
        return ret;
}

int _sdio_read_write(struct hw_interface * hif, struct sdio_rw_desc * pDesc)
{
        int ret = -1;
        unsigned char *tbuffer;
        int data;
        int i,j = 0;
        int srambuf;

        ASSERT(hif != NULL);
_restartsdio:
        if (pDesc->rw_flag == SDIO_RW_FLAG_READ)
        {
           switch (pDesc->func)
           {
               case SDIO_FUNC1:
                    tbuffer = (unsigned char *)pDesc->buf;
                    ret = aml_sdio_read_reg(hif, SDIO_FUNC1,pDesc->addr&SDIO_ADDR_MASK,
                                    tbuffer,pDesc->len);
                    break;
                case SDIO_FUNC2:
                case SDIO_FUNC6:
                         tbuffer = (unsigned char *)j;
                         ret = aml_sdio_read(hif, pDesc->func, pDesc->addr&SDIO_ADDR_MASK,
                                tbuffer, ALIGN(pDesc->len,4), 1);
                         for (i = 0; i < pDesc->len/4; i++)
                         {
                                 hostSram_access(FW_ID,0,j,0,(int*)(pDesc->buf+j));
                                 j+=4;
                         }
                    break;
                case SDIO_FUNC4:
                    tbuffer = (unsigned char *)j;//pDesc->buf;
                    ret = aml_sdio_read(hif,SDIO_FUNC4,pDesc->addr&SDIO_ADDR_MASK,tbuffer,
                                    ALIGN(pDesc->len,hif->CommStaticParam.tx_page_len),0);
                    for ( i = 0;i < ALIGN(pDesc->len,hif->CommStaticParam.tx_page_len)/4;i++)
                    {
                        hostSram_access(FW_ID,0,j,0,(int*)(pDesc->buf+j));
                        j+=4;
                    }
                    break;
                case SDIO_FUNC5:
                    tbuffer = (unsigned char *)j;
                    pDesc->len = ALIGN(pDesc->len,4);
                    ret = aml_sdio_read(hif,SDIO_FUNC5,pDesc->addr&SDIO_ADDR_MASK,tbuffer,
                                ALIGN(pDesc->len,4),1);
                    for ( i = 0;i < pDesc->len/4;i++)
                    {
                        hostSram_access(FW_ID,0,j,0,(int*)(pDesc->buf+j));
                        j+=4;
                    }
                    break;
                case SDIO_FUNCNUM_MAX:
                    tbuffer = (unsigned char *)pDesc->buf;
                    ret = aml_sdio_write_reg(hif,1,pDesc->addr&SDIO_ADDR_MASK,tbuffer,pDesc->len);
                    break;
                default:
                    break;
           }
        }
        else if (pDesc->rw_flag == SDIO_RW_FLAG_WRITE)
        {
            switch (pDesc->func)
            {
                case SDIO_FUNC1:
                    tbuffer = (unsigned char *)pDesc->buf;
                    ret = aml_sdio_write_reg(hif,SDIO_FUNC1,pDesc->addr&SDIO_ADDR_MASK,
                                tbuffer,pDesc->len);
                    break;
                case SDIO_FUNC2:
                    tbuffer = (unsigned char *)j;
                    pDesc->len = ALIGN(pDesc->len,4);
                    for ( i = 0;i < pDesc->len/4;i++) {
                            hostSram_access(FW_ID,1,j,*(unsigned int*)(pDesc->buf+j),(int*)&data);
                            j+=4;
                    }
                    j=0;
                    for ( i = 0;i < pDesc->len/4;i++) {
                            hostSram_access(FW_ID,0,j,0,(int*)&srambuf);
                            j+=4;
                    }
                    ret = aml_sdio_write(hif,SDIO_FUNC2,pDesc->addr&SDIO_ADDR_MASK,tbuffer,
                                ALIGN(pDesc->len,4),1);
                    break;
                 case SDIO_FUNC3:
                    tbuffer = (unsigned char *)j;
                    for ( i = 0;i < pDesc->len/4;i++) {
                            hostSram_access(FW_ID,1,j,*(unsigned int*)(pDesc->buf+j),(int*)&data);
                            j+=4;
                    }
                    hostSram_access(FW_ID,0,j,0,(int*)&srambuf);
                    ret = aml_sdio_write(hif,SDIO_FUNC3,pDesc->addr&SDIO_ADDR_MASK,tbuffer,
                                ALIGN(pDesc->len,4),1);
                    break;

                case SDIO_FUNC4:
                    tbuffer = (unsigned char *)j;
                    for ( i = 0;i < ALIGN(pDesc->len,hif->CommStaticParam.tx_page_len)/4;i++)
                    {
                        hostSram_access(FW_ID,1,j,*(unsigned int*)(pDesc->buf+j),(int*)&data);
                        j+=4;
                    }
                    ret = aml_sdio_write(hif,SDIO_FUNC4,pDesc->addr&SDIO_ADDR_MASK,tbuffer,
                                ALIGN(pDesc->len,hif->CommStaticParam.tx_page_len),0);
                    break;
                case SDIO_FUNC5:
                    tbuffer = (unsigned char *)j;
                    pDesc->len = ALIGN(pDesc->len,4);
                    for ( i = 0;i < pDesc->len/4;i++)
                    {
                        hostSram_access(FW_ID,1,j,*(unsigned int*)(pDesc->buf+j),(int*)&data);
                        j+=4;
                    }
                    j=0;
                    for ( i = 0;i < pDesc->len/4;i++)
                    {
                        hostSram_access(FW_ID,0,j,0,(int*)&srambuf);
                        j+=4;
                    }
                    ret = aml_sdio_write(hif,SDIO_FUNC5,pDesc->addr&SDIO_ADDR_MASK,tbuffer,
                                    ALIGN(pDesc->len,4),1);
                    break;
                case SDIO_FUNCNUM_MAX:
                    tbuffer = (unsigned char *)pDesc->buf;
                    ret = aml_sdio_write_reg(hif,1,pDesc->addr&SDIO_ADDR_MASK,tbuffer,pDesc->len);
                    break;
                default:
                    break;
            }
        }
        else{
           PRINT("%s(%d) Error: sdio function read/write flag:%d\n",__func__,__LINE__,pDesc->rw_flag);
        }

        if (ret) {
                PRINT("_sdio_read_write error!!! function = %x, addr = %x, len = %x\n",pDesc->func, pDesc->addr,pDesc->len);
                sv_delay(FW_ID,100);//msleep(100);
        }
        return ret;
}


#ifdef HAL_SIM_VER
#ifdef FW_NAME
}
#endif
#endif

#endif /*end HAL_FPGA_VER */


