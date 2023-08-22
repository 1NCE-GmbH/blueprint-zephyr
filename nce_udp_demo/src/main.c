/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <modem/lte_lc.h>
#include <zephyr/net/socket.h>
#include <nce_iot_c_sdk.h>

#if defined(CONFIG_BOARD_THINGY91_NRF9160_NS)
#include <zephyr/drivers/gpio.h>
/*
 * Thingy:91 LEDs
 */
static struct gpio_dt_spec ledRed = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,
						     {0});
static struct gpio_dt_spec ledGreen = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios,
						     {0});
static struct gpio_dt_spec ledBlue = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led2), gpios,
						     {0});
#endif

LOG_MODULE_REGISTER( UDP_CLIENT, CONFIG_LOG_DEFAULT_LEVEL );
#define UDP_IP_HEADER_SIZE 28

static int client_fd, recv_fd;
static struct k_work_delayable server_transmission_work;
static struct k_work_delayable control_recv_work;
static struct addrinfo * res;
static struct addrinfo hints =
{
    .ai_family   = AF_INET,
    .ai_socktype = SOCK_DGRAM,
};
static struct addrinfo * control_recv;
static struct addrinfo hints_control_recv =
{
    .ai_family   = AF_INET,
    .ai_socktype = SOCK_DGRAM,
};
int hints_control_recv_struct_length = sizeof(hints_control_recv);
struct k_thread thread_recv,thread_send;

#define STACK_SIZE 512
#define STACK_SIZE2 2048
#define THREAD_PRIORITY 5
K_THREAD_STACK_DEFINE(thread_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_stack2, STACK_SIZE2);

K_SEM_DEFINE(lte_connected, 0, 1);

#if defined(CONFIG_BOARD_THINGY91_NRF9160_NS)
void configureLeds() {
	int ret = 0;
	if (ledRed.port && !device_is_ready(ledRed.port)) {
		printk("Error %d: LED device %s is not ready; ignoring it\n",
		       ret, ledRed.port->name);
		ledRed.port = NULL;
	}
	if (ledRed.port) {
		ret = gpio_pin_configure_dt(&ledRed, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED device %s pin %d\n",
			       ret, ledRed.port->name, ledRed.pin);
			ledRed.port = NULL;
		}
	}

	if (ledGreen.port && !device_is_ready(ledGreen.port)) {
		printk("Error %d: LED device %s is not ready; ignoring it\n",
		       ret, ledGreen.port->name);
		ledGreen.port = NULL;
	}
	if (ledGreen.port) {
		ret = gpio_pin_configure_dt(&ledGreen, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED device %s pin %d\n",
			       ret, ledGreen.port->name, ledGreen.pin);
			ledGreen.port = NULL;
		}
	}

	if (ledBlue.port && !device_is_ready(ledBlue.port)) {
		printk("Error %d: LED device %s is not ready; ignoring it\n",
		       ret, ledBlue.port->name);
		ledBlue.port = NULL;
	}
	if (ledBlue.port) {
		ret = gpio_pin_configure_dt(&ledBlue, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED device %s pin %d\n",
			       ret, ledBlue.port->name, ledBlue.pin);
			ledBlue.port = NULL;
		}
	}
}
#endif

