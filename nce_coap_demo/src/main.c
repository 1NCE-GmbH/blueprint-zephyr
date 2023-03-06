#include <string.h>
#include <zephyr/kernel.h>
#include <stdlib.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/coap.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <nce_iot_c_sdk.h>
#include <udp_interface_zephyr.h>
#include <zephyr/logging/log.h>

#if defined( CONFIG_NCE_ENABLE_DTLS )
    #include <modem/modem_key_mgmt.h>
    #include <nrf_modem_at.h>
    #include <zephyr/net/tls_credentials.h>
#endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */


LOG_MODULE_REGISTER( COAP_CLIENT, CONFIG_LOG_DEFAULT_LEVEL );


#define APP_COAP_SEND_INTERVAL_MS    5000
#define APP_COAP_MAX_MSG_LEN         1280
#define APP_COAP_VERSION             1
#define MAX_COAP_MSG_LEN             256

#if defined( CONFIG_NCE_ENABLE_DTLS )
/* Security tag for DTLS */
const sec_tag_t tls_sec_tag[] =
{
    CONFIG_DTLS_SECURITY_TAG,
};
DtlsKey_t nceKey = { 0 };
#endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

static int sock;
int err;
int fd;
char * p;
int bytes;
size_t off;
struct addrinfo * res;
struct addrinfo hints =
{
    .ai_family   = AF_INET,
    .ai_socktype = SOCK_DGRAM,
};


static struct k_work_delayable coap_transmission_work;

#if defined( CONFIG_NCE_ENABLE_DTLS )

/* Store DTLS Credentials in the modem */
int store_credentials( void )
{
    int err;
    char psk_hex[ 40 ];
    int cred_len;

    /* Convert PSK to HEX */
    cred_len = bin2hex( nceKey.Psk, strlen( nceKey.Psk ), psk_hex, sizeof( psk_hex ) );

    if( cred_len == 0 )
    {
        LOG_ERR( "PSK is too large to convert (%d)", -EOVERFLOW );
        return -EOVERFLOW;
    }

    /* Store DTLS Credentials */
    err = modem_key_mgmt_write( CONFIG_DTLS_SECURITY_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK, psk_hex, sizeof( psk_hex ) );
    LOG_DBG( "psk status: %d\n", err );

    err = modem_key_mgmt_write( CONFIG_DTLS_SECURITY_TAG, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY, nceKey.PskIdentity, sizeof( nceKey.PskIdentity ) );
    LOG_DBG( "psk_id status: %d\n", err );

    return err;
}

/* Configure DTLS socket */
int dtls_setup( int fd )
{
    int err;
    int verify;
    int role;

    /* Set up DTLS peer verification */
    enum
    {
        NONE = 0,
        OPTIONAL = 1,
        REQUIRED = 2,
    };

    verify = NONE;

    err = zsock_setsockopt( fd, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof( verify ) );

    if( err )
    {
        LOG_ERR( "Failed to setup peer verification, err %d\n", errno );
        return err;
    }

    /* Set up DTLS client mode */
    enum
    {
        CLIENT = 0,
        SERVER = 1,
    };

    role = CLIENT;

    err = zsock_setsockopt( fd, SOL_TLS, TLS_DTLS_ROLE, &role, sizeof( role ) );

    if( err )
    {
        LOG_ERR( "Failed to setup DTLS role, err %d\n", errno );
        return err;
    }

    /* Associate the socket with the security tag */
    err = zsock_setsockopt( fd, SOL_TLS, TLS_SEC_TAG_LIST, tls_sec_tag,
                            sizeof( tls_sec_tag ) );

    if( err )
    {
        LOG_ERR( "Failed to setup TLS sec tag, err %d\n", errno );
        return err;
    }

    return 0;
}

#endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */


static int send_coap_post_request( uint8_t * payload,
                                   uint16_t payload_len )
{
    struct coap_packet request;
    uint8_t * data;

    data = ( uint8_t * ) k_malloc( MAX_COAP_MSG_LEN );

    if( !data )
    {
        return -ENOMEM;
    }

    int r;

    r = coap_packet_init( &request, data, MAX_COAP_MSG_LEN,
                          COAP_VERSION_1, COAP_TYPE_NON_CON,
                          COAP_TOKEN_MAX_LEN, coap_next_token(),
                          COAP_METHOD_POST, coap_next_id() );

    if( r < 0 )
    {
        LOG_ERR( "Failed to init CoAP message" );
        goto end;
    }

    r = coap_packet_append_option( &request, COAP_OPTION_URI_QUERY,
                                   CONFIG_COAP_URI_QUERY, strlen( CONFIG_COAP_URI_QUERY ) );

    if( r < 0 )
    {
        LOG_ERR( "Unable add option to request" );
        goto end;
    }

    r = coap_packet_append_payload_marker( &request );

    if( r < 0 )
    {
        LOG_ERR( "Unable to append payload marker" );
        goto end;
    }

    r = coap_packet_append_payload( &request, ( uint8_t * ) payload,
                                    payload_len );

    if( r < 0 )
    {
        LOG_ERR( "Unable to append payload" );
        goto end;
    }

    r = zsock_send( sock, request.data, request.offset, 0 );

    if( r < 0 )
    {
        LOG_ERR( "Unable to send CoAP packet" );
        goto end;
    }

end:
    k_free( data );
    return r;
}

