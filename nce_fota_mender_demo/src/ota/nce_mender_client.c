/**
 * @file nce_mender_client.c
 * @brief Mender FOTA update using 1NCE CoAP Proxy.
 *
 * @author  Hatim Jamali & Mohamed Abdelmaksoud
 * @date 10 Jan 2024
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
#include <modem/modem_info.h>
#include <nce_iot_c_sdk.h>
#include <udp_interface_zephyr.h>
#include <modem/nrf_modem_lib.h>
#include <zephyr/net/coap.h>
#include "update.h"
#include "nce_mender_client.h"
#include "led_control.h"

#if defined( CONFIG_NCE_ENABLE_DTLS )
    #include <modem/modem_key_mgmt.h>
    #include <nrf_modem_at.h>
    #include <zephyr/net/tls_credentials.h>
#endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

#define MAX_COAP_MSG_LEN             1024
#define WORK_DELAY_SECONDS             5

/* NVS Definitions */
static struct nvs_fs fs;
int rc = 0;
char nvs_deployment_id[50];
char nvs_artifact_name[50];
#if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
#define NVS_PARTITION		custom_nvs_storage
#else
#define NVS_PARTITION		storage_partition
#endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */
#define NVS_PARTITION_DEVICE	FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET	FIXED_PARTITION_OFFSET(NVS_PARTITION)
#define DEPLOYMENT_ID 1
#define ARTIFACT_NAME_ID 2

/* Deployment definitions */
static char filename[900] = {0};
static char id[50] = {0};
static char artifact_name[50] = {0};
static char uri[100] = {0};

/* LED Thread */
K_THREAD_DEFINE(led_thread, CONFIG_LED_THREAD_STACK_SIZE, led_animation_thread_fn,
		NULL, NULL, NULL, 4, 0, 0);

/* Deployment URL processing */
#define REPLACE_UNICODE_ESCAPE(str) do { \
    char *unicodePtr = strstr(str, "\\u0026"); \
    while (unicodePtr != NULL) { \
        *unicodePtr = '&'; \
        memmove(unicodePtr + 1, unicodePtr + 6, strlen(unicodePtr + 6) + 1); \
        unicodePtr = strstr(str, "\\u0026"); \
    } \
} while(0)
#define EXTRACT_INFO(payload, id, artifact_name, uri, filename) \
    do { \
        const char *start_id = strstr(payload, "id") + 5; \
        const char *end_id = strchr(start_id, '"') ; \
        *id = (char *)malloc(end_id - start_id + 1); \
        strncpy(*id, start_id, end_id - start_id); \
        (*id)[end_id - start_id] = '\0'; \
        const char *start_artifact_name = strstr(payload, "artifact_name") + 16; \
        const char *end_artifact_name = strchr(start_artifact_name, '"') ; \
        *artifact_name = (char *)malloc(end_artifact_name - start_artifact_name + 1); \
        strncpy(*artifact_name, start_artifact_name, end_artifact_name - start_artifact_name); \
        (*artifact_name)[end_artifact_name - start_artifact_name] = '\0'; \
        const char *start_uri = strstr(payload, "://") + 3; \
        const char *end_uri = strchr(start_uri, '/'); \
        *uri = (char *)malloc(end_uri - start_uri + 1); \
        strncpy(*uri, start_uri, end_uri - start_uri); \
        (*uri)[end_uri - start_uri] = '\0'; \
        const char *start_filename = end_uri + 1; \
        const char *end_filename = strchr(start_filename, '"'); \
        *filename = (char *)malloc(strlen(start_filename) + 1); \
        strncpy(*filename, start_filename, end_filename - start_filename); \
        (*filename)[end_filename - start_filename] = '\0'; \
    } while(0)

static int mender_socket=0;
int err;
struct addrinfo * res;
struct addrinfo hints =
{
    .ai_family   = AF_INET,
    .ai_socktype = SOCK_DGRAM,
};

