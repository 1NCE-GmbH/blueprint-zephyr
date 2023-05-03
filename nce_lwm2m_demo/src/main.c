#include <string.h>
#include <zephyr/kernel.h>
#include <stdlib.h>
#include <zephyr/net/socket.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <zephyr/logging/log.h>


// LwM2M 
#include <zephyr/net/lwm2m.h>
#include <app_event_manager.h>
#include <date_time.h>
#include <modem/modem_info.h>
#include <net/lwm2m_client_utils.h>
#include "lwm2m_app_utils.h"
#include "lwm2m_engine.h"
#include "date_time.h"
#include <modem/modem_key_mgmt.h>
#include <lwm2m_client_app.h>
#include <lwm2m_resource_ids.h>

#if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
#include <zephyr/drivers/gpio.h>

/*
 * Thingy:91 LEDs
 */
static struct gpio_dt_spec ledRed = GPIO_DT_SPEC_GET_OR( DT_ALIAS( led0 ), gpios,
                                                         { 0 } );
static struct gpio_dt_spec ledGreen = GPIO_DT_SPEC_GET_OR( DT_ALIAS( led1 ), gpios,
                                                           { 0 } );
static struct gpio_dt_spec ledBlue = GPIO_DT_SPEC_GET_OR( DT_ALIAS( led2 ), gpios,
                                                          { 0 } );
#endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */

LOG_MODULE_REGISTER( LWM2M_CLIENT, CONFIG_LOG_DEFAULT_LEVEL );

int err;
static struct lwm2m_ctx client = {0};
static uint8_t endpoint_name[]=CONFIG_NCE_ICCID;
static K_MUTEX_DEFINE(lte_mutex);
static K_SEM_DEFINE(state_mutex, 0, 1);
static bool modem_connected_to_network;
static bool reconnect;
/* Client State Machine states */
static enum client_state {
	START,		/* Start Connection to a server*/
	CONNECTING,	/* LwM2M engine is connecting to server */
	BOOTSTRAP,	/* LwM2M engine is doing a bootstrap */
	CONNECTED,	/* LwM2M Client connection establisment to server */
	LTE_OFFLINE,	/* LTE offline and LwM2M engine should be suspended */
	NETWORK_ERROR	/* Client network error handling. Client stop and modem reset */
} client_state = START;
/* Enable session lifetime check for initial boot */
static bool update_session_lifetime = true;
static void date_time_event_handler(const struct date_time_evt *evt)
{
	switch (evt->type) {
	case DATE_TIME_OBTAINED_MODEM: {
		int64_t time = 0;

		LOG_INF("Obtained date-time from modem");
		date_time_now(&time);
		lwm2m_engine_set_s32(LWM2M_PATH(LWM2M_OBJECT_DEVICE_ID, 0, 13),
				     (int32_t)(time / 1000));
		break;
	}

	case DATE_TIME_OBTAINED_NTP: {
		int64_t time = 0;

		LOG_INF("Obtained date-time from NTP server");
		date_time_now(&time);
		lwm2m_engine_set_s32(LWM2M_PATH(LWM2M_OBJECT_DEVICE_ID, 0, 13),
				     (int32_t)(time / 1000));
		break;
	}

	case DATE_TIME_NOT_OBTAINED:
		LOG_INF("Could not obtain date-time update");
		break;

	default:
		break;
	}
}