#if !defined( CONFIG_NCE_ENERGY_SAVER )
/* Create Payload and send it through CoAP */
int create_and_send_payload( void )
{
    int err = 0;

    LOG_INF( "\nCoAP client POST (Payload) \n" );

    err = send_coap_post_request( CONFIG_PAYLOAD, strlen( CONFIG_PAYLOAD ) );

    return err;
}

#else /* if !defined( CONFIG_NCE_ENERGY_SAVER ) */
/* Create Binary Payload and send it through CoAP */
int create_and_send_binary_payload( void )
{
    int err = 0;
    int converted_bytes = 0;
    char message[ CONFIG_PAYLOAD_DATA_SIZE ];


    LOG_INF( "\nCoAP client POST (Binary Payload) \n" );


    Element2byte_gen_t battery_level = { .type = E_INTEGER, .value.i = 99, .template_length = 1 };
    Element2byte_gen_t signal_strength = { .type = E_INTEGER, .value.i = 84, .template_length = 1 };
    Element2byte_gen_t software_version = { .type = E_STRING, .value.s = "2.2.1", .template_length = 5 };

    converted_bytes = os_energy_save( message, 1, 3, battery_level, signal_strength, software_version );

    if( converted_bytes < 0 )
    {
        LOG_ERR( "os_energy_save error \n" );
        return converted_bytes;
    }

    else
    {
        err = send_coap_post_request( message, converted_bytes );
    }

    return err;
}
#endif /* if !defined( CONFIG_NCE_ENERGY_SAVER ) */



static void coap_transmission_work_fn( struct k_work * work )
{
    #if !defined( CONFIG_NCE_ENERGY_SAVER )
    err = create_and_send_payload();
    #else
    err = create_and_send_binary_payload();
    #endif

    if( err < 0 )
    {
        LOG_ERR( "\nCoAP client POST err %d \n", err );
    }
    else
    {
        LOG_INF( "\nCoAP client POST SUCCESS \n" );
        LOG_INF( "Waiting... \n" );
        k_work_schedule( &coap_transmission_work,
                         K_SECONDS( CONFIG_COAP_DATA_UPLOAD_FREQUENCY_SECONDS ) );
    }
}
static void work_init( void )
{
    k_work_init_delayable( &coap_transmission_work,
                           coap_transmission_work_fn );
}

void main( void )
{
    LOG_INF( "1NCE CoAP client sample started\n\r" );
    #if !defined( CONFIG_NRF_MODEM_LIB_SYS_INIT )
    err = nrf_modem_lib_init( NORMAL_MODE );

    if( err < 0 )
    {
        LOG_ERR( "Unable to init modem library (%d)", err );
        return;
    }
    #endif /* if !defined( CONFIG_NRF_MODEM_LIB_SYS_INIT ) */

    work_init();

    LOG_INF( "Waiting for network.. " );
    err = lte_lc_init_and_connect();

    if( err )
    {
        LOG_ERR( "Failed to connect to the LTE network, err %d\n", err );
        return;
    }

    LOG_INF( "OK\n" );

    #if defined( CONFIG_NCE_ENABLE_DTLS )
    bool exists;

    #if !defined( CONFIG_OVERWRITE_CREDENTIALS_IF_EXISTS )
    err = modem_key_mgmt_exists( CONFIG_DTLS_SECURITY_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK, &exists );
    #endif

    if( IS_ENABLED( CONFIG_OVERWRITE_CREDENTIALS_IF_EXISTS ) || ( ( err == 0 ) && !exists ) )
    {
        struct OSNetwork xOSNetwork = { .os_socket = 0 };
        os_network_ops_t osNetwork =
        {
            .os_socket             = &xOSNetwork,
            .nce_os_udp_connect    = nce_os_udp_connect,
            .nce_os_udp_send       = nce_os_udp_send,
            .nce_os_udp_recv       = nce_os_udp_recv,
            .nce_os_udp_disconnect = nce_os_udp_disconnect
        };


        err = os_auth( &osNetwork, &nceKey );

        if( err )
        {
            LOG_ERR( "SDK onboarding failed, err %d\n", errno );
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
    }
    #endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

    err = zsock_getaddrinfo( CONFIG_COAP_SERVER_HOSTNAME, NULL, &hints, &res );

    if( err )
    {
        LOG_ERR( "getaddrinfo() failed, err %d\n", errno );
        return;
    }

    ( ( struct sockaddr_in * ) res->ai_addr )->sin_port = htons( CONFIG_COAP_SERVER_PORT );

    #if defined( CONFIG_NCE_ENABLE_DTLS )
    fd = socket( AF_INET, SOCK_DGRAM, IPPROTO_DTLS_1_2 );
    #else
    fd = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    #endif

    if( fd == -1 )
    {
        LOG_ERR( "Failed to open socket!\n" );
        return;
    }

    #if defined( CONFIG_NCE_ENABLE_DTLS )
    /* Setup DTLS socket options */
    err = dtls_setup( fd );

    if( err )
    {
        LOG_ERR( "Failed to Configure DTLS!\n" );
        return;
    }
    #endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

    LOG_INF( "Connecting to %s\n", CONFIG_COAP_SERVER_HOSTNAME );

    err = zsock_connect( fd, res->ai_addr, sizeof( struct sockaddr_in ) );

    if( err )
    {
        LOG_ERR( "Failed to Connect to CoAP Server!\n" );
        return;
    }

    k_work_schedule( &coap_transmission_work, K_NO_WAIT );
}
