#ifdef HAL_SIM_VER
#ifdef FW_NAME
namespace FW_NAME
{
#endif
#endif

#include "wifi_hal_com.h"
#include "wifi_hal_platform.h"
#include "wifi_hif.h"
#include "wifi_common.h"
#include "version.h"
#include "wifi_drv_reg_ops.h"
#include "wifi_usb.h"
#include "chip_intf_reg.h"
#if defined (HAL_FPGA_VER)
#include <linux/amlogic/aml_gpio_consumer.h>
#include "wifi_mac_com.h"
#include <linux/delay.h>
#endif

#if defined (HAL_FPGA_VER)
struct platform_wifi_gpio amlhal_gpio =
{

#ifdef  CONFIG_GPIO_RESET
    .gpio_reset = GPIOX_6,//234,//GPIOX_6
#endif

#ifndef USE_SDIO_IRQ
    .gpio_irq = GPIOX_7, //235, //GPIOX_7
    .gpio_irq_mode = WIFI_GPIO_IRQ_LOW,
    .irq_num = 97,
    .filter_num = FILTER_NUM4,
    .clk_sel = GPIOX_20,//GPIOY_15,//GPIOX_19,
#endif

#ifdef CONFIG_RTC_ENABLE
    .gpio_rtc = GPIOX_10,
#endif

};
extern int wifi_irq_trigger_level(void);
extern int wifi_irq_num(void);
#ifndef CONFIG_USB_CLOSE
extern struct urb *g_urb;
extern unsigned char *g_buffer;
void aml_usb_ctlread_complete(struct urb *urb)
{
    struct hal_private *hal_priv = hal_get_priv();
    static unsigned int print_cnt = 0;

    if (hal_priv->bhalOpen == 0) {
        hi_supplement_usb_buffer();
        if (!(print_cnt++ % 1000)) {
            AML_PRINT_LOG_INFO("aml_usb_ctlread_complete, bhalOpen:%d\n", hal_priv->bhalOpen);
        }
        return;
    }
#if defined (HAL_FPGA_VER)
    hal_priv->gpio_irq_cnt++;
    hal_priv->int_status_copy = (g_buffer[3]<<24)|(g_buffer[2]<<16)|(g_buffer[1]<<8)|g_buffer[0];
    atomic_set(&hal_priv->usb_isr_done, 1);
    up(&hal_priv->hi_irq_thread_sem);
#elif defined (HAL_SIM_VER)
    OS_SCHEDULE_TQUEUE(&hal_priv->hi_tasktq);
#endif
}
#endif
#ifndef USE_SDIO_IRQ
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,29)
int platform_wifi_request_gpio_irq (void *data)
{
    unsigned int irq_num = wifi_irq_num();
    int ret = -1;
    int irq_trigger_type, irq_flag;

    irq_flag = IRQF_DISABLED;
    amlhal_gpio.gpio_irq_mode = wifi_irq_trigger_level();
    amlhal_gpio.irq_num = irq_num;
    switch (amlhal_gpio.gpio_irq_mode)
    {
        case WIFI_GPIO_IRQ_FALLING:
            irq_trigger_type = GPIO_IRQ_FALLING;
            break;
        case WIFI_GPIO_IRQ_RISING:
            irq_trigger_type = GPIO_IRQ_RISING;
            break;
        case WIFI_GPIO_IRQ_HIGH:
            irq_trigger_type = GPIO_IRQ_HIGH;
            break;
        case WIFI_GPIO_IRQ_LOW:
            irq_trigger_type = GPIO_IRQ_LOW;
            break;
        default:
            ret = -1;
            goto exit;
    }
    do
    {
        irq_flag = AML_GPIO_IRQ(irq_num, amlhal_gpio.filter_num, irq_trigger_type);
        msleep(10);
        AML_PRINT_LOG_INFO("irq_num %d irq_trigger_type %d irq_flag 0x%08x\n",irq_num,irq_trigger_type,irq_flag);
        ret  = request_irq(irq_num, hal_irq_top, irq_flag, "WIFI_INT", data);
        if (ret < 0)
            AML_PRINT(AML_LOG_ID_LOG,AML_LOG_LEVEL_ERROR," request_irq err, ret=%d\n", ret);
    }
    while (0);
exit:
    return ret;
}

#else

int platform_wifi_request_gpio_irq (void *data)
{
    unsigned int irq_num = 0;
    int ret = -1;
    unsigned int irq_flag = 0;

    irq_num = wifi_irq_num();

    if (1)
        irq_flag = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL | IORESOURCE_IRQ_SHAREABLE;
    else
        irq_flag = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL | IORESOURCE_IRQ_SHAREABLE;

    AML_PRINT(AML_LOG_ID_LOG, AML_LOG_LEVEL_DEBUG, " irq_flag=0x%x  irq=%d\n",
        irq_flag,  irq_num);

    do
    {
        ret  = request_irq(irq_num, hal_irq_top, irq_flag, "WIFI_INT", data);
        if (ret < 0)
            AML_PRINT(AML_LOG_ID_LOG, AML_LOG_LEVEL_ERROR," request_irq err, ret=%d\n", ret);
    }
    while (0);

    /* for free irq num */
    amlhal_gpio.irq_num = irq_num;
    return ret;
}
#endif

void platform_wifi_free_gpio_irq (void *data)
{
    AML_PRINT(AML_LOG_ID_ANDROID, AML_LOG_LEVEL_DEBUG," \n");

    disable_irq(amlhal_gpio.irq_num);
    free_irq(amlhal_gpio.irq_num,data);
    gpio_free(amlhal_gpio.gpio_irq);
#ifdef CONFIG_RTC_ENABLE
    amlogic_gpio_free(amlhal_gpio.gpio_rtc, OWNER_NAME);
#endif
}
#endif

