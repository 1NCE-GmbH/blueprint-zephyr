/*
 * Copyright (c) 2024 1NCE
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/random/random.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <memfault/metrics/metrics.h>
#include <memfault/metrics/platform/overrides.h>
#include <memfault/core/data_packetizer.h>
#include <memfault/core/trace_event.h>
#include <memfault_ncs.h>
#include <dk_buttons_and_leds.h>
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <nce_iot_c_sdk.h>
#include <network_interface_zephyr.h>
#include <memfault_interface_zephyr.h>


#if defined( CONFIG_NCE_MEMFAULT_DEMO_ENABLE_DTLS )
    #include <modem/modem_key_mgmt.h>
    #include <nrf_modem_at.h>
    #include <zephyr/net/tls_credentials.h>
    #define MAX_PSK_SIZE    100
    extern void sys_arch_reboot(int type);
#endif /* if defined( CONFIG_NCE_MEMFAULT_DEMO_ENABLE_DTLS ) */

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

LOG_MODULE_REGISTER( nce_memfault_demo, CONFIG_NCE_MEMFAULT_DEMO_LOG_LEVEL );

static K_SEM_DEFINE( lte_connected_sem, 0, 1 );

static struct k_work_delayable coap_transmission_work;

bool connected = false;

bool virtual_switch = false; 

#if defined( CONFIG_NCE_MEMFAULT_DEMO_ENABLE_DTLS )
/* Security tag for DTLS */
const sec_tag_t tls_sec_tag[] =
{
    CONFIG_NCE_SDK_DTLS_SECURITY_TAG,
};
DtlsKey_t nceKey = { 0 };
#endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