static struct k_work_delayable nce_mender_work;

struct coap_packet response, request;

#if defined( CONFIG_NCE_ENABLE_DTLS )
/* Security tag for DTLS */
const sec_tag_t tls_sec_tag[] =
{
    CONFIG_DTLS_SECURITY_TAG,
};
DtlsKey_t nceKey = { 0 };
#endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

/* Static function declaration */
#if defined( CONFIG_NCE_ENABLE_DTLS )
static int store_credentials( void );
static int dtls_setup( int fd );
#endif
static int connect_to_coap_server(int sock,char *hostname, int port);
static void print_coap_payload(struct coap_packet *packet);
static uint8_t handle_confirmable_response(int sock,struct coap_packet* response);
static void nce_mender_work_fn( struct k_work * work );
/* Mender communication via 1NCE CoAP Proxy */
static int nce_mender_auth( int fd, struct coap_packet request, struct coap_packet *response);
static int nce_mender_update_inventory( int fd, struct coap_packet request, struct coap_packet *response);
static int nce_mender_check_for_updates( int fd, struct coap_packet request, struct coap_packet *response);
static int nce_mender_report_status( int fd, struct coap_packet request, struct coap_packet *response, const char *status,bool active_deployment);

/**
 * @brief Device status.
 *
 */
enum device_status {
	UNAUTHORIZED = 0,
	AUTHORIZED = 1,
	INV_UPDATED = 2,
	UPDATE_AVAILABLE = 3,
    UPDATE_DOWNLOADING=4,
	UPDATE_DOWNLOADED = 5,
    UPDATE_FAILED=6,
};

uint8_t device_status = AUTHORIZED;

static update_start_cb update_start;

#if defined( CONFIG_NCE_ENABLE_DTLS )

/* Store DTLS Credentials in the modem */
static int store_credentials( void )
{
    int err;
    char psk_hex[ 40 ];
    int cred_len;

    /* Convert PSK to HEX */
    cred_len = bin2hex( nceKey.Psk, strlen( nceKey.Psk ), psk_hex, sizeof( psk_hex ) );

    if( cred_len == 0 )
    {
        printk( "PSK is too large to convert (%d)", -EOVERFLOW );
        return -EOVERFLOW;
    }

    /* Store DTLS Credentials */
    err = modem_key_mgmt_write( CONFIG_DTLS_SECURITY_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK, psk_hex, sizeof( psk_hex ) );
    printk( "psk status: %d\n", err );

    err = modem_key_mgmt_write( CONFIG_DTLS_SECURITY_TAG, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY, nceKey.PskIdentity, sizeof( nceKey.PskIdentity ) );
    printk( "psk_id status: %d\n", err );

    return err;
}

/* Configure DTLS socket */
static int dtls_setup( int fd )
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
        printk( "[ERR]Failed to setup peer verification, err %d\n", errno );
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
        printk( "[ERR]Failed to setup DTLS role, err %d\n", errno );
        return err;
    }

    /* Associate the socket with the security tag */
    err = zsock_setsockopt( fd, SOL_TLS, TLS_SEC_TAG_LIST, tls_sec_tag,
                            sizeof( tls_sec_tag ) );

    if( err )
    {
        printk( "[ERR]Failed to setup TLS sec tag, err %d\n", errno );
        return err;
    }

    return 0;
}
#endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