#ifdef CONFIG_GPIO_RESET
static void clk_source_select(int IS_SSV_CLK)
{
    int gpio_clk_sel = amlhal_gpio.clk_sel;
    int ret = 0;
    int gpio = 0;

    ret = gpio_request(gpio_clk_sel,  OWNER_NAME);
    if (ret < 0){
        gpio_free(gpio_clk_sel);
        ret = gpio_request(gpio_clk_sel,  OWNER_NAME); //retry
        AML_PRINT_LOG_INFO("again request gpio_clk_sel ret=%d\n", ret);
    }

    if(IS_SSV_CLK){
        ret = gpio_direction_output(gpio_clk_sel, 1);
        gpio = gpio_get_value(gpio_clk_sel);
        AML_PRINT_LOG_INFO("gpio_clk_sel set 1, now=%d##########\n", gpio);
    }
    else{
        ret = gpio_direction_output(gpio_clk_sel, 0);
        gpio = gpio_get_value(gpio_clk_sel);
        AML_PRINT_LOG_INFO("gpio_clk_sel set 0, now=%d##########\n",  gpio);
    }

    gpio_free(gpio_clk_sel);

}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,29)
static void reset_wifi (void)
{
    /* use gpio reset */
    int ret = 0;
    int gpio_wifi_reset = amlhal_gpio.gpio_reset;
    int gpio = 0;

    ret = gpio_request(gpio_wifi_reset,  OWNER_NAME);
    if (ret < 0){
        gpio_free(gpio_wifi_reset);
        ret = gpio_request(gpio_wifi_reset,  OWNER_NAME); //retry
        AML_PRINT_LOG_ERR("request gpio fail ret=%d\n", ret);
    }

    ret = gpio_direction_output(gpio_wifi_reset, 1);
    OS_MDELAY(20);

    ret = gpio_direction_output(gpio_wifi_reset, 0);
    OS_MDELAY(20);
    gpio = gpio_get_value(gpio_wifi_reset);
    AML_PRINT_LOG_INFO("######gpio_wifi_reset set 0, now=%d#########\n", gpio);

    ret = gpio_direction_output(gpio_wifi_reset, 1);
    gpio = gpio_get_value(gpio_wifi_reset);
    AML_PRINT_LOG_INFO("######gpio_wifi_reset set 1, now=%d#########\n", gpio);

    gpio_free(gpio_wifi_reset);
}
#endif

#endif

void platform_wifi_reset_cpu(void)
{
}


void platform_wifi_clk_source_sel(int is_ssv_clk)
{
#ifdef CONFIG_GPIO_RESET
    clk_source_select(is_ssv_clk);
#endif
}

void platform_wifi_reset (void)
{
#ifdef CONFIG_GPIO_RESET
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,29)
    reset_wifi();
    platform_wifi_clk_source_sel(0);
#else
    //reset_wifi();
    //platform_wifi_clk_source_sel(0);
#endif
#endif
}

static inline void set_clk(unsigned int reg, unsigned int data)
{
    struct hw_interface* hif = hif_get_hw_interface();

    hif->hif_ops.hi_write_word(reg, data);

    OS_UDELAY(SWITCH_CLK_WAIT_US);
}

void hi_change_sram_concurrent_mode(void)
{
    unsigned int regdata = 0;
    struct hw_interface* hif = hif_get_hw_interface();

#ifdef SRAM_CONCURRENT
#if  (SRAM_16KMODE == 0)
    //2014 3.4 later FPGA version,need to add the register,because SRAM is used both wifi mac and test bus

    regdata = hif->hif_ops.hi_read_word(RG_INTF_RTC_CLK_CTRL);
    regdata &= ~BIT(31);
    hif->hif_ops.hi_write_word(RG_INTF_RTC_CLK_CTRL, regdata);

    PRINT("++++++SRAM 64K++++++++++ \n");
#else //#if  (SRAM_16KMODE ==0)

    regdata = hif->hif_ops.hi_read_word(RG_INTF_RTC_CLK_CTRL);
    regdata |= BIT(31);
    hif->hif_ops.hi_write_word(RG_INTF_RTC_CLK_CTRL, regdata);

    PRINT("++++++SRAM 16K++++++++++ \n");
#endif  //SRAM_CONCURRENT
#endif
}

void set_wifi_baudrate (unsigned int apb_clk)
{
    unsigned int data, uart_div;
    unsigned int wifi_dig_timebase;
    struct hw_interface* hif = hif_get_hw_interface();

    //set uart baudrate
    uart_div = apb_clk/(UART_BAUD_RATE*4)-1;
    PRINT("for uart baudrate0x%x\n",apb_clk);

    data = hif->hif_ops.hi_read_word(RG_UART_WORK_MODE);
    wifi_dig_timebase =  hif->hif_ops.hi_read_word(RG_INTF_CTRL_CLK);

    PRINT("uart mode 0x%x=0x%x\n", RG_UART_WORK_MODE, data);
    data &= (~(0xfff<<0)); //baudrate, bit0~11
    data |= uart_div;
    hif->hif_ops.hi_write_word(RG_UART_WORK_MODE, data);

    PRINT("set uart baudrate, apb_clk=%d, addr=0x%08x data=0x%08x\n",
          apb_clk, RG_UART_WORK_MODE, data);
}

/*
All valid bits are R/W, the default value is 32h8000_0003
[0]: soft reset 0, which reset the whole MAC. Its set by software,auto cleaned by
    hardware when soft reset timer0 times out. Active low.
[1]: soft reset 1, which reset MAC except sd2wifi sub-module.
Its set by software, auto cleaned by hardware when soft reset timer1 times out. Active low.
[30:2]: reserved
[31]: soft_arc_reset, which reset ARC625 core and arc_11n_ram_loader sub-module.
    Its set and cleaned both by software. Active low.
*/
int amlhal_resetmac(void)
{
#if 0
    struct hal_private *hal_priv;
    int sdio_kptr_val = 0;
    unsigned int to_sdio = 0;
    struct hw_interface   *hif = hif_get_hw_interface();
    hal_priv = hal_get_priv();

    PRINT("%s++\n", __func__);

    to_sdio = 0x1f;
    hif->hif_ops.hi_write_word(RG_WIFI_RST_TIMER0, to_sdio);
    hif->hif_ops.hi_write_word(RG_WIFI_RST_TIMER1, to_sdio);

    to_sdio = (unsigned int)~(BIT(1));
    hif->hif_ops.hi_write_word(RG_WIFI_RST_CTRL, to_sdio);

    sdio_kptr_val = hif->hif_ops.hi_read_reg8(RG_SCFG_RX_EN);
    AML_PRINT_LOG_INFO("reg0x74 0x%x\n", sdio_kptr_val);
    sdio_kptr_val &= ~(BIT(2) | BIT(3)); //keep pointer
    hif->hif_ops.hi_write_reg8(RG_SCFG_RX_EN, sdio_kptr_val);
    sdio_kptr_val =hif->hif_ops.hi_read_reg8(RG_SCFG_RX_EN);
    AML_PRINT_LOG_INFO("reg0x74 0x%x\n", sdio_kptr_val);

    hif->hif_ops.hi_write_reg8(RG_SCFG_SELECT_RST,1);
    // reg_tmp = 0;
    msleep(1);
    hif->hif_ops.hi_write_reg8(RG_SCFG_SELECT_RST,0);
    msleep(100);
    PRINT("%s--\n", __func__);
#endif
    return 0;
}