#if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
void configureLeds()
{
    int ret = 0;

    if( ledRed.port && !device_is_ready( ledRed.port ) )
    {
        printk( "Error %d: LED device %s is not ready; ignoring it\n",
                ret, ledRed.port->name );
        ledRed.port = NULL;
    }

    if( ledRed.port )
    {
        ret = gpio_pin_configure_dt( &ledRed, GPIO_OUTPUT );

        if( ret != 0 )
        {
            printk( "Error %d: failed to configure LED device %s pin %d\n",
                    ret, ledRed.port->name, ledRed.pin );
            ledRed.port = NULL;
        }
    }

    if( ledGreen.port && !device_is_ready( ledGreen.port ) )
    {
        printk( "Error %d: LED device %s is not ready; ignoring it\n",
                ret, ledGreen.port->name );
        ledGreen.port = NULL;
    }

    if( ledGreen.port )
    {
        ret = gpio_pin_configure_dt( &ledGreen, GPIO_OUTPUT );

        if( ret != 0 )
        {
            printk( "Error %d: failed to configure LED device %s pin %d\n",
                    ret, ledGreen.port->name, ledGreen.pin );
            ledGreen.port = NULL;
        }
    }

    if( ledBlue.port && !device_is_ready( ledBlue.port ) )
    {
        printk( "Error %d: LED device %s is not ready; ignoring it\n",
                ret, ledBlue.port->name );
        ledBlue.port = NULL;
    }

    if( ledBlue.port )
    {
        ret = gpio_pin_configure_dt( &ledBlue, GPIO_OUTPUT );

        if( ret != 0 )
        {
            printk( "Error %d: failed to configure LED device %s pin %d\n",
                    ret, ledBlue.port->name, ledBlue.pin );
            ledBlue.port = NULL;
        }
    }
}
#endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */

int store_credentials( void )
{
    int err;
	bool exists=false;

    /* Store Bootstrapping Credentials */
	 err = modem_key_mgmt_write( CONFIG_LWM2M_CLIENT_UTILS_BOOTSTRAP_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK, CONFIG_NCE_LWM2M_BOOTSTRAP_PSK, sizeof(CONFIG_NCE_LWM2M_BOOTSTRAP_PSK));
	LOG_DBG("Bootstrap DTLS PSK storage status: %d\n", err);

	err = modem_key_mgmt_write( CONFIG_LWM2M_CLIENT_UTILS_BOOTSTRAP_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY, CONFIG_NCE_ICCID, sizeof(CONFIG_NCE_ICCID));
	LOG_DBG("Bootstrap DTLS Identity storage status: %d\n", err);

	/* Free LwM2M server Credentials if they exists*/
	err = modem_key_mgmt_exists( CONFIG_LWM2M_CLIENT_UTILS_SERVER_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK,&exists);
	LOG_DBG("LwM2M Server DTLS PSK exist status: %d\n", err);
	err = modem_key_mgmt_exists( CONFIG_LWM2M_CLIENT_UTILS_SERVER_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY,&exists);
	LOG_DBG("LwM2M Server DTLS Identity exist status: %d\n", err);

	if(err==0 && exists)  {
	err = modem_key_mgmt_delete( CONFIG_LWM2M_CLIENT_UTILS_SERVER_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK);
	LOG_DBG("LwM2M Server DTLS PSK Deletion status: %d\n", err);
	err = modem_key_mgmt_delete( CONFIG_LWM2M_CLIENT_UTILS_SERVER_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY);
	LOG_DBG("LwM2M Server DTLS Identity Deletion status: %d\n", err);
	}
	else  {
	LOG_DBG("No Credentials found\n");
	}


    return err;
}