/* Connect to 1NCE CoAP Proxy using DTLS */
static int connect_to_coap_server(int fd,char *hostname, int port){
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

        /* Get DTLS Credentials using 1NCE SDK from the Device Authenticator */
        err = os_auth( &osNetwork, &nceKey );

        if( err )
        {
            printk("[ERR]SDK onboarding failed, err %d\n", errno );
            return -1;
        }

        printk("[INF]Disconnecting from the network to store credentials\n" );
        
        err = lte_lc_offline();

        if( err )
        {
            printk("[ERR]Failed to disconnect from the LTE network, err %d\n", err );
            return -1;
        }

        err = store_credentials();

        if( err )
        {
            printk("[ERR]Failed to store credentials, err %d\n", errno );
            return -1;
        }

        printk("[INF]Reconnecting after storing credentials.. " );
        err = lte_lc_connect();
        if( err )
        {
            printk("[ERR]Failed to connect to the LTE network, err %d\n", err );
            return -1;
        }
        long_led_pattern(LED_SUCCESS);

    }
    #endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */


    err = zsock_getaddrinfo( hostname, NULL, &hints, &res );

    if( err )
    {
        printk("[ERR]getaddrinfo() failed, err %d\n", errno );
        return -1;
    }

    ( ( struct sockaddr_in * ) res->ai_addr )->sin_port = htons( port );

    if(port == 5684){
        fd = socket( AF_INET, SOCK_DGRAM, IPPROTO_DTLS_1_2 );
    }else{
        fd = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    }

    if( fd == -1 )
    {
        printk("[ERR]Failed to open socket!\n" );
            return -1;
    }

    if(port ==5684){
    /* Setup DTLS socket options */
    err = dtls_setup( fd );

    if( err )
    {
        printk("[ERR]Failed to Configure DTLS!\n" );
        return -1;
    }
    } /* if( prot 5684 ) */

    printk("[INF]Connecting to %s\n", hostname );

    err = zsock_connect( fd, res->ai_addr, sizeof( struct sockaddr_in ) );

    if( err )
    {
        printk("[ERR]Failed to Connect to CoAP Server!\n" );
        return -1;
    }
    return 0;
}

/* Send a CoAP request using the connected socket */ 
static int coap_request(int fd, struct coap_packet request, struct coap_packet* response, 
                        uint8_t method, bool isJson, uint16_t option_path, 
                        const char *Uripath, const char *payload, uint16_t payload_len, 
                        const char *proxyUri) {
    /* Implementation for CoAP request */
    uint8_t * data;
    data = ( uint8_t * ) k_malloc( MAX_COAP_MSG_LEN );

    if( !data )
    {
        return -ENOMEM;
    }

    int r;

    r = coap_packet_init( &request, data, MAX_COAP_MSG_LEN,
                          COAP_VERSION_1, COAP_TYPE_CON,
                          COAP_TOKEN_MAX_LEN, coap_next_token(),
                          method, coap_next_id() );

    if( r < 0 )
    {
        printk( "[ERR]Failed to init CoAP message" );
        goto end;
    }

    r = coap_packet_append_option( &request, option_path,
                                   Uripath, strlen( Uripath ) );

    if( r < 0 )
    {
        printk( "[ERR]Unable add option to request" );
        goto end;
    }

    if( isJson == true )
    {
        const uint8_t json = COAP_CONTENT_FORMAT_APP_JSON;
        r = coap_packet_append_option(&request, COAP_OPTION_CONTENT_FORMAT,
                                    &json, sizeof(json));
        if (r < 0) {
            printk("[ERR]Unable to add JSON Format option to request\n");
            goto end;
        }
    }
    if(proxyUri != NULL){
        r = coap_packet_append_option(&request, COAP_OPTION_PROXY_URI,
                                  proxyUri, strlen(proxyUri));
        if (r < 0) {
            printk("[ERR]Unable to add Proxy URI option to request\n");
            goto end;
        }

        if( method != COAP_METHOD_GET )
        {
            r = coap_packet_append_payload_marker( &request );
            if( r < 0 )
            {
                printk( "[ERR]Unable to append payload marker" );
                goto end;
            }
        

            r = coap_packet_append_payload( &request, ( uint8_t * ) payload,
                                    payload_len );
            if( r < 0 )
            {
                printk( "[ERR]Unable to append payload" );
                goto end;
            }
        }
    }


    r = zsock_send( fd, request.data, request.offset, 0 );

    if( r < 0 )
    {
        printk( "[ERR]Unable to send CoAP packet" );
        goto end;
    }
    
end:
    k_free( data );
    return r;
}