int amlhal_resetsdio(void)
{
#if 0
    struct hal_private *hal_priv;
    unsigned int to_sdio = 0;
    struct hw_interface   *hif = hif_get_hw_interface();

    hal_priv = hal_get_priv();
    PRINT("amlhal_resetsdio++\n");

    to_sdio = 0x1f;
    hif->hif_ops.hi_write_word(RG_WIFI_RST_TIMER0, to_sdio);
    hif->hif_ops.hi_write_word(RG_WIFI_RST_TIMER1, to_sdio);

    //reset when rmmod, so reset mac and sdio
    to_sdio = (unsigned int)~(BIT(0));
    hif->hif_ops.hi_write_word(RG_WIFI_RST_CTRL, to_sdio);

    PRINT("amlhal_resetsdio--\n");
#endif
    return 0;
}
#endif

#if defined (HAL_SIM_VER)
unsigned char hal_set_sys_clk_Core(unsigned int addr, unsigned int value)
{
    unsigned int tmp,tmp1;
//divide config for clock
    tmp=MAC_RD_REG(addr);
    tmp=(tmp&~(0xffffffff))|(value);
    MAC_WR_REG(addr,tmp);
    OS_MDELAY(10);
//switch clock from osc to real
    tmp=MAC_RD_REG(addr);
    tmp1 = tmp |(1 << 4);
    MAC_WR_REG(addr,tmp1);
    OS_MDELAY(10);
}

unsigned char hal_set_sys_clk(int clockdiv)
{
    unsigned int tmp,tmp1,tmp2,tmp3;

    PRINT("hal_set_sys_clk \n");
    /* reset bpll , write 1 for bit0 firstly, then write 0 again*/
    tmp = AON_RD_REG(RG_DPLL_A3);
    AON_WR_REG(RG_DPLL_A3, (tmp | BIT(0)));
    OS_MDELAY(10);
    AON_WR_REG(RG_DPLL_A3, (tmp & ~BIT(0)));
    //select CPU clock by digital divider for 80MHz
#ifdef TX_TIMEOUT_ERR
    hal_set_sys_clk_Core(RG_INTF_CPU_CLK,TEST_CPU_CLOCK);
#else
    //riscv
    hal_set_sys_clk_Core(RG_INTF_CPU_CLK,0x4f670031);
    //arc, cpu clock 0x4f070031
#endif
    //select BTCPU clock by digital divider for 48MHz
    hal_set_sys_clk_Core(RG_INTF_BTCPU_CLK,0x940011);

    //config mactxdelay
    tmp=MAC_RD_REG(RG_MAC_COUNT1);
    tmp= tmp | (0x7000000 );
    MAC_WR_REG(RG_MAC_COUNT1,tmp);
    OS_MDELAY(10);
    //config agc gain not use lut
    tmp=MAC_RD_REG(RG_AGC_GAIN_SEL);
    tmp= tmp & (~(1<< 24 ));
    MAC_WR_REG(RG_AGC_GAIN_SEL,tmp);
    OS_MDELAY(10);
    //switch share 64k ram to mac
    tmp= 0x00000001;
    MAC_WR_REG(RG_INTF_SHARE_64K_CAP,tmp);
    OS_MDELAY(10);
    //reset mac time rise pulse
    tmp= 0x81080100;
    MAC_WR_REG(RG_INTF_MAC_TIMER_CTRL1,tmp);
    OS_MDELAY(10);
    //gpioB pin mux input
    tmp= 0xffdfffff;
    MAC_WR_REG(RG_INTF_PIN_MUX_REG1,tmp);
    OS_MDELAY(10);

#ifdef B2B_TEST
    //pin mux config for b2b
    tmp= 0x9c03330a;
    MAC_WR_REG(RG_PHY_BT_CTRL,tmp);
    OS_MDELAY(10);
    //tx out format
    //tmp= 0x00000004;
    tmp= 0x00000002;    //bit[1] reg_tx_negate_msb is set to 1 in compliance with FPGA environment
    //bit[0] reg_tx_iq_swap
    //bit[1] reg_tx_negate_msb
    //bit[2] reg_tx_rotate_en is not exist now
    //bit[5:4]  reg_tx_signal_sel
    MAC_WR_REG(RG_PHY_TX_CTRL,tmp);
    OS_MDELAY(10);
    //add config for rx enable
    tmp= 0x00000001;
    MAC_WR_REG(RG_OFDM_RX_FSM_EN,tmp);
    OS_MDELAY(10);
    PRINT("B2B_TEST \n");
    //add ap/fs config,will delete sw ok
    if(STA1_VMAC0_SEND_40M==2)
    {
        tmp= 0xa1000000;
        MAC_WR_REG(RG_PHY_BW_REG, tmp);
        OS_MDELAY(10);
    }
#endif
}
#endif

#ifdef HAL_FPGA_VER
unsigned char hal_set_sys_clk_for_fpga(void)
{
    hi_change_sram_concurrent_mode();
    return 1;
}

unsigned int bbpll_init(void)
{
    RG_DPLL_A0_FIELD_T rg_dpll_a0;
    RG_DPLL_A1_FIELD_T rg_dpll_a1;
    RG_DPLL_A2_FIELD_T rg_dpll_a2;
    RG_DPLL_A3_FIELD_T rg_dpll_a3;
    RG_DPLL_A4_FIELD_T rg_dpll_a4;
    RG_DPLL_A5_FIELD_T rg_dpll_a5;
    RG_DPLL_A6_FIELD_T rg_dpll_a6;

    rg_dpll_a0.data = 0x00000c01;
    aml_aon_write_reg(RG_DPLL_A0, rg_dpll_a0.data);

    rg_dpll_a1.data = 0x00000090;
    aml_aon_write_reg(RG_DPLL_A1, rg_dpll_a1.data);

    rg_dpll_a2.data = 0x40095000;
    aml_aon_write_reg(RG_DPLL_A2, rg_dpll_a2.data);

    rg_dpll_a3.data = 0x02fe78fd;
    aml_aon_write_reg(RG_DPLL_A3, rg_dpll_a3.data);

    rg_dpll_a4.data = 0x00050851;
    aml_aon_write_reg(RG_DPLL_A4, rg_dpll_a4.data);

    rg_dpll_a5.data = 0x00000138;
    aml_aon_write_reg(RG_DPLL_A5, rg_dpll_a5.data);

    rg_dpll_a6.data = 0x00000000;
    aml_aon_write_reg(RG_DPLL_A6, rg_dpll_a6.data);

    return 0;
}

