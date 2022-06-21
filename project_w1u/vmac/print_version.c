#include "wifi_hal_com.h"

void print_driver_version(void) {
    printk("driver compile date:2022-04-16 21:03:48, driver hash: drv_hash:568ef5bf0b119022cd306edc267b4856c96a10e3\n");
    printk("fw compile date: 116280 Apr, 16, fw hash: fw_hash:568ef5bf0b119022cd306edc267b4856c96a10e3\n");
    printk("rf cali: last commit: 2022/Mar/21 15:32:32  hash:c9dea380b8cd1254e423fc2db41acda7d6f593c0\n");
}