/* Print the CoAP payload received from the server */ 
static void print_coap_payload(struct coap_packet *packet) {
    uint16_t payload_len;
    const uint8_t *payload = coap_packet_get_payload(packet, &payload_len);
    if (payload_len > 0 ) {
            
            char* uri1;
            char* id1;
            char* filename1;
            char* artifact_name1;
            EXTRACT_INFO(payload, &id1,&artifact_name1, &uri1, &filename1);
            REPLACE_UNICODE_ESCAPE(filename1);
            /* Output the results */ 
            memcpy(filename,filename1, strlen(filename1));
            memcpy(id,id1, strlen(id1));
            memcpy(artifact_name,artifact_name1, strlen(artifact_name1));
            memcpy(uri,uri1, strlen(uri1));
            printk("ID: %s\n", id);
            printk("URI: %s\n", uri);
            printk("Artifact name: %s\n", artifact_name);
            printk("Filename: %s\n", filename);

            if (strlen(uri) > 0 && strlen(id) > 0 )
            {
                printk("[INF]Update is available...\n");
                device_status = UPDATE_AVAILABLE;
            }
            
            free(filename1);
            free(uri1);
            printk("\n");

        
    }
}

/* Handle the confirmable CoAP response received from the connected socket */ 
static uint8_t handle_confirmable_response(int sock,struct coap_packet* response) {
    uint8_t buffer[MAX_COAP_MSG_LEN];
    uint8_t response_code = 0;
    int bytes_received = zsock_recv(sock, buffer, sizeof(buffer), 0);

    if (bytes_received > 0) {

        int err = coap_packet_parse(response, buffer, bytes_received, NULL, 0);

        if (err < 0) {
            printk("[ERR]Failed to parse CoAP response\n");
            return err;
        } else {
            response_code = coap_header_get_code(response);
            if (response_code == COAP_RESPONSE_CODE_UNAUTHORIZED ) {
                device_status =UNAUTHORIZED;
            }
            else if (response_code == COAP_RESPONSE_CODE_CONTENT){
                switch (device_status) {
                    case UNAUTHORIZED:
                    printk("[INF]Device is Authorized...\n");
                    device_status =AUTHORIZED;
                    break;
                    case INV_UPDATED:
                    print_coap_payload(response);
                    break; 
                }
            }
        }
    }

    return response_code;

}

/* Authenticate the device in Mender via 1NCE CoAP proxy using the connected DTLS socket */ 
static int nce_mender_auth( int fd, struct coap_packet request, struct coap_packet *response)
{
    int response_code =0;
    static char auth_url[150] = {0}; 
    sprintf(auth_url, "https://%s/api/devices/v1/authentication/auth_requests",CONFIG_MENDER_URL);
    err= coap_request(mender_socket,request,response, COAP_METHOD_POST,true, COAP_OPTION_URI_PATH, CONFIG_COAP_URI_QUERY, CONFIG_PAYLOAD, strlen( CONFIG_PAYLOAD ), auth_url);
    if( err < 0 )
    {
        printk("[ERR]\nCoAP err %d \n", err );
        return err;
    }
    response_code = handle_confirmable_response(mender_socket,response);
    return response_code;
}