unsigned int bbpll_start (void)
{
    //RG_DPLL_A0_FIELD_T rg_dpll_a0;
    //RG_DPLL_A1_FIELD_T rg_dpll_a1;
    RG_DPLL_A2_FIELD_T rg_dpll_a2;
    RG_DPLL_A3_FIELD_T rg_dpll_a3;
    RG_DPLL_A4_FIELD_T rg_dpll_a4;
    RG_DPLL_A5_FIELD_T rg_dpll_a5;
    //RG_DPLL_A6_FIELD_T rg_dpll_a6;

    AML_PRINT_LOG_INFO("bbpll power on -------------->\n");
    AML_PRINT_LOG_INFO("1, start inter Ido \n");

    rg_dpll_a4.data = aml_aon_read_reg(RG_DPLL_A4);
    rg_dpll_a4.b.rg_wifi_bb_bt_dac_clk_div = 0x1;
    rg_dpll_a4.b.rg_wifi_bb_bt_adc_div = 0x5; //for bt adc clock
    aml_aon_write_reg(RG_DPLL_A4, rg_dpll_a4.data);
    udelay(50);
    rg_dpll_a4.b.rg_wifi_bb_bt_dac_clk_div = 0x3;
    aml_aon_write_reg(RG_DPLL_A4, rg_dpll_a4.data);
    udelay(10);

    AML_PRINT_LOG_INFO("2, start pll core \n");

    rg_dpll_a3.data = aml_aon_read_reg(RG_DPLL_A3);
    rg_dpll_a3.b.rg_wifi_bb_pll_bias_en = 1;
    aml_aon_write_reg(RG_DPLL_A3, rg_dpll_a3.data);

    udelay(50);
    rg_dpll_a3.b.rg_wifi_bb_pll_rst = 0;
    aml_aon_write_reg(RG_DPLL_A3, rg_dpll_a3.data);

    udelay(80);
    rg_dpll_a2.data = aml_aon_read_reg(RG_DPLL_A2);
    rg_dpll_a2.b.rg_wifi_bb_pll_reve |= BIT(6);
    rg_dpll_a2.b.rg_wifi_bb_pll_reve |= BIT(4); //bit18 need pull up
    aml_aon_write_reg(RG_DPLL_A2, rg_dpll_a2.data);

    AML_PRINT_LOG_INFO("3, check \n");
    udelay(10);
    rg_dpll_a5.data = aml_aon_read_reg(RG_DPLL_A5);
    if (rg_dpll_a5.b.ro_wifi_bb_pll_done == 1)
    {
        AML_PRINT_LOG_INFO("bbpll done !\n");
        return 1;
    }
    else
    {
        AML_PRINT_LOG_ERR("bbpll start failed !\n");
        return 0;
    }
}

unsigned int bbpll_stop(void)
{

    RG_DPLL_A3_FIELD_T rg_dpll_a3;
    RG_DPLL_A4_FIELD_T rg_dpll_a4;

    AML_PRINT_LOG_INFO("bbpll power down -------------->\n");
    rg_dpll_a3.data = aml_aon_read_reg(RG_DPLL_A3);
    rg_dpll_a3.b.rg_wifi_bb_pll_bias_en = 0;
    aml_aon_write_reg(RG_DPLL_A3, rg_dpll_a3.data);
    udelay(5);
    rg_dpll_a3.b.rg_wifi_bb_pll_rst = 1;
    aml_aon_write_reg(RG_DPLL_A3, rg_dpll_a3.data);

    udelay(5);
    rg_dpll_a4.data = aml_aon_read_reg(RG_DPLL_A4);
    rg_dpll_a4.b.rg_wifi_bb_bt_dac_clk_div &= ~BIT(1);
    aml_aon_write_reg(RG_DPLL_A4, rg_dpll_a4.data);
    udelay(5);
    rg_dpll_a4.b.rg_wifi_bb_bt_dac_clk_div = 0;
    aml_aon_write_reg(RG_DPLL_A4, rg_dpll_a4.data);

    return 0;
}

//0x4f070031: switch to bbp clk
void wifi_cpu_clk_switch(unsigned int clk_cfg)
{
    struct hw_interface   *hif = hif_get_hw_interface();
    hif->hif_ops.hi_write_word(RG_INTF_CPU_CLK,clk_cfg);

    AML_PRINT_LOG_INFO("cpu_clk_reg=0x%08x\n",
        hif->hif_ops.hi_read_word(RG_INTF_CPU_CLK));
}

#endif


#ifdef ICCM_CHECK
unsigned char buf_iccm_rd[ICCM_BUFFER_RD_LEN];
#endif
unsigned char buf_tmp[SRAM_LEN] = {0};