static void server_transmission_work_fn(struct k_work *work)
{
	int err;
	#if !defined(CONFIG_NCE_ENERGY_SAVER)
	 char buffer[] = CONFIG_PAYLOAD;
	#else
	char buffer[CONFIG_PAYLOAD_DATA_SIZE];
	
    Element2byte_gen_t battery_level = {.type= E_INTEGER,.value.i=99,.template_length=1};
    Element2byte_gen_t signal_strength = {.type= E_INTEGER,.value.i=84,.template_length=1};
    Element2byte_gen_t software_version = {.type= E_STRING,.value.s="2.2.1",.template_length=5};
    err= os_energy_save(buffer,1, 3,battery_level,signal_strength,software_version);
	if (err < 0) {
		printk("Failed to save energy, %d\n", errno);
		return;
	}
	printk("Transmitting UDP/IP payload of %d bytes to the ",
	       CONFIG_PAYLOAD_DATA_SIZE + UDP_IP_HEADER_SIZE);
	#endif

	printk("Hostname %s, port number %d\n",
	       CONFIG_UDP_SERVER_HOSTNAME,
	       CONFIG_UDP_SERVER_PORT);

	err = send(client_fd, buffer, sizeof(buffer), 0);
	if (err < 0) {
		printk("Failed to transmit UDP packet, %d\n", errno);
		return;
	}

	#if defined(CONFIG_BOARD_THINGY91_NRF9160_NS)
	if (ledBlue.port) {
		gpio_pin_set_dt(&ledBlue, 0);
	}
	if (ledGreen.port) {
		gpio_pin_set_dt(&ledGreen, 100);
	}
	#endif

	k_work_schedule(&server_transmission_work,
			K_SECONDS(CONFIG_UDP_DATA_UPLOAD_FREQUENCY_SECONDS));
}
static void reciever_work_fn(struct k_work *work)
{
	char buffer[256];
	struct sockaddr_in my_addr = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = htonl(INADDR_ANY),
    .sin_port = htons(CONFIG_NCE_RECV_PORT)
	};
  
    // Create socket
    recv_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(recv_fd < 0){
        printk("Error while creating socket\n");
        goto cleanup;
    }
    printf("Socket created successfully\n");

	// Bind to the set port and IP:
    if(zsock_bind(recv_fd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr_in)) < 0){
        printk("Couldn't bind to the port\n");
		goto cleanup;
    }
    printk("Listening for incoming messages...\n\n");

    // Set the timeout value
    struct timeval timeout;
    timeout.tv_sec = 60;  // Timeout of 20 seconds
    timeout.tv_usec = 0;

    // Set the socket option for timeout
    if (setsockopt(recv_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("failed to set the socket option for timeout");
		goto cleanup;
    }

    ssize_t received_bytes = recvfrom(recv_fd, buffer, sizeof(buffer)-1, 0,
         (struct sockaddr*)control_recv, &hints_control_recv_struct_length); 
	if (received_bytes < 0)  {
        printf("No message received\n");
        goto cleanup;
    }

	buffer[received_bytes] = '\0';
	printk("Received message: %s\n", buffer);
	k_work_schedule(&control_recv_work,
			K_SECONDS(CONFIG_UDP_DATA_UPLOAD_FREQUENCY_SECONDS));

cleanup:
    close(recv_fd);
    return;
}
static void work_init(void)
{
	k_work_init_delayable(&server_transmission_work,
			      server_transmission_work_fn);
	k_work_init_delayable(&control_recv_work,
			      reciever_work_fn);
				  
}

#if defined(CONFIG_NRF_MODEM_LIB)
static void lte_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
		     (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			break;
		}

		printk("Network registration status: %s\n",
			evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
			"Connected - home network" : "Connected - roaming\n");
		k_sem_give(&lte_connected);
		break;
	case LTE_LC_EVT_PSM_UPDATE:
		printk("PSM parameter update: TAU: %d, Active time: %d\n",
			evt->psm_cfg.tau, evt->psm_cfg.active_time);
		break;
	case LTE_LC_EVT_EDRX_UPDATE: {
		char log_buf[60];
		ssize_t len;

		len = snprintf(log_buf, sizeof(log_buf),
			       "eDRX parameter update: eDRX: %f, PTW: %f\n",
			       evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
		if (len > 0) {
			printk("%s\n", log_buf);
		}
		break;
	}
	case LTE_LC_EVT_RRC_UPDATE:
		printk("RRC mode: %s\n",
			evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
			"Connected" : "Idle\n");
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		printk("LTE cell changed: Cell ID: %d, Tracking area: %d\n",
		       evt->cell.id, evt->cell.tac);
		break;
	default:
		break;
	}
}

static int configure_low_power(void)
{
	int err;

#if defined(CONFIG_UDP_PSM_ENABLE)
	/** Power Saving Mode */
	err = lte_lc_psm_req(true);
	if (err) {
		printk("lte_lc_psm_req, error: %d\n", err);
	}
#else
	err = lte_lc_psm_req(false);
	if (err) {
		printk("lte_lc_psm_req, error: %d\n", err);
	}
#endif

#if defined(CONFIG_UDP_EDRX_ENABLE)
	/** enhanced Discontinuous Reception */
	err = lte_lc_edrx_req(true);
	if (err) {
		printk("lte_lc_edrx_req, error: %d\n", err);
	}
#else
	err = lte_lc_edrx_req(false);
	if (err) {
		printk("lte_lc_edrx_req, error: %d\n", err);
	}
#endif

#if defined(CONFIG_UDP_RAI_ENABLE)
	/** Release Assistance Indication  */
	err = lte_lc_rai_req(true);
	if (err) {
		printk("lte_lc_rai_req, error: %d\n", err);
	}
#endif

	return err;
}