/* Update the device inventory in Mender via 1NCE CoAP proxy using the connected DTLS socket */ 
static int nce_mender_update_inventory( int fd, struct coap_packet request, struct coap_packet *response)
{
    int response_code =0;
    static char inventory_url[150] = {0};
    static char inventory_payload[150] = {0};
    char imei[22];

	err = modem_info_string_get(MODEM_INFO_IMEI, imei, sizeof(imei));
	if (err < 0) {
		printk("Unable to get IMEI");
	}

    /* Define inventory payload */
  	if (strlen(imei) > 0 ) {
    sprintf(inventory_payload, "[{\"name\":\"IMEI\",\"value\":\"%s\"},{\"name\":\"artifact_name\",\"value\":\"%s\"},{\"name\":\"device_type\",\"value\":\"%s\"}]",
                                imei, CONFIG_ARTIFACT_NAME ,CONFIG_MENDER_DEVICE_TYPE);
	} else {
    sprintf(inventory_payload, "[{\"name\":\"artifact_name\",\"value\":\"%s\"},{\"name\":\"device_type\",\"value\":\"%s\"}]", CONFIG_ARTIFACT_NAME ,CONFIG_MENDER_DEVICE_TYPE);
	}

    sprintf(inventory_url, "https://%s/api/devices/v1/inventory/device/attributes",CONFIG_MENDER_URL);
    err= coap_request(mender_socket,request,response, COAP_METHOD_PATCH, true, COAP_OPTION_URI_PATH, CONFIG_COAP_URI_QUERY, inventory_payload, strlen( inventory_payload ),inventory_url);
    if( err < 0 )
    {
        printk("[ERR]\nCoAP err %d \n", err );
        return err;
    }
    response_code = handle_confirmable_response(mender_socket,response);
    return response_code;
}

/* Check for updates in Mender via 1NCE CoAP proxy using the connected DTLS socket */ 
static int nce_mender_check_for_updates( int fd, struct coap_packet request, struct coap_packet *response)
{
    int response_code =0;
    static char update_check_payload[200] = {0};
    static char deployment_url[150] = {0};

    printk("[INF] Waiting For Updates... \n" );
    sprintf(update_check_payload, "{\"device_provides\":{\"device_type\":\"%s\",\"artifact_name\":\"%s\"}}",CONFIG_MENDER_DEVICE_TYPE,CONFIG_ARTIFACT_NAME);
    sprintf(deployment_url, "https://%s/api/devices/v2/deployments/device/deployments/next",CONFIG_MENDER_URL);
    err= coap_request(mender_socket,request,response, COAP_METHOD_POST,true, COAP_OPTION_URI_PATH, CONFIG_COAP_URI_QUERY, update_check_payload, strlen( update_check_payload ), deployment_url);
    if( err < 0 )
    {
        printk("[ERR]\nCoAP err %d \n", err );
        return err;
    }
    response_code = handle_confirmable_response(mender_socket,response);
    return response_code;
}

/* Report deployment status in Mender via 1NCE CoAP proxy using the connected DTLS socket */ 
static int nce_mender_report_status( int fd, struct coap_packet request, struct coap_packet *response, const char *status,bool active_deployment)
{
    int response_code =0;
    static char status_url[150] = {0}; 

    if(active_deployment)
    {
        sprintf(status_url, "https://%s/api/devices/v1/deployments/device/deployments/%s/status",CONFIG_MENDER_URL, nvs_deployment_id);
    } else  {
        sprintf(status_url, "https://%s/api/devices/v1/deployments/device/deployments/%s/status",CONFIG_MENDER_URL, id);
    }

    err= coap_request(mender_socket,request,response, COAP_METHOD_PUT,true, COAP_OPTION_URI_PATH, CONFIG_COAP_URI_QUERY, status, strlen( status ), status_url);
    if( err < 0 )
    {
        printk("[ERR]\nCoAP err %d \n", err );
        return err;
    }
    printk("[INF] Downloading status reported ... \n" );
    response_code = handle_confirmable_response(mender_socket,response);
    return response_code;
}

