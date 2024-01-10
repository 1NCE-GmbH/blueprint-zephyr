/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/net/socket.h>
#include <custom_fota_download.h>
#include <nrf_socket.h>
#include <stdio.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/toolchain.h>
#include <modem/lte_lc.h>
#include <modem/modem_key_mgmt.h>
#include "nce_mender_client.h"
#include <modem/nrf_modem_lib.h>
#include "update.h"


static const struct gpio_dt_spec sw0 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);


/* static function declaration */
static void dfu_button_pressed(const struct device *gpiob, struct gpio_callback *cb, uint32_t pins);

static struct gpio_callback sw0_cb;

int cert_provision(void)
{
	static const char cert[] = {
		#include "../cert/BaltimoreCyberTrustRoot"
	};
	BUILD_ASSERT(sizeof(cert) < KB(4), "Certificate too large");

	int err;
	bool exists;

	err = modem_key_mgmt_exists(TLS_SEC_TAG,
				    MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, &exists);
	if (err) {
		printk("Failed to check for certificates err %d\n", err);
		return err;
	}

	if (exists) {
		/* For the sake of simplicity we delete what is provisioned
		 * with our security tag and reprovision our certificate.
		 */
		err = modem_key_mgmt_delete(TLS_SEC_TAG,
					    MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN);
		if (err) {
			printk("Failed to delete existing certificate, err %d\n",
			       err);
		}
	}

	printk("Provisioning certificate\n");

	/*  Provision certificate to the modem */
	err = modem_key_mgmt_write(TLS_SEC_TAG,
				   MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, cert,
				   sizeof(cert) - 1);
	if (err) {
		printk("Failed to provision certificate, err %d\n", err);
		return err;
	}

	return 0;
}

int button_init(void)
{
	int err;

	if (!device_is_ready(sw0.port)) {
		printk("SW0 GPIO port device not ready\n");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&sw0, GPIO_INPUT);
	if (err < 0) {
		return err;
	}

	gpio_init_callback(&sw0_cb, dfu_button_pressed, BIT(sw0.pin));

	err = gpio_add_callback(sw0.port, &sw0_cb);
	if (err < 0) {
		printk("Unable to configure SW0 GPIO pin!\n");
		return err;
	}

	button_irq_enable();

	return 0;
}
void button_irq_disable(void)
{
	gpio_pin_interrupt_configure_dt(&sw0, GPIO_INT_DISABLE);
}
void button_irq_enable(void)
{
	gpio_pin_interrupt_configure_dt(&sw0, GPIO_INT_EDGE_TO_ACTIVE);
}
static void dfu_button_pressed(const struct device *gpiob, struct gpio_callback *cb,uint32_t pins)
{
	button_irq_disable();
}

/**@brief Configures modem to provide LTE link.
 *
 * Blocks until link is successfully established.
 */
 void modem_configure(void)
{
#if defined(CONFIG_LTE_LINK_CONTROL)
	BUILD_ASSERT(!IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT),
			"This sample does not support auto init and connect");
	int err;
#if defined(CONFIG_USE_HTTPS)
	err = cert_provision();
	__ASSERT(err == 0, "Could not provision root CA to %d", TLS_SEC_TAG);
#endif    
	printk("LTE Link Connecting ...\n");
	err = lte_lc_init_and_connect();
	__ASSERT(err == 0, "LTE link could not be established.");
	printk("LTE Link Connected!\n");
#endif
}

static int shell_download(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	fota_start();

	return 0;
}

static int shell_reboot(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Device will now reboot");
	sys_reboot(SYS_REBOOT_WARM);

	return 0;
}

SHELL_CMD_REGISTER(reset, NULL, "For rebooting device", shell_reboot);
SHELL_CMD_REGISTER(download, NULL, "For downloading modem  firmware", shell_download);