#ifdef SDIO_MODE_ON
extern unsigned char wifi_in_insmod;
extern unsigned char wifi_in_rmmod;
int hal_download_sdio_fw_img(void)
{
    const struct firmware *fw;
    struct device *dev = vm_cfg80211_get_parent_dev();
    unsigned char *src;
    int err = 0, len = 0, offset = 0, offset_base = 0;
    int rd_offset = 0;
    SYS_TYPE databyte = 0;
    unsigned int regdata =0;
    struct hw_interface *hif = hif_get_hw_interface();
    RG_PMU_A22_FIELD_T pmu_a22;
    RG_DPLL_A5_FIELD_T rg_dpll_a5;

    err = request_firmware(&fw, WIFI_FW_NAME, dev);
    if (err) {
        return err;
    }

    src = (unsigned char *)fw->data;
    hif->hif_ops.hi_write_reg32(RG_SCFG_SRAM_FUNC,MAC_REG_BASE);
    hif->hif_ops.hi_write_reg32(RG_SCFG_SRAM_FUNC,MAC_ICCM_AHB_BASE);
    hif->hif_ops.hi_write_reg32(RG_SCFG_REG_FUNC,MAC_REG_BASE);
    hif->hif_ops.hi_write_reg32(RG_SCFG_EEPROM_FUNC,MAC_REG_BASE);

    // close phy rest
    hif->hif_ops.hi_write_word(RG_WIFI_RST_CTRL, ~0);
    AML_PRINT_LOG_INFO("RG_SCFG_SRAM_FUNC %lx \n",hif->hif_ops.hi_read_reg32(RG_SCFG_SRAM_FUNC));

#ifdef EFUSE_ENABLE
    efuse_init();
    AML_PRINT_LOG_INFO("called efuse init\n");
#endif

    if (fw->size < ICCM_RAM_LEN + DCCM_LEN) {
        AML_PRINT_LOG_INFO("bin size(%d) error!\n", fw->size);
        release_firmware(fw);
        err = 2;
        return err;
    }
    len = ICCM_RAM_LEN;
#ifdef HAL_FPGA_VER
    hif->hif_ops.hi_write_reg32(RG_SCFG_SRAM_FUNC,MAC_REG_BASE);

    rg_dpll_a5.data = aml_aon_read_reg(RG_DPLL_A5);
    /*bpll not init*/
    if (rg_dpll_a5.b.ro_wifi_bb_pll_done != 1) {
        bbpll_init();
        bbpll_start();
        AML_PRINT_LOG_INFO("bbpll  init ok!\n");

    } else {
        AML_PRINT_LOG_ERR("bbpll  already init,not need to init!\n");
    }

    hal_set_sys_clk_for_fpga();
    hif->hif_ops.hi_write_reg32(RG_SCFG_SRAM_FUNC,MAC_ICCM_AHB_BASE);
    AML_PRINT_LOG_INFO("img len 0x%x, start download fw\n", fw->size);
#else
    hif->hif_ops.hi_write_reg32(RG_SCFG_SRAM_FUNC,MAC_REG_BASE);
    hal_set_sys_clk(10);
    hif->hif_ops.hi_write_reg32(RG_SCFG_SRAM_FUNC,MAC_ICCM_AHB_BASE);
#endif

#ifdef ICCM_ROM
    /*
     * skip iccm rom code, 0 to ICCM_ROM_LEN-1 for iccm rom,
     * ICCM_ROM_LEN to ICCM_RAM_LEN-1 for iccm ram.
     */
    offset = ICCM_ROM_LEN;
#else
    offset = 0;
#endif

    do {
        databyte = (len > SRAM_MAX_LEN) ? SRAM_MAX_LEN : len;
        if((offset + databyte) >= MAX_OFFSET) {
            offset_base += offset;
            hif->hif_ops.hi_write_reg32(RG_SCFG_SRAM_FUNC, MAC_ICCM_AHB_BASE + offset_base);
            offset = 0;
        }

        hif->hif_ops.hi_write_sram(src + offset + offset_base - ICCM_ROM_LEN,
            (unsigned char*)(SYS_TYPE)(0 + offset), databyte);

        offset += databyte;
        len -= databyte;
    } while(len > 0);

#ifdef ICCM_CHECK
    offset_base =0;
    offset = ICCM_ROM_LEN;
    len = ICCM_CHECK_LEN;
    hif->hif_ops.hi_write_reg32(RG_SCFG_SRAM_FUNC,MAC_ICCM_AHB_BASE);
    //host iccm ram read
    do {
        databyte = (len > SRAM_MAX_LEN) ? SRAM_MAX_LEN : len;

        if((offset + SRAM_MAX_LEN) >= MAX_OFFSET) {
            offset_base += offset;
            hif->hif_ops.hi_write_reg32(RG_SCFG_SRAM_FUNC, MAC_ICCM_AHB_BASE + offset_base);
            offset = 0;
        }

        hif->hif_ops.hi_read_sram(buf_iccm_rd + rd_offset,
            (unsigned char*)(SYS_TYPE)(0 + offset) ,databyte);

        offset += databyte;
        rd_offset += databyte;
        len -= databyte;
    } while(len > 0);

    if (memcmp(buf_iccm_rd, src, ICCM_CHECK_LEN)) {
        AML_PRINT_LOG_ERR("Host HAL: write ICCM ERROR!!!! \n");
        release_firmware(fw);
        err = 3;
        return err;

    } else {
        AML_PRINT_LOG_INFO("Host HAL: write ICCM SUCCESS!!!! \n");
    }
#endif

    /* Starting download DCCM */
    len = fw->size - ICCM_RAM_LEN;
    if (len > DCCM_LEN) {
        len = DCCM_LEN;
    }
    offset = 0;
    hif->hif_ops.hi_write_reg32(RG_SCFG_SRAM_FUNC, MAC_DCCM_AHB_BASE);
    src = (unsigned char *)(fw->data + ICCM_RAM_LEN);

    do {
        databyte = (len > SRAM_MAX_LEN) ? SRAM_MAX_LEN : len;
        hif->hif_ops.hi_write_sram(src + offset,
            (unsigned char*)(SYS_TYPE)(0 + offset), databyte);

        offset += databyte;
        len -= databyte;
    } while(len > 0);

    /* Starting run firmware */
    //set baseaddr to sram
    hif->hif_ops.hi_write_reg32(RG_SCFG_SRAM_FUNC, MAC_REG_BASE);
    len = SRAM_LEN;
    memset(buf_tmp, 0, len);
    AML_PRINT_LOG_INFO("set sram zero for simulation, total=0x%x\n", len);
    hif->hif_ops.hi_write_sram(buf_tmp, (unsigned char*)(SYS_TYPE)MAC_SRAM_BASE, len);

    //configure sram space
    hi_cfg_firmware();
    //set func2 baseaddr
    hif->hif_ops.hi_write_reg32(RG_SCFG_SRAM_FUNC, MAC_REG_BASE);

    regdata = hif->hif_ops.hi_read_reg32(RG_SCFG_REG_FUNC);
    AML_PRINT_LOG_INFO("RG_SCFG_REG_FUNC redata %x \n", regdata);
    //OS_MDELAY(10000);

#ifdef PROJECT_T9026
#ifdef HAL_FPGA_VER
    wifi_cpu_clk_switch(0x4f070033);
    wifi_cpu_clk_switch(0x4f0700f3);
#endif

    /* arc cpu domain power save for t9026  */
    if (0) {
        //cpu select arc
        //enable cpu
        regdata = hif->hif_ops.hi_read_word(RG_WIFI_MAC_ARC_CTRL);
        /* start firmware chip cpu */
        regdata |= CPU_RUN;
        hif->hif_ops.hi_write_word(RG_WIFI_MAC_ARC_CTRL, regdata);
    } else {
    //cpu select riscv
      regdata = hif->hif_ops.hi_read_word(RG_WIFI_CPU_CTRL);
      regdata |= 0x10000;
      hif->hif_ops.hi_write_word(RG_WIFI_CPU_CTRL,regdata);
      AML_PRINT_LOG_INFO("RG_WIFI_CPU_CTRL = %x redata= %x \n",RG_WIFI_CPU_CTRL,regdata);
      //enable cpu
      pmu_a22.data = hif->hif_ops.bt_hi_read_word(RG_PMU_A22);
      pmu_a22.b.rg_dev_reset_sw = 0x00;
      hif->hif_ops.bt_hi_write_word(RG_PMU_A22,pmu_a22.data);
      AML_PRINT_LOG_INFO("RG_PMU_A22 = %x redata= %x \n",RG_PMU_A22,pmu_a22.data);
    }

#elif defined (PROJECT_W1)

#ifdef HAL_FPGA_VER
    wifi_cpu_clk_switch(0x4f770033);
    /* mac clock 160 Mhz */
    hif->hif_ops.hi_write_word(RG_INTF_MAC_CLK, 0x00030001);
    if (aml_wifi_is_enable_rf_test())
        hal_dpd_memory_download();
#endif

    hif->hif_ops.hi_write_word(RG_AON_A37, hif->hif_ops.hi_read_word(RG_AON_A37) | 0x1);
    hif->hif_ops.hi_write_word(CMD_DOWN_FIFO_FDH_ADDR, 0);
    hif->hif_ops.hi_write_word(CMD_DOWN_FIFO_FDT_ADDR, 0);
    hif->hif_ops.hi_write_word(RG_WIFI_IF_RXPAGE_BUF_RDPTR, 0);
    hif->hif_ops.hi_write_word(RG_WIFI_IF_MAC_TXTABLE_RD_ID, 0);
    //cpu select riscv
    regdata = hif->hif_ops.hi_read_word(RG_WIFI_CPU_CTRL);
    regdata |= 0x10000;
    hif->hif_ops.hi_write_word(RG_WIFI_CPU_CTRL,regdata);
    AML_PRINT_LOG_INFO("RG_WIFI_CPU_CTRL = %x redata= %x \n",RG_WIFI_CPU_CTRL,regdata);
    //enable cpu
    pmu_a22.data = hif->hif_ops.bt_hi_read_word(RG_PMU_A22);
    pmu_a22.b.rg_dev_reset_sw = 0x00;
    hif->hif_ops.bt_hi_write_word(RG_PMU_A22,pmu_a22.data);
    AML_PRINT_LOG_INFO("RG_PMU_A22 = %x redata= %x \n",RG_PMU_A22,pmu_a22.data);
#endif

    AML_PRINT_LOG_INFO("fw download success!\n");
#ifdef SDIO_BUILD_IN
    wifi_in_insmod = 0;
#endif
    release_firmware(fw);
    return err;
}
#endif