static void rd_client_update_lifetime(int srv_obj_inst)
{
	char pathstr[MAX_RESOURCE_LEN];
	uint32_t current_lifetime = 0;

	uint32_t lifetime = CONFIG_LWM2M_ENGINE_DEFAULT_LIFETIME;

	snprintk(pathstr, sizeof(pathstr), "1/%d/1", srv_obj_inst);
	lwm2m_engine_get_u32(pathstr, &current_lifetime);

	if (current_lifetime != lifetime) {
		/* SET Configured value */
		lwm2m_engine_set_u32(pathstr, lifetime);
		LOG_DBG("Update session lifetime from %d to %d", current_lifetime, lifetime);
	}
	update_session_lifetime = false;
}
static void state_trigger_and_unlock(enum client_state new_state)
{
	if (new_state != client_state) {
		client_state = new_state;
		k_sem_give(&state_mutex);
	}
	k_mutex_unlock(&lte_mutex);
}
static void state_set_and_unlock(enum client_state new_state)
{
	client_state = new_state;
	k_mutex_unlock(&lte_mutex);
}
static void rd_client_event(struct lwm2m_ctx *client, enum lwm2m_rd_client_event client_event)
{
	k_mutex_lock(&lte_mutex, K_FOREVER);

	if (client_state == LTE_OFFLINE &&
	    client_event != LWM2M_RD_CLIENT_EVENT_ENGINE_SUSPENDED) {
		LOG_DBG("Drop network event %d at LTE offline state", client_event);
		k_mutex_unlock(&lte_mutex);
		return;
	}

	switch (client_event) {
	case LWM2M_RD_CLIENT_EVENT_NONE:
		/* do nothing */
		k_mutex_unlock(&lte_mutex);
		break;

	case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_FAILURE:
		LOG_DBG("Bootstrap registration failure!");
		state_trigger_and_unlock(NETWORK_ERROR);
		break;

	case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_COMPLETE:
		LOG_DBG("Bootstrap registration complete");
		update_session_lifetime = true;
		state_trigger_and_unlock(BOOTSTRAP);
		break;

	case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_TRANSFER_COMPLETE:
		LOG_DBG("Bootstrap transfer complete");
		k_mutex_unlock(&lte_mutex);
		break;

	case LWM2M_RD_CLIENT_EVENT_REGISTRATION_FAILURE:
		LOG_WRN("Registration failure!");
		state_trigger_and_unlock(NETWORK_ERROR);
		break;

	case LWM2M_RD_CLIENT_EVENT_REGISTRATION_COMPLETE:
		LOG_DBG("Registration complete");
		
		#if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
        if( ledBlue.port )
        {
            gpio_pin_set_dt( &ledBlue, 0 );
        }

        if( ledGreen.port )
        {
        	gpio_pin_set_dt( &ledGreen, 100 );
			char lwm2m_path[MAX_LWM2M_PATH_LEN];
			snprintk(lwm2m_path, MAX_LWM2M_PATH_LEN, "%d/%u/%d", IPSO_OBJECT_LIGHT_CONTROL_ID,
					0, ON_OFF_RID);
			lwm2m_engine_set_bool(lwm2m_path, true);

        }
        #endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */

		state_trigger_and_unlock(CONNECTED);
		break;

	case LWM2M_RD_CLIENT_EVENT_REG_TIMEOUT:
		LOG_DBG("Registration update failure!");
		state_trigger_and_unlock(NETWORK_ERROR);
		break;

	case LWM2M_RD_CLIENT_EVENT_REG_UPDATE_COMPLETE:
		LOG_DBG("Registration update complete");
		state_trigger_and_unlock(CONNECTED);
		break;

	case LWM2M_RD_CLIENT_EVENT_DEREGISTER_FAILURE:
		LOG_DBG("Deregister failure!");
		reconnect = true;
		state_trigger_and_unlock(NETWORK_ERROR);
		break;

	case LWM2M_RD_CLIENT_EVENT_DISCONNECT:
		LOG_DBG("Disconnected");
		state_set_and_unlock(START);
		break;

	case LWM2M_RD_CLIENT_EVENT_QUEUE_MODE_RX_OFF:
		LOG_DBG("Queue mode RX window closed");
		if (IS_ENABLED(CONFIG_LWM2M_CLIENT_UTILS_RAI)) {
			lwm2m_rai_last();
		}
		k_mutex_unlock(&lte_mutex);
		break;

	case LWM2M_RD_CLIENT_EVENT_ENGINE_SUSPENDED:
		LOG_DBG("LwM2M engine suspended");
		k_mutex_unlock(&lte_mutex);
		break;

	case LWM2M_RD_CLIENT_EVENT_NETWORK_ERROR:
		LOG_ERR("LwM2M engine reported a network error.");
		reconnect = true;
		state_trigger_and_unlock(NETWORK_ERROR);
		break;
	}
}
static int lwm2m_setup(void)
{

	#if defined(CONFIG_LWM2M_CLIENT_UTILS_DEVICE_OBJ_SUPPORT)
	/* Manufacturer independent */
	lwm2m_init_device();
	#endif


	lwm2m_init_security(&client, endpoint_name, NULL);

	#if defined(CONFIG_LWM2M_CLIENT_UTILS_CELL_CONN_OBJ_SUPPORT)
	lwm2m_init_cellular_connectivity_object();
	#endif
	
	#if defined(CONFIG_LWM2M_APP_LIGHT_CONTROL)
	lwm2m_init_light_control();
	#endif

	#if defined(CONFIG_LWM2M_APP_PUSH_BUTTON)
		lwm2m_init_push_button();
	#endif

	#if defined(CONFIG_LWM2M_APP_BUZZER)
		lwm2m_init_buzzer();
	#endif

	if (IS_ENABLED(CONFIG_LWM2M_CLIENT_UTILS_RAI)) {
		lwm2m_init_rai();
	}
	return 0;
}
static void suspend_lwm2m_engine(void)
{
	int ret;

	state_trigger_and_unlock(LTE_OFFLINE);
	ret = lwm2m_engine_pause();
	if (ret) {
		LOG_ERR("LwM2M engine pause fail %d", ret);
		reconnect = true;
		k_mutex_lock(&lte_mutex, K_FOREVER);
		state_trigger_and_unlock(NETWORK_ERROR);
	}
}

