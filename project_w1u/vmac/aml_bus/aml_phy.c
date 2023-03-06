#include <linux/init.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h> /* printk() */

char *bus_type = "sdio";
unsigned char aml_bus_type;
extern int aml_usb_insmod(void);
extern int aml_usb_rmmod(void);
extern int aml_sdio_insmod(void);
extern int aml_sdio_rmmod(void);
int aml_bus_intf_insmod(void)
{
    int ret;
    if (strncmp(bus_type,"usb",3) == 0) {
        ret = aml_usb_insmod();
        aml_bus_type = 1;
        if(ret) {
            printk("aml usb bus init fail\n");
        }
    } else if (strncmp(bus_type,"sdio",4) == 0) {
        ret = aml_sdio_insmod();
        aml_bus_type = 0;
        if(ret) {
            printk("aml sdio bus init fail\n");
        }
    }
    return 0;
}

void aml_bus_intf_rmmod(void)
{
    if (strncmp(bus_type,"usb",3) == 0) {
        aml_usb_rmmod();
    } else if (strncmp(bus_type,"sdio",4) == 0) {
        aml_sdio_rmmod();
    }
}

EXPORT_SYMBOL(aml_bus_type);
module_param(bus_type, charp,S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(bus_type,"A string variable to adjust sdio or usb bus interface");
module_init(aml_bus_intf_insmod);
module_exit(aml_bus_intf_rmmod);

MODULE_LICENSE("GPL");