/* Communicate with Mender using 1NCE CoAP proxy and update/handle device status */ 
static void nce_mender_work_fn( struct k_work * work )
{
    int response_code =0;

    if (device_status == UNAUTHORIZED) {
    /* Mender Authentication request */
    response_code = nce_mender_auth(mender_socket, request, &response);
    } 

    if( response_code < 0 )
    {
        printk("[ERR]\nCoAP err %d \n", err );
    }
    else
    {
        switch (device_status) {
        case UNAUTHORIZED:
            if (response_code != COAP_RESPONSE_CODE_CONTENT) {
                    printk("[INF]Device is not Authorized...\n");
                    printk("[INF] Please Accept the device in Mender dashboard...\n");
                    k_work_schedule( &nce_mender_work,
                    K_SECONDS( CONFIG_MENDER_AUTH_CHECK_FREQUENCY_SECONDS ) );
                } 
            break;

        case AUTHORIZED:
            /* Update Inventory */
            response_code = nce_mender_update_inventory(mender_socket, request, &response);
            if (response_code != COAP_RESPONSE_CODE_UNAUTHORIZED){
                device_status= INV_UPDATED;
            }
            k_work_schedule( &nce_mender_work,
            K_SECONDS( WORK_DELAY_SECONDS ) );
            break;            

        case INV_UPDATED:
            /* Check for updates */
            response_code = nce_mender_check_for_updates(mender_socket, request, &response);
            k_work_schedule( &nce_mender_work,
            K_SECONDS( CONFIG_MENDER_FW_UPDATE_CHECK_FREQUENCY_SECONDS ) );
            break;
        
        case UPDATE_AVAILABLE:
            /* Download the available update */
            fota_start();
            printk("[INF] Download Started... \n" );
            /* Report Downloading status to Mender */
            response_code = nce_mender_report_status(mender_socket, request, &response,STATUS_DOWNLOADING,false);
            k_work_schedule( &nce_mender_work,
            K_SECONDS( WORK_DELAY_SECONDS ) );
            break;

        case UPDATE_DOWNLOADING:
            k_work_schedule( &nce_mender_work,
            K_SECONDS( WORK_DELAY_SECONDS ) );
            break;

        case UPDATE_FAILED:
            printk("[INF] Update Failed ... \n" );
            long_led_pattern(LED_FAILURE);
            /* Report Failure status to Mender */
            response_code = nce_mender_report_status(mender_socket, request, &response,STATUS_FAILURE,false);
            k_sleep(K_SECONDS(10));    
            device_status= INV_UPDATED;
            long_led_pattern(LED_IDLE);
            k_work_schedule( &nce_mender_work,
            K_SECONDS( WORK_DELAY_SECONDS ) );
            break;

        case UPDATE_DOWNLOADED:
            printk("[INF] Done ... \n" );

        default:
            printk("[INF] ... \n" );
            k_work_schedule( &nce_mender_work,
            K_SECONDS( WORK_DELAY_SECONDS ) );

        }
    }
}

/* Connect to Mender via 1NCE CoAP proxy using DTLS and check active deployment status from NVS (if exists)  */ 
void nce_mender_application(){
    printk("[INF]1NCE CoAP client sample started\n\r" );
    printk("[INF]OK\n" );
    int response_code=0;
    err = connect_to_coap_server(mender_socket,CONFIG_COAP_SERVER_HOSTNAME, CONFIG_COAP_SERVER_PORT);
    if (err) {
        printk("[ERR]Failed to connect to CoAP server, err %d\n", err);
        return;
    }

    /* Report Success status to Mender for the active deployment after installation */
    rc = nvs_read(&fs, DEPLOYMENT_ID, &nvs_deployment_id, sizeof(nvs_deployment_id));
    rc = nvs_read(&fs, ARTIFACT_NAME_ID, &nvs_artifact_name, sizeof(nvs_deployment_id));
	if (rc > 0 && strlen(nvs_deployment_id) > 1 && strlen(nvs_artifact_name) > 1) { 
		printk("Active deployment found at ID (%s)\n",
			 nvs_deployment_id);
        int cmp = strcmp(nvs_artifact_name, CONFIG_ARTIFACT_NAME);
        if (cmp == 0) { 
        	printk("Artifact (%s) is installed, reporting success...\n",nvs_artifact_name);
            response_code = nce_mender_report_status(mender_socket, request, &response,STATUS_SUCCESS,true);
        } else  {
        	printk("Artifact (%s) installation failed, device recovered to (%s), reporting failure...\n",nvs_artifact_name,CONFIG_ARTIFACT_NAME);
            response_code = nce_mender_report_status(mender_socket, request, &response,STATUS_FAILURE,true);
            long_led_pattern(LED_FAILURE);
            k_sleep(K_SECONDS(10));    
            long_led_pattern(LED_IDLE);
	    }
        (void)nvs_delete(&fs, DEPLOYMENT_ID);
        (void)nvs_delete(&fs, ARTIFACT_NAME_ID);
	} else   {
		printk("No Active Deployment, %d\n",strlen(nvs_deployment_id) );
	}

    k_work_schedule( &nce_mender_work, K_NO_WAIT );
    k_sleep(K_SECONDS(2));
}