/* LTE event handler */
static void lte_handler( const struct lte_lc_evt *const evt )
{
    switch( evt->type )
    {
        case LTE_LC_EVT_NW_REG_STATUS:

            if( ( evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME ) &&
                ( evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING ) )
            {
                connected = false;
                break;
            }

            connected = true;
            LOG_INF( "Network registration status: %s\n",
                     evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
                     "Connected - home" : "Connected - roaming" );
            k_sem_give( &lte_connected_sem );
            break;

        case LTE_LC_EVT_RRC_UPDATE:
            LOG_INF( "RRC mode: %s\n",
                     evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle\n" );
            break;

        case LTE_LC_EVT_CELL_UPDATE:
            LOG_INF( "LTE cell changed: Cell ID: %d, Tracking area: %d\n",
                     evt->cell.id, evt->cell.tac );
            break;

        default:
            break;
    }
}


/* Recursive Fibonacci calculation used to trigger stack overflow */
static int fib( int n )
{
    if( n <= 1 )
    {
        return n;
    }

    return fib( n - 1 ) + fib( n - 2 );
}

/* Handle button presses and trigger faults that can be captured and sent to
 * the Memfault cloud for inspection after rebooting:
 * Only button 1 is available on Thingy:91, the rest are available on nRF9160 DK.
 *	Button 1: Trigger stack overflow.
 *	Button 2: Trigger NULL-pointer dereference.
 *	Switch 1: Increment switch_1_toggle_count metric by one.
 *	Switch 2: Trace switch_2_toggled event, along with switch state.
 */
static void button_handler( uint32_t button_states,
                            uint32_t has_changed )
{
    uint32_t buttons_pressed = has_changed & button_states;

    if( buttons_pressed & DK_BTN1_MSK )
    {
        LOG_WRN( "Stack overflow will now be triggered" );
        fib( 10000 );
    }
    else if( buttons_pressed & DK_BTN2_MSK )
    {
        volatile uint32_t i;

        LOG_WRN( "Division by zero will now be triggered" );
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wdiv-by-zero"
        i = 1 / 0;
        #pragma GCC diagnostic pop
        ARG_UNUSED( i );
    }
    else if( has_changed & DK_BTN3_MSK )
    {
        /* DK_BTN3_MSK is Switch 1 on nRF9160 DK */
        int err = MEMFAULT_METRIC_ADD( switch_1_toggle_count, 1 );

        if( err )
        {
            LOG_ERR( "Failed to increment switch_1_toggle_count" );
        }
        else
        {
            LOG_INF( "switch_1_toggle_count incremented" );
        }
    }
    else if( has_changed & DK_BTN4_MSK )
    {
        /* DK_BTN4_MSK is Switch 2 on nRF9160 DK */
        MEMFAULT_TRACE_EVENT_WITH_LOG( switch_2_toggled, "Switch state: %d",
                                       buttons_pressed & DK_BTN4_MSK ? 1 : 0 );
        LOG_INF( "switch_2_toggled event has been traced, button state: %d",
                 buttons_pressed & DK_BTN4_MSK ? 1 : 0 );
    }
}

#if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
void configureLeds()
{
    int ret = 0;

    if( ledRed.port && !device_is_ready( ledRed.port ) )
    {
        LOG_ERR( "Error %d: LED device %s is not ready; ignoring it\n",
                ret, ledRed.port->name );
        ledRed.port = NULL;
    }

    if( ledRed.port )
    {
        ret = gpio_pin_configure_dt( &ledRed, GPIO_OUTPUT );

        if( ret != 0 )
        {
            LOG_ERR( "Error %d: failed to configure LED device %s pin %d\n",
                    ret, ledRed.port->name, ledRed.pin );
            ledRed.port = NULL;
        }
    }

    if( ledGreen.port && !device_is_ready( ledGreen.port ) )
    {
        LOG_ERR( "Error %d: LED device %s is not ready; ignoring it\n",
                ret, ledGreen.port->name );
        ledGreen.port = NULL;
    }

    if( ledGreen.port )
    {
        ret = gpio_pin_configure_dt( &ledGreen, GPIO_OUTPUT );

        if( ret != 0 )
        {
            LOG_ERR( "Error %d: failed to configure LED device %s pin %d\n",
                    ret, ledGreen.port->name, ledGreen.pin );
            ledGreen.port = NULL;
        }
    }

    if( ledBlue.port && !device_is_ready( ledBlue.port ) )
    {
        LOG_ERR( "Error %d: LED device %s is not ready; ignoring it\n",
                ret, ledBlue.port->name );
        ledBlue.port = NULL;
    }

    if( ledBlue.port )
    {
        ret = gpio_pin_configure_dt( &ledBlue, GPIO_OUTPUT );

        if( ret != 0 )
        {
            LOG_ERR( "Error %d: failed to configure LED device %s pin %d\n",
                    ret, ledBlue.port->name, ledBlue.pin );
            ledBlue.port = NULL;
        }
    }
}
#endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */

/**
 * Initialize the modem information interface, retrieve the
 * supported LTE bands and APN (Access Point Name), and set these values
 * as Memfault metrics. Errors are logged if initialization or retrieval fails.
 */
#if defined( CONFIG_NCE_MEMFAULT_DEMO_CONNECTIVITY_METRICS )
static int init_network_info( void )
{
    int err;
    char bands[ MODEM_INFO_MAX_RESPONSE_SIZE ];
    char apn[ MODEM_INFO_MAX_RESPONSE_SIZE ];

    err = modem_info_init();

    if( err )
    {
        LOG_ERR( "Failed to initialize modem info" );
        return err;
    }

    err = modem_info_string_get( MODEM_INFO_SUP_BAND,
                                 bands,
                                 MODEM_INFO_MAX_RESPONSE_SIZE );

    if( err < 0 )
    {
        LOG_WRN( "Failed to get supported bands" );
    }
    else
    {
        err = MEMFAULT_METRIC_SET_STRING( ncs_lte_nce_bands, bands );

        if( err )
        {
            LOG_ERR( "Failed to set supported bands metric" );
        }
    }

    err = modem_info_string_get( MODEM_INFO_APN,
                                 apn,
                                 MODEM_INFO_MAX_RESPONSE_SIZE );

    if( err < 0 )
    {
        LOG_WRN( "Failed to get APN" );
    }
    else
    {
        err = MEMFAULT_METRIC_SET_STRING( ncs_lte_nce_apn, apn );

        if( err )
        {
            LOG_ERR( "Failed to set APN metric" );
        }
    }

    return 0;
}

/**
 * Collect and update network information metrics, such as RSRP (Reference Signal Received Power),
 * current band, operator name, and connectivity statistics (transmitted and received kilobytes).
 * The data is updated in Memfault metrics for further diagnostics or monitoring.
 */
static int get_network_info( void )
{
    int err;
    int rsrp;
    uint8_t band;
    char operator[ MODEM_INFO_MAX_RESPONSE_SIZE ];

    err = modem_info_get_rsrp( &rsrp );

    if( err < 0 )
    {
        LOG_WRN( "Failed to get RSRP" );
    }
    else
    {
        err = MEMFAULT_METRIC_SET_SIGNED( ncs_lte_nce_rsrp_dbm, rsrp );

        if( err )
        {
            LOG_ERR( "Failed to set RSRP metric" );
        }
    }

    err = modem_info_get_current_band( &band );

    if( err < 0 )
    {
        LOG_WRN( "Failed to get current band" );
    }
    else
    {
        err = MEMFAULT_METRIC_SET_UNSIGNED( ncs_lte_nce_current_band, band );

        if( err )
        {
            LOG_ERR( "Failed to set current band" );
        }
    }

    err = modem_info_string_get( MODEM_INFO_OPERATOR,
                                 operator,
                                 MODEM_INFO_MAX_RESPONSE_SIZE );

    if( err < 0 )
    {
        LOG_WRN( "Failed to get operator name" );
    }
    else
    {
        err = MEMFAULT_METRIC_SET_STRING( ncs_lte_nce_operator, operator );

        if( err )
        {
            LOG_ERR( "Failed to set operator metric" );
        }
    }

    int tx_kbytes;
    int rx_kybtes;
    err = modem_info_get_connectivity_stats( &tx_kbytes, &rx_kybtes );

    if( err )
    {
        LOG_WRN( "Failed to collect connectivity stats, error: %d", err );
    }
    else
    {
        err = MEMFAULT_METRIC_SET_UNSIGNED( ncs_lte_tx_kilobytes, tx_kbytes );

        if( err )
        {
            LOG_ERR( "Failed to set ncs_lte_tx_kilobytes" );
        }

        err = MEMFAULT_METRIC_SET_UNSIGNED( ncs_lte_rx_kilobytes, rx_kybtes );

        if( err )
        {
            LOG_ERR( "Failed to set ncs_lte_rx_kilobytes" );
        }
    }

    return 0;
}

/* Collect and log heartbeat metrics for diagnostics */
void memfault_metrics_heartbeat_collect_data( void )
{
    LOG_INF( "memfault_metrics_heartbeat_collect_data" );
    get_network_info();
    #if defined( CONFIG_NCE_MEMFAULT_DEMO_PRINT_HEARTBEAT_METRICS )
    memfault_metrics_heartbeat_debug_print();
    #endif /* CONFIG_NCE_MEMFAULT_DEMO_PRINT_HEARTBEAT_METRICS */
}
#endif /* CONFIG_NCE_MEMFAULT_DEMO_CONNECTIVITY_METRICS */

#if defined( CONFIG_NCE_MEMFAULT_DEMO_ENABLE_DTLS )
/* Store DTLS Credentials in the modem */
int store_credentials( void )
{
    int err;
    char psk_hex[ MAX_PSK_SIZE ];
    int cred_len;

    /* Convert PSK to HEX */
    cred_len = bin2hex( nceKey.Psk, strlen( nceKey.Psk ), psk_hex, sizeof( psk_hex ) );

    if( cred_len == 0 )
    {
        LOG_ERR( "PSK is too large to convert (%d)", -EOVERFLOW );
        return -EOVERFLOW;
    }

    /* Store DTLS Credentials */
    err = modem_key_mgmt_write( CONFIG_NCE_SDK_DTLS_SECURITY_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK, psk_hex, sizeof( psk_hex ) );
    LOG_DBG( "psk status: %d\n", err );

    err = modem_key_mgmt_write( CONFIG_NCE_SDK_DTLS_SECURITY_TAG, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY, nceKey.PskIdentity, sizeof( nceKey.PskIdentity ) );
    LOG_DBG( "psk_id status: %d\n", err );

    return err;
}

/**
 * @brief Onboard the device by managing DTLS credentials.
 *
 * @param[in] overwrite Whether to overwrite existing credentials, should be set to true when DTLS connecting is failing. 
 * @return 0 on success, negative error code on failure.
 */
static int prv_onboard_device( bool overwrite )
{
    /* If the configured tag contains DTLS credentials, the onboarding process is skipped */
    /* Unless "overwrite" is true,  */
    int err;
    bool exists;
    err = modem_key_mgmt_exists( CONFIG_NCE_SDK_DTLS_SECURITY_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK, &exists );

    if( overwrite || !exists )
    {
        struct OSNetwork xOSNetwork = { .os_socket = 0 };
        os_network_ops_t osNetwork =
        {
            .os_socket             = &xOSNetwork,
            .nce_os_udp_connect    = nce_os_connect,
            .nce_os_udp_send       = nce_os_send,
            .nce_os_udp_recv       = nce_os_recv,
            .nce_os_udp_disconnect = nce_os_disconnect
        };


        err = os_auth( &osNetwork, &nceKey );

        if( err )
        {
            LOG_ERR( "1NCE SDK onboarding failed, err %d\n", errno );
            return err;
        }

        LOG_INF( "Disconnecting from the network to store credentials\n" );

        err = lte_lc_offline();

        if( err )
        {
            LOG_ERR( "Failed to disconnect from the LTE network, err %d\n", err );
            return err;
        }

        err = store_credentials();

        if( err )
        {
            LOG_ERR( "Failed to store credentials, err %d\n", errno );
            return err;
        }

        LOG_INF( "Rebooting to ensure changes take effect after saving credentials.." );

       sys_arch_reboot(0);

    }
    else
    {
        LOG_INF( "Device is already onboarded \n" );
    }

    return err;

}

/* Handles DTLS failure by onboarding the device with overwriting enabled  */
static int prv_handle_dtls_failure( void )
{
    int err;
    
    /* Onboard the device with overwrtiting enabled */
    err = prv_onboard_device(true);
    if( err )
    {
        LOG_ERR( "Device onboarding failed, err %d\n", err );
    }
    return err;

}
#endif /* if defined( CONFIG_NCE_MEMFAULT_DEMO_ENABLE_DTLS ) */

/* Handle the result of sending Memfault data and update relevant metrics  */
static void prv_handle_memfault_send_result( int res )
{
    if( res )
    {
        LOG_ERR( "Failed to send Memfault data to 1NCE CoAP Proxy, err %d\n", res );
        LOG_ERR( "SYNC_FAILURE" );
        #if defined( CONFIG_NCE_MEMFAULT_DEMO_COAP_SYNC_METRICS )
        MEMFAULT_METRIC_ADD( sync_memfault_failure, 1 );
        #endif

        #if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
        if( ledRed.port )
        {
            gpio_pin_set_dt( &ledRed, 25 );
        }

        if( ledGreen.port )
        {
            gpio_pin_set_dt( &ledGreen, 0 );
        }

        if( ledBlue.port )
        {
            gpio_pin_set_dt( &ledBlue, 0 );
        }
        #endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */

        #if defined( CONFIG_NCE_MEMFAULT_DEMO_ENABLE_DTLS )
        if( res == NCE_SDK_DTLS_CONNECT_ERROR )
        {
            LOG_ERR( "DTLS connection failed, Updating DTLS credentials, err %d\n", res );
            prv_handle_dtls_failure();            
        }
        #endif /* if defined( CONFIG_NCE_MEMFAULT_DEMO_ENABLE_DTLS ) */
    }
    else
    {
        LOG_INF( "Successfully synchronized Memfault data via 1NCE CoAP Proxy\n\n" );
        LOG_INF( "SYNC_SUCCESS" );
        #if defined( CONFIG_NCE_MEMFAULT_DEMO_COAP_SYNC_METRICS )
        MEMFAULT_METRIC_ADD( sync_memfault_successful, 1 );
        #endif
        
        #if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
        if( ledRed.port )
        {
            gpio_pin_set_dt( &ledRed, 0 );
        }

        if( ledGreen.port )
        {
            gpio_pin_set_dt( &ledGreen, 25 );
        }

        if( ledBlue.port )
        {
            gpio_pin_set_dt( &ledBlue, 0 );
        }
        #endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */

    }

}

#if defined( CONFIG_NCE_MEMFAULT_DEMO_PERIODIC_UPDATE )
/* Handles periodic transmission of Memfault data via 1NCE CoAP Proxy  */
static void coap_transmission_work_fn( struct k_work * work )
{
    if( !memfault_packetizer_data_available() )
    {
        LOG_INF( "No Memfault Data" );
        goto end;
    }

    if( !connected )
    {
        LOG_ERR( "Not Connected!" );
        LOG_ERR( "SYNC_FAILURE" );
        #if defined( CONFIG_NCE_MEMFAULT_DEMO_COAP_SYNC_METRICS )
        MEMFAULT_METRIC_ADD( sync_memfault_failure, 1 );
        #endif

        goto end;
    }

    LOG_INF( "Posting Memfault Data to 1NCE CoAP Proxy" );

    int res = os_memfault_send();

    prv_handle_memfault_send_result(res);

end:
    k_work_schedule( &coap_transmission_work,
                     K_SECONDS( CONFIG_NCE_MEMFAULT_DEMO_PERIODIC_UPDATE_FREQUENCY_SECONDS ) );
    return;
}
#endif /* if defined( CONFIG_NCE_MEMFAULT_DEMO_PERIODIC_UPDATE ) */

/**
 * Handle operations to be performed upon a successful LTE connection, including metrics collection,
 * device onboarding for DTLS, and data synchronization with Memfault.
 */
static void on_connect( void )
{

    #if IS_ENABLED( MEMFAULT_NCS_LTE_METRICS )
    uint32_t time_to_lte_connection;
    
    /* Retrieve the LTE time to connect metric */
    memfault_metrics_heartbeat_timer_read( MEMFAULT_METRICS_KEY( ncs_lte_time_to_connect_ms ),
                                           &time_to_lte_connection );

    LOG_INF( "Time to connect: %d ms", time_to_lte_connection );
    #endif /* IS_ENABLED(MEMFAULT_NCS_LTE_METRICS) */

    #if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
    configureLeds();
    if( ledBlue.port )
    {
        gpio_pin_set_dt( &ledBlue, 25 );
    }
    #endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */

    #if defined( CONFIG_NCE_MEMFAULT_DEMO_ENABLE_DTLS )
    /* Onboard the device */
    int err = prv_onboard_device(false);
    if( err )
    {
        LOG_ERR( "Deviec onboarding failed, err %d\n", err );
        return;
    }
    #endif /* if defined( CONFIG_NCE_MEMFAULT_DEMO_ENABLE_DTLS ) */

    LOG_INF( "Sending already captured data to Memfault" );

    #if defined( CONFIG_NCE_MEMFAULT_DEMO_CONNECTIVITY_METRICS )
    init_network_info();
    #endif /* CONFIG_NCE_MEMFAULT_DEMO_CONNECTIVITY_METRICS */

    /* Trigger collection of heartbeat data */
    memfault_metrics_heartbeat_debug_trigger();

    /* Check if there is any data available to be sent */
    if( !memfault_packetizer_data_available() )
    {
        LOG_DBG( "There was no data to be sent" );
        return;
    }

    int res = os_memfault_send();

    prv_handle_memfault_send_result(res);

    #if defined( CONFIG_NCE_MEMFAULT_DEMO_PERIODIC_UPDATE )
    k_work_schedule( &coap_transmission_work,
                     K_SECONDS( CONFIG_NCE_MEMFAULT_DEMO_PERIODIC_UPDATE_FREQUENCY_SECONDS ) );
    #endif /* if defined( CONFIG_NCE_MEMFAULT_DEMO_PERIODIC_UPDATE ) */
}


/* Send Memfault data via the 1NCE CoAP proxy (CLI Command) */
static int post_data_cmd( const struct shell * shell,
                      size_t argc,
                      char ** argv )
{
    LOG_INF( "Posting Memfault Data via 1NCE CoAP Proxy" );

    if( !connected )
    {
        LOG_ERR( "Not Connected!" );
        LOG_ERR( "SYNC_FAILURE" );
        #if defined( CONFIG_NCE_MEMFAULT_DEMO_COAP_SYNC_METRICS )
        MEMFAULT_METRIC_ADD( sync_memfault_failure, 1 );
        #endif
        return 0;
    }

    int res = os_memfault_send();

    prv_handle_memfault_send_result(res);

    return res;
}

/* Disconnects and reconnects to the LTE network with a delay (CLI Command) */
static int disconnect_cmd( const struct shell * shell,
                       size_t argc,
                       char ** argv )
{
    LOG_INF( "Disconnecting" );

    int err = lte_lc_offline();

    if( err )
    {
        LOG_ERR( "Failed to disconnect from the LTE network, err %d\n", err );
        return err;
    }

    k_sleep( K_SECONDS( CONFIG_NCE_MEMFAULT_DEMO_DISCONNECT_DURATION_SECONDS ) );

    LOG_INF( "Connecting" );

    err = lte_lc_connect();

    if( err )
    {
        LOG_ERR( "Failed to connect to the LTE network, err %d\n", err );
        return err;
    }

    return 0;
}


/* Trigger Division by zero (CLI Command) */
static void div_by_0_cmd( const struct shell * shell,
                      size_t argc,
                      char ** argv )
{
        volatile uint32_t i;
        LOG_WRN( "Division by zero will now be triggered" );
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wdiv-by-zero"
        i = 1 / 0;
        #pragma GCC diagnostic pop
        ARG_UNUSED( i );
}

/* Increment switch_1_toggle_count (CLI Command) */
static void sw1_cmd( const struct shell * shell,
                      size_t argc,
                      char ** argv )
{
    int err = MEMFAULT_METRIC_ADD( switch_1_toggle_count, 1 );

    if( err )
    {
        LOG_ERR( "Failed to increment switch_1_toggle_count" );
    }
    else
    {
        LOG_INF( "switch_1_toggle_count incremented" );
    }
}

/* Trigger switch_2_toggled event (CLI Command) */
static void sw2_cmd( const struct shell * shell,
                      size_t argc,
                      char ** argv )
{
    MEMFAULT_TRACE_EVENT_WITH_LOG( switch_2_toggled, "Switch state: %d",
                                    virtual_switch ? 1 : 0 );
    virtual_switch = !virtual_switch;
    LOG_INF( "switch_2_toggled event has been traced, button state: %d",
                virtual_switch ? 1 : 0 );
}

/* Shell command set definition */
SHELL_STATIC_SUBCMD_SET_CREATE(
    nce_cmds, SHELL_CMD( post_chunks, NULL, "Post Memfault data to cloud via 1NCE CoAP Proxy", post_data_cmd ),
    SHELL_CMD( divby0, NULL, "Trigger Division by zero", div_by_0_cmd ),
    SHELL_CMD( sw1, NULL, "Increment switch_1_toggle_count", sw1_cmd ),
    SHELL_CMD( sw2, NULL, "Trigger switch_2_toggled event", sw2_cmd ),
    SHELL_CMD( disconnect, NULL, "Disconnect from the LTE Network", disconnect_cmd ),
    SHELL_SUBCMD_SET_END
    );

SHELL_CMD_REGISTER( nce, &nce_cmds, "1NCE Memfault Demo Test Commands", NULL );

int main( void )
{
    int err;

    LOG_INF( "1NCE Memfault demo has started" );

    /* Initialization */

    err = dk_buttons_init( button_handler );

    if( err )
    {
        LOG_ERR( "dk_buttons_init, error: %d", err );
        return err;
    }

    err = nrf_modem_lib_init();

    if( err )
    {
        LOG_ERR( "Failed to initialize modem library, error: %d\n", err );
        return err;
    }

    #if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
    configureLeds();
    if( ledRed.port )
    {
        gpio_pin_set_dt( &ledRed, 0 );
    }
    if( ledGreen.port )
    {
        gpio_pin_set_dt( &ledGreen, 0 );
    }
    if( ledBlue.port )
    {
        gpio_pin_set_dt( &ledBlue, 0 );
    }
    #endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */

    /* Connecting to LTE Network */

    LOG_INF( "Connecting to LTE network" );

    err = lte_lc_connect_async( lte_handler );

    if( err )
    {
        LOG_ERR( "Failed to connect to LTE network, error: %d\n", err );
        return err;
    }

    #if defined( CONFIG_NCE_MEMFAULT_DEMO_PERIODIC_UPDATE )
    k_work_init_delayable( &coap_transmission_work,
                           coap_transmission_work_fn );
    #endif /* if defined( CONFIG_NCE_MEMFAULT_DEMO_PERIODIC_UPDATE ) */

    while( 1 )
    {
        k_sem_take( &lte_connected_sem, K_FOREVER );
        LOG_INF( "Connected to network" );
        on_connect();
    }
}