#ifndef CONFIG_USB_CLOSE
int hal_download_usb_fw_img(void)
{
    struct hal_private *hal_priv = hal_get_priv();

    RG_DPLL_A5_FIELD_T rg_dpll_a5;
    hal_priv->hif->hif_ops.hi_write_word(RG_WIFI_RST_CTRL, ~(0));
    rg_dpll_a5.data = aml_aon_read_reg(RG_DPLL_A5);

    if (rg_dpll_a5.b.ro_wifi_bb_pll_done != 1 ) {
    //if (1) {
        bbpll_init();
        bbpll_start();
        AML_PRINT_LOG_INFO("bbpll  init ok!\n");

    } else {
        AML_PRINT_LOG_INFO("bbpll  already init,not need to init!\n");
    }

    hal_set_sys_clk_for_fpga();
    wifi_fw_download();

#ifdef HAL_FPGA_VER
    if (aml_wifi_is_enable_rf_test())
        hal_dpd_memory_download();
#endif

    hi_cfg_firmware();
    wifi_cpu_clk_switch(0x4f770033);
    hal_priv->hif->hif_ops.hi_write_word(RG_INTF_MAC_CLK, 0x00030001);

#ifdef HAL_SIM_VER
    hal_set_sys_clk(10);
#endif
    AML_PRINT_LOG_INFO("-----start firmware\n");
    start_wifi();

    return 0;
}
#endif

#if defined (HAL_FPGA_VER)
int mac_addr0 = 0x00;
int mac_addr1 = 0x01;
int mac_addr2 = 0x02;
int mac_addr3 = 0x58;
int mac_addr4 = 0x00;
int mac_addr5 = 0xcc;

static int vif0opmode = -1;
static int vif1opmode = -1;
static char * vmac0 = "wlan0";
static char * vmac1 = "p2p0";
static unsigned int con_mode = ((1 << WIFINET_M_STA) | (1 << WIFINET_M_P2P_DEV));
static int en_rf_test = 0;
static int cali_proofing = INVALID_PARAM_VALUE; /*the val is -1 use rf txt data otherwise use module param data*/

static char * plt_ver = NULL;
struct version_info version_map[] = {
    {"gva", 1},
    {"gva_mrt", 2}

};

const unsigned char BROADCAST_ADDRESS[WIFINET_ADDR_LEN] =  {0xff,0xff,0xff,0xff,0xff,0xff};
static char * mac_addr = "00:01:02:58:00:CC";
static char * country_code = "WW";
unsigned short dhcp_offload = 0;
static int sdblksize = BLKSIZE;
unsigned char aml_insmod_flag = 0;
#ifdef SDIO_MODE_ON
char *bus_type = "sdio";
#else
char *bus_type = "usb";
#endif

#ifdef DEBUG_MALLOC
    int kmalloc_count = 0;
    int kfree_count = 0;
    struct mem_statistic m_pn_buf[64];
    struct mem_statistic f_pn_buf[64];
#endif
#ifdef CONFIG_MAC_SUPPORT
extern u8 *wifi_get_mac(void);
#endif
extern void print_driver_version(void);

