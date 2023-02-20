#include "wifi_hal_com.h"

void print_driver_version(void) {
    printk("driver compile date: 2022-11-24 17:46:04,driver hash: 514550a5955ea2db763f1c1f2cac697daa6e1570\n");
    printk("fw compile date: 2022-11-24 17:46:02,fw hash: 514550a5955ea2db763f1c1f2cac697daa6e1570,fw size: 116280\n");
    printk("rf cali: last commit: 2022-11-07 20:47:18 hash:514550a5955ea2db763f1c1f2cac697daa6e1570\n");
}