static void modem_connect(void)
{
	int ret;

#if defined(CONFIG_LWM2M_QUEUE_MODE_ENABLED)
	ret = lte_lc_psm_req(true);
	if (ret < 0) {
		LOG_ERR("lte_lc_psm_req, error: (%d)", ret);
	} else {
		LOG_INF("PSM mode requested");
	}
#endif

	do {

		LOG_INF("Connecting to network.");
		LOG_INF("This may take several minutes.");

		ret = lte_lc_connect();
		if (ret < 0) {
			LOG_WRN("Failed to establish LTE connection (%d).", ret);
			LOG_WRN("Will retry in a minute.");
			lte_lc_offline();
			k_sleep(K_SECONDS(60));
		} else {
			enum lte_lc_lte_mode mode;

			lte_lc_lte_mode_get(&mode);
			if (mode == LTE_LC_LTE_MODE_NBIOT) {
				LOG_INF("Connected to NB-IoT network");
			} else if (mode == LTE_LC_LTE_MODE_LTEM) {
				LOG_INF("Connected to LTE network");
			} else  {
				LOG_INF("Connected to unknown network");
			}
		}
	} while (ret < 0);
}

static bool lte_connected(enum lte_lc_nw_reg_status nw_reg_status)
{
	if ((nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME) ||
	    (nw_reg_status == LTE_LC_NW_REG_REGISTERED_ROAMING)) {
		return true;
	}

	return false;
}

static void lwm2m_lte_reg_handler_notify(enum lte_lc_nw_reg_status nw_reg_status)
{
	bool lte_registered;

	LOG_DBG("LTE NW status: %d", nw_reg_status);
	k_mutex_lock(&lte_mutex, K_FOREVER);
	lte_registered = lte_connected(nw_reg_status);
	if (lte_registered != modem_connected_to_network) {
		modem_connected_to_network = lte_registered;
		if (client_state != START && client_state != BOOTSTRAP) {
			k_sem_give(&state_mutex);
		}
	}
	k_mutex_unlock(&lte_mutex);
}

static void lte_notify_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		lwm2m_lte_reg_handler_notify(evt->nw_reg_status);
		break;
	default:
		break;
	}
}