void aml_wifi_set_mac_addr(void)
{
    u64 timestamp;
#ifdef CONFIG_MAC_SUPPORT
    u8 addr[ETH_ALEN];
    u8 cbuf[50];
    unsigned int efuse_data_l = 0;
    unsigned int efuse_data_h = 0;

    memcpy(addr, wifi_get_mac(), ETH_ALEN);
    if (addr[0] != 0xff || aml_read_macaddr_from_file(WIFIMAC_PATH, addr) == true) {
        mac_addr0 = addr[0];
        mac_addr1 = addr[1];
        mac_addr2 = addr[2];
        mac_addr3 = addr[3];
        mac_addr4 = addr[4];
        mac_addr5 = addr[5];
    } else {
        efuse_data_l = efuse_manual_read(0x1);
        efuse_data_h = efuse_manual_read(0x2);
        if ((efuse_data_h != 0) && (efuse_data_l != 0)) {
            mac_addr0 = (efuse_data_h & 0xff00) >> 8;
            mac_addr1 = efuse_data_h & 0x00ff;
            mac_addr2 = (efuse_data_l & 0xff000000) >> 24;
            mac_addr3 = (efuse_data_l & 0x00ff0000) >> 16;
            mac_addr4 = (efuse_data_l & 0xff00) >> 8;
            mac_addr5 = efuse_data_l & 0xff;
            aml_retrieve_from_file(WIFIMAC_PATH, cbuf, 48);

            sprintf(cbuf + 21, MAC_FMT, mac_addr0, mac_addr1, mac_addr2, mac_addr3, mac_addr4, mac_addr5);
            if (aml_store_to_file(WIFIMAC_PATH, cbuf, strlen(cbuf)) > 0) {
                AML_PRINT_LOG_INFO("write the efuse mac to wifimac.txt\n");
            }
        } else {
#endif
            timestamp = get_jiffies_64();
            mac_addr3 = (timestamp & 0xff);
            mac_addr4 = ((timestamp >> 2) & 0xff);
            mac_addr5 = ((timestamp >> 4) & 0xff);
#ifdef CONFIG_MAC_SUPPORT
            aml_retrieve_from_file(WIFIMAC_PATH, cbuf, 48);

            sprintf(cbuf + 21, MAC_FMT, mac_addr0, mac_addr1, mac_addr2, mac_addr3, mac_addr4, mac_addr5);
            if (aml_store_to_file(WIFIMAC_PATH, cbuf, strlen(cbuf)) > 0)
                AML_PRINT_LOG_INFO("write the random mac to wifimac.txt\n");
        }
    }
#endif

    if (mac_addr0 & 0x3) {
        AML_PRINT_LOG_INFO("change the mac addr from [0x%x] \n", mac_addr0);

        mac_addr0 &= ~0x3;
        AML_PRINT_LOG_INFO("change the mac addr to [0x%x] \n", mac_addr0);
#ifdef CONFIG_MAC_SUPPORT
        aml_retrieve_from_file(WIFIMAC_PATH, cbuf, 48);

        sprintf(cbuf + 21, MAC_FMT, mac_addr0, mac_addr1, mac_addr2, mac_addr3, mac_addr4, mac_addr5);
        if (aml_store_to_file(WIFIMAC_PATH, cbuf, strlen(cbuf)) > 0)
            AML_PRINT_LOG_INFO("write the random mac to wifimac.txt\n");
#endif
    }
}

char * aml_wifi_get_country_code(void)
{
    return country_code;
}

int aml_wifi_get_vif0_opmode(void)
{
    return vif0opmode;
}

int aml_wifi_get_vif1_opmode(void)
{
    return vif1opmode;
}

char * aml_wifi_get_vif0_name(void)
{
    return vmac0;
}

char * aml_wifi_get_vif1_name(void)
{
    return vmac1;
}

unsigned int aml_wifi_get_con_mode(void)
{
    return con_mode;
}

int aml_wifi_get_cali_proofing(void)
{
    return cali_proofing;
}

void aml_wifi_set_con_mode(void *wifimac)
{
    unsigned int concurrent_mode = 0;
    struct drv_private *drv_priv = ((struct wifi_mac *)wifimac)->drv_priv;
    struct wlan_net_vif *main_vmac = drv_priv->drv_wnet_vif_table[NET80211_MAIN_VMAC];
    struct wlan_net_vif *p2p_vmac = drv_priv->drv_wnet_vif_table[NET80211_P2P_VMAC];

    concurrent_mode = BIT(main_vmac->vm_opmode) | BIT(p2p_vmac->vm_opmode);
    if (con_mode != concurrent_mode) {
        con_mode = concurrent_mode;
        AML_PRINT_LOG_INFO(" con_mode = 0x%02x",con_mode);
    }
}

char * aml_wifi_get_bus_type(void)
{
   char *ch[] = {"usb","sdio"};
   if(aml_bus_type) {
       return ch[0];
   } else {
       return ch[1];
   }
}

char * aml_wifi_get_fw_type(void)
{
    unsigned int regdata = hi_get_fw_version();
    if (regdata == FW_VERSION_W1) {
        return "w1";
    }
    else if (regdata == FW_VERSION_W1U) {
        return "w1u";
    }
    else {
        return "unknown";
    }
}

/* return value default is 0 */
unsigned int aml_wifi_get_platform_verid(void)
{
    int i;
    if (plt_ver == NULL)
    {
        return 0;
    }
    for (i = 0; i < sizeof(version_map)/sizeof(struct version_info); ++i)
    {
        if (strcmp(version_map[i].version_name, plt_ver) == 0)
        {
            AML_PRINT_LOG_INFO("version name: %s\n", version_map[i].version_name);
            return version_map[i].version_id;
        }
    }
    return 0;
}

unsigned int aml_wifi_is_enable_rf_test(void)
{
    return en_rf_test;
}

extern unsigned char wifi_in_insmod;
extern unsigned char wifi_in_rmmod;
extern unsigned char aml_bus_type;
extern void Init_B2B_Resource(void);

