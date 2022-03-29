#include "wifi_hal_com.h"

void print_driver_version(void) {
    printk("driver compile date:2022-03-10 16:35:22, driver hash: drv_hash:11a766db42cfca37e1c32dec79d5d0b357c16b54\n");
    printk("fw compile date: 116280 Mar, 10, fw hash: fw_hash:abdfc31482e431327ca9b480e1ad8ef92a8a47e5\n");
    printk("rf cali: last commit: 2022/Mar/5 20:43:04  hash:f5f8c15835ca5f624174156eaf8edeffbf66a806\n");
}