void main( void )
{
    int ret;
	uint32_t bootstrap_flags = 0;
    LOG_INF("Run LWM2M client\n\r");

    #if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
    configureLeds();
    k_sleep( K_SECONDS( 10 ) );

    if( ledRed.port )
    {
        gpio_pin_set_dt( &ledRed, 100 );
    }
    #endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */


	if (strlen(CONFIG_NCE_ICCID) < 1) {
		LOG_ERR("[1NCE] Failed to read CONFIG_NCE_ICCID ");
		return;
	}

	ret = app_event_manager_init();
	if (ret) {
		LOG_ERR("Unable to init Application Event Manager (%d)", ret);
		return;
	}

    #if !defined( CONFIG_NRF_MODEM_LIB_SYS_INIT )
    err = nrf_modem_lib_init( NORMAL_MODE );

    if( err < 0 )
    {
        LOG_ERR( "Unable to init modem library (%d)", err );
        return;
    }
    #endif

	LOG_INF("Initializing modem.");
	err = lte_lc_init();
	if (err < 0) {
		LOG_ERR("Unable to init modem (%d)", err);
		return;
	}	
	
	lte_lc_register_handler(lte_notify_handler);


    LOG_INF("endpoint: %s", (char *)endpoint_name);
	err = modem_info_init();
	if (err < 0) {
		LOG_ERR("Unable to init modem_info (%d)", err);
		return;
	}

        LOG_INF( "Disconnecting from the network to store credentials\n" );
        err = lte_lc_offline();

        if( err )
        {
            LOG_ERR( "Failed to disconnect from the LTE network, err %d\n", err );
            return;
        }

        err = store_credentials();

        if( err )
        {
            LOG_ERR( "Failed to store credentials, err %d\n", errno );
            return;
        }

        LOG_INF( "Reconnecting after storing credentials.. " );
        err = lte_lc_connect();

        if( err )
        {
            LOG_ERR( "Failed to connect to the LTE network, err %d\n", err );
            return;
        }



	/* Setup LwM2M */
	ret = lwm2m_setup();
	if (ret < 0) {
		LOG_ERR("Failed to setup LWM2M fields (%d)", ret);
		return;
	}
	modem_connect();
	while (true) {
		#if defined(CONFIG_LWM2M_RD_CLIENT_SUPPORT_BOOTSTRAP)
			bootstrap_flags = LWM2M_RD_CLIENT_FLAG_BOOTSTRAP;
		#else
			bootstrap_flags = 0;
		#endif

		k_mutex_lock(&lte_mutex, K_FOREVER);

		switch (client_state) {
		case START:
			LOG_INF("Client connect to server");
			state_set_and_unlock(CONNECTING);
			lwm2m_rd_client_start(&client, endpoint_name, bootstrap_flags,
					      rd_client_event, NULL);
			break;

		case BOOTSTRAP:
			state_set_and_unlock(BOOTSTRAP);
			LOG_INF("LwM2M is boosttrapping");

			#if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
			if( ledRed.port )
			{
				gpio_pin_set_dt( &ledRed, 0 );
			}

			if( ledBlue.port )
			{
				gpio_pin_set_dt( &ledBlue, 100 );
			}
			#endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */

			break;

		case CONNECTING:
			LOG_INF("LwM2M is connecting to server");
			k_mutex_unlock(&lte_mutex);
			break;

		case CONNECTED:
			if (!modem_connected_to_network) {
				/* LTE connection down suspend LwM2M engine */
				suspend_lwm2m_engine();
			} else {
				k_mutex_unlock(&lte_mutex);
				LOG_INF("LwM2M is connected to server");
				if (update_session_lifetime) {
					/* Read a current server  lifetime value */
					rd_client_update_lifetime(client.srv_obj_inst);
				}
				/* Get current time and date */
				date_time_update_async(date_time_event_handler);
			}
			break;

		case LTE_OFFLINE:
			if (modem_connected_to_network) {
				state_trigger_and_unlock(CONNECTING);
				LOG_INF("Resume LwM2M engine");
				ret = lwm2m_engine_resume();
				if (ret) {
					LOG_ERR("LwM2M engine Resume fail %d", ret);
				}
			} else {
				LOG_INF("LTE Offline");
				k_mutex_unlock(&lte_mutex);
			}
			break;

		case NETWORK_ERROR:
			/* Stop the LwM2M engine. */
			state_trigger_and_unlock(START);
			lwm2m_rd_client_stop(&client, rd_client_event, false);

			/* Set network state to start for blocking LTE */
			if (reconnect) {
				reconnect = false;

				LOG_INF("LwM2M restart requested. The sample will try to"
					" re-establish network connection.");

				/* Try to reconnect to the network. */
				ret = lte_lc_offline();
				if (ret < 0) {
					LOG_ERR("Failed to put LTE link in offline state (%d)",
						ret);
				}
				modem_connect();
			}
			break;
		}

		/* Wait for statmachine update event */
		k_sem_take(&state_mutex, K_FOREVER);
	}   

}