int _aml_insmod(void)
{
    int ret = 0;
    struct hw_interface * hif = hif_get_hw_interface();
    struct hal_private *hal_priv = hal_get_priv();
#if defined(SDIO_BUILD_IN) && defined(SDIO_MODE_ON)
    wifi_in_insmod = 1;
#endif

    if (aml_insmod_flag == 1) {
        AML_PRINT_LOG_INFO("aml_insmod end\n");
        return 0;
    }

    print_driver_version();
    AML_PRINT_LOG_INFO("driver version: %s\n", DRIVERVERSION);
    AML_PRINT_LOG_INFO("dhcp_offload %d set done.\n\n", dhcp_offload);

#ifdef DEBUG_MALLOC
    memset(m_pn_buf, 0, 64 * sizeof(struct mem_statistic));
    memset(f_pn_buf, 0, 64 * sizeof(struct mem_statistic));
#endif

    if(hif == NULL)
    {
        AML_PRINT_LOG_ERR("hif = %p\n",hif);
        ret =  -1;
        goto insmod_failed;
    }
    memset(hif, 0, sizeof(struct hw_interface));

    //dma interface or sdio interface init
    if (aml_bus_type == 1) {
        AML_PRINT_LOG_INFO("bus interface is USB!!!!!\n");
#ifndef CONFIG_USB_CLOSE
        ret = aml_usb_init();
#endif
    }
#ifdef SDIO_MODE_ON
    else if (aml_bus_type == 0) {
        AML_PRINT_LOG_INFO("bus interface is SDIO!!!!!\n");
        ret = aml_w1_init();
     }
#endif
#if 0
    if (strncmp(bus_type,"usb",3) == 0) {
        AML_PRINT_LOG_INFO("bus interface is USB!!!!!\n");
        aml_bus_type = 1;
#ifndef CONFIG_USB_CLOSE
        ret = aml_usb_init();
#endif
    } else if (strncmp(bus_type,"sdio",4) == 0) {
        AML_PRINT_LOG_INFO("bus interface is SDIO!!!!!\n");
        aml_bus_type = 0;
        ret = aml_sdio_init();
    }
#endif
    if(ret) {
        goto insmod_failed;
    }

    if (aml_wifi_is_enable_rf_test()) {
        AML_PRINT_LOG_INFO("---Aml drv---:Before calling \n");
        Init_B2B_Resource();
    }

    aml_insmod_flag = 1;
    hal_priv->hal_call_back->drv_trace_nl_init();
    AML_PRINT_LOG_INFO("start...\n");
    return 0;

insmod_failed:
#if defined(SDIO_BUILD_IN) && defined(SDIO_MODE_ON)
    wifi_in_insmod = 0;
#endif

    return ret;
}

void aml_disable_wifi(void)
{
    if (aml_bus_type == 1) {
#ifndef CONFIG_USB_CLOSE
        aml_usb_disable_wifi();
#endif
    }
#ifdef SDIO_MODE_ON
    else if (aml_bus_type == 0) {
        aml_sdio_disable_wifi();
    }
#endif
}

void aml_enable_wifi(void)
{
    struct hal_private *hal_priv = hal_get_priv();

    AML_PRINT_LOG_INFO("aml_enable_wifi start\n");
    if (aml_bus_type == 1) {
#ifndef CONFIG_USB_CLOSE
        aml_usb_enable_wifi();
#endif
    }
#ifdef SDIO_MODE_ON
    else if (aml_bus_type == 0) {
        aml_sdio_enable_wifi();
    }
#endif
    hal_priv->txcompletestatus->txdoneframecounter = 0;
    hal_priv->HalTxFrameDoneCounter = 0;
    hal_priv->txcompletestatus->txpagecounter = 0;
    hal_priv->HalTxPageDoneCounter = 0;

    AML_PRINT_LOG_INFO("aml_enable_wifi end\n");
}

void _aml_rmmod(void)
{
    struct hal_private *hal_priv = hal_get_priv();
#ifdef DEBUG_MALLOC
    unsigned char i;
#endif
#if defined(SDIO_BUILD_IN) && defined(SDIO_MODE_ON)
    wifi_in_rmmod = 1;
#endif
    if (aml_insmod_flag == 0) {
        AML_PRINT_LOG_INFO("==================aml_rmmod end ======================\n");
        return;
    }
    hal_priv->hal_call_back->drv_trace_nl_deinit();
    AML_PRINT_LOG_INFO("===================aml_rmmod start====================\n");
    hal_exit_priv();
    AML_PRINT_LOG_INFO("===================aml_rmmod end====================\n");
    aml_insmod_flag = 0;

#ifdef DEBUG_MALLOC
    AML_PRINT_LOG_INFO("kmalloc_count:%d\n", kmalloc_count);
    for (i = 0; i < kmalloc_count; ++i) {
        AML_PRINT_LOG_INFO("malloc name:%s, count:%d\n", m_pn_buf[i].name, m_pn_buf[i].count);
    }

    AML_PRINT_LOG_INFO("kfree_count:%d\n", kfree_count);
    for (i = 0; i < kfree_count; ++i) {
        AML_PRINT_LOG_INFO("free name:%s, count:%d\n", f_pn_buf[i].name, f_pn_buf[i].count);
    }
#endif
#if defined(SDIO_BUILD_IN) && defined(SDIO_MODE_ON)
    wifi_in_rmmod = 0;
#endif
}

#ifdef CHIP_RESET_SUPPORT
extern struct drv_reset_ops g_drv_reset_ops;
void drv_reset_ops_init(void)
{
    struct drv_reset_ops *ops = &g_drv_reset_ops;

    ops->enable_cb = _aml_insmod;
    ops->disable_cb = _aml_rmmod;
}
#endif

static int aml_insmod(void)
{
#ifdef CHIP_RESET_SUPPORT
    drv_reset_ops_init();
#endif
    return _aml_insmod();
}

static void aml_rmmod(void)
{
#ifdef CHIP_RESET_SUPPORT
    memset(&g_drv_reset_ops, 0x00, sizeof(struct drv_reset_ops));
#endif
    _aml_rmmod();
}

MODULE_LICENSE("GPL");
module_init(aml_insmod);
module_exit(aml_rmmod);

module_param(vif0opmode, int, S_IRUGO);
module_param(vif1opmode, int, S_IRUGO);
module_param(vmac0, charp, S_IRUGO);
module_param(vmac1, charp, S_IRUGO);
module_param(con_mode, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
module_param(plt_ver, charp, S_IRUGO);
module_param(sdblksize, int, S_IRUGO);
module_param(en_rf_test, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
module_param(cali_proofing, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);


/*Added for pass mac address when load wifi driver
 *Usage: insmod ***.ko mac_addr=xx:xx:xx:xx:xx:xx
 *Default: 00:01:02:58:00:12
*/
module_param(mac_addr,charp,0644);
MODULE_PARM_DESC(mac_addr,"A string variable to describe wifi mac address");

module_param(dhcp_offload, ushort, S_IRUGO);
MODULE_PARM_DESC(dhcp_offload,"A short variable to control dhcp offload function");

/*Usage: insmod ***.ko country_code=xx*/

module_param(country_code,charp,0644);
MODULE_PARM_DESC(country_code,"A string variable to describe country code");

module_param(bus_type, charp,S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(bus_type,"A string variable to adjust sdio or usb bus interface");

#endif

#ifdef HAL_SIM_VER
#ifdef FW_NAME
}
#endif
#endif //#ifdef CONFIG_GXL