static void modem_init(void)
{
	int err;

	if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {
		/* Do nothing, modem is already configured and LTE connected. */
	} else {
		err = lte_lc_init();
		if (err) {
			printk("Modem initialization failed, error: %d\n", err);
			return;
		}
	}
}

static void modem_connect(void)
{
	int err;

	if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {
		/* Do nothing, modem is already configured and LTE connected. */
	} else {
		err = lte_lc_connect_async(lte_handler);
		if (err) {
			printk("Connecting to LTE network failed, error: %d\n",
			       err);
			return;
		}
	}
}
#endif

static void server_disconnect(void)
{
	(void)close(client_fd);
}

static int server_init(void)
{
	int err;

    err= zsock_getaddrinfo( CONFIG_UDP_SERVER_HOSTNAME, NULL, &hints, &res );
	if (err < 0) {
		printk("getaddrinfo() failed, err %d\n", errno);
		err = -errno;
		goto error;
	}
	 ( ( struct sockaddr_in * ) res->ai_addr )->sin_port = htons( CONFIG_UDP_SERVER_PORT );

	return 0;
error:
	server_disconnect();

	return err;
}

static int server_connect(void)
{
	int err;

	client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (client_fd < 0) {
		printk("Failed to create UDP socket: %d\n", errno);
		err = -errno;
		goto error;
	}

	err = connect(client_fd, (struct sockaddr *)res->ai_addr,
		      sizeof(struct sockaddr_in));
	if (err < 0) {
		printk("Connect failed : %d\n", errno);
		goto error;
	}

	return 0;

error:
	server_disconnect();

	return err;
}
void background_thread_func() {
    while (1) {
        // If a message is received or event occurs, schedule the work
        k_work_schedule(&control_recv_work, K_NO_WAIT);
		k_sleep(K_SECONDS(2));
    }
}
void front_thread_func() {
    while (1) {
        // Sending the UDP Packet
        k_work_schedule(&server_transmission_work, K_NO_WAIT);
		k_sleep(K_SECONDS(1));
    }
}
void main(void)
{
	int err;

	#if defined(CONFIG_BOARD_THINGY91_NRF9160_NS)
	configureLeds();
	k_sleep(K_SECONDS(10));
	if (ledRed.port) {
		gpio_pin_set_dt(&ledRed, 100);
	}
	#endif

	printk("1NCE UDP sample has started\n");

	work_init();

#if defined(CONFIG_NRF_MODEM_LIB)

	/* Initialize the modem before calling configure_low_power(). This is
	 * because the enabling of RAI is dependent on the
	 * configured network mode which is set during modem initialization.
	 */
	modem_init();

	err = configure_low_power();
	if (err) {
		printk("Unable to set low power configuration, error: %d\n",
		       err);
	}

	printk("Connecting... \n");

	modem_connect();

	k_sem_take(&lte_connected, K_FOREVER);
#endif

	#if defined(CONFIG_BOARD_THINGY91_NRF9160_NS)
	if (ledRed.port) {
		gpio_pin_set_dt(&ledRed, 0);
	}
	if (ledBlue.port) {
		gpio_pin_set_dt(&ledBlue, 100);
	}
	#endif

	err = server_init();
	if (err) {
		printk("Not able to initialize UDP server connection\n");
		return;
	}

	err = server_connect();
	if (err) {
		printk("Not able to connect to UDP server\n");
		return;
	}
	k_tid_t thread_recv_id = k_thread_create(&thread_recv, thread_stack,
                                        K_THREAD_STACK_SIZEOF(thread_stack),
                                        background_thread_func,
                                        NULL, NULL, NULL,
                                        THREAD_PRIORITY, 0, K_NO_WAIT);
	k_tid_t thread_send_id = k_thread_create(&thread_send, thread_stack2,
                                        K_THREAD_STACK_SIZEOF(thread_stack2),
                                        front_thread_func,
                                        NULL, NULL, NULL,
                                        THREAD_PRIORITY, 0, K_NO_WAIT);
	// Start the thread
	k_thread_start(thread_send_id);
	k_thread_start(thread_recv_id);

	// Delay or wait for the threads to complete
    k_sleep(K_SECONDS(2));  // Delay for 2 seconds
}