/* Initialize and start the application */ 
int fota_init(struct fota_init_params *params)
{
	int err;
    
	struct flash_pages_info info;
	if (params == NULL || params->update_start == NULL) {
		return -EINVAL;
	}

    fs.flash_device = NVS_PARTITION_DEVICE;
	if (!device_is_ready(fs.flash_device)) {
		printk("Flash device %s is not ready\n", fs.flash_device->name);
		return -EIO;
	}
	fs.offset = NVS_PARTITION_OFFSET;
	rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
	if (rc) {
		printk("Unable to get page info\n");
		return -EIO;
	}
	fs.sector_size = info.size;
	fs.sector_count = 4U;

	rc = nvs_mount(&fs);
	if (rc) {
		printk("Flash Init failed\n");
		return -EIO;
	}

	err = modem_info_init();
	if (err < 0) {
		printk("Unable to init modem_info, error: (%d)", err);
	}

    k_work_init_delayable( &nce_mender_work,
                           nce_mender_work_fn );
	update_start = params->update_start;

	modem_configure();

	err = button_init();
	if (err != 0) {
		return err;
	}


    nce_mender_application();
    
	return 0;
}

/* Start downloading the firmware */ 
void fota_start(void)
{
	int err;
    long_led_pattern(LED_WAITING);
	/* Functions for getting the host and file */
    printk("fota_download_start() \n");
    device_status = UPDATE_DOWNLOADING;
	err = fota_download_start(uri, filename, SEC_TAG, 0, 0);
	if (err != 0) {
        device_status = INV_UPDATED;
		fota_stop(1);
		printk("fota_download_start() failed, err %d\n", err);
	}
}

/* End the firmware update */ 
void fota_stop(int retry)
{
	button_irq_enable();
    if (retry == 0)
    {
    device_status = UPDATE_FAILED;
    } else {
    device_status = INV_UPDATED;
    }
    
}

/*  Finalize the update and reboot */ 
void fota_done(void)
{
	long_led_pattern(LED_SUCCESS);
    device_status = UPDATE_DOWNLOADED;

    /* Report Installing status to Mender */
    int response_code = nce_mender_report_status(mender_socket, request, &response,STATUS_INSTALLING,false);
    
    /* Store Deployment ID & Artifact name to NVS */
    strcpy(nvs_deployment_id, id);
	(void)nvs_write(&fs, DEPLOYMENT_ID, &nvs_deployment_id,
			  sizeof(nvs_deployment_id));
	printk("Stored deployment ID (%s) at %d ...\n",
			nvs_deployment_id,DEPLOYMENT_ID);
    strcpy(nvs_artifact_name, artifact_name);
	(void)nvs_write(&fs, ARTIFACT_NAME_ID, &nvs_artifact_name,
			  sizeof(nvs_artifact_name));
	printk("Stored artifact name (%s) at %d ...\n",
			nvs_artifact_name,ARTIFACT_NAME_ID);

    k_sleep(K_SECONDS(10));
    /* Report Rebooting status to Mender */
    response_code = nce_mender_report_status(mender_socket, request, &response,STATUS_REBOOTING,false);
	lte_lc_deinit();
    /* Reboot */
	sys_reboot(SYS_REBOOT_WARM);

}
