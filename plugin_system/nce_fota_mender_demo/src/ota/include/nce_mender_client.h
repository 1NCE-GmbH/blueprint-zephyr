/**
 * @file nce_mender_client.h
 * @brief Mender FOTA update using 1NCE CoAP Proxy.
 *
 * @author  Hatim Jamali & Mohamed Abdelmaksoud
 * @date 10 Jan 2024
 */

#ifndef NCE_MENDER_CLIENT_H__
#define NCE_MENDER_CLIENT_H__

/**
 * @typedef update_start_cb
 *
 * @brief the callback invoked when button is pressed.
 */
typedef void (*update_start_cb)(void);

/**
 * @brief FOTA initialization struct.
 */
struct fota_init_params {
	update_start_cb update_start;
};

/* Download status definitions */
#define STATUS_DOWNLOADING "{\"status\":\"downloading\"}"
#define STATUS_INSTALLING "{\"status\":\"installing\"}"
#define STATUS_REBOOTING "{\"status\":\"rebooting\"}"
#define STATUS_SUCCESS "{\"status\":\"success\"}"
#define STATUS_FAILURE "{\"status\":\"failure\"}"

int fota_init(struct fota_init_params *params);

void fota_start(void);

void fota_stop(int retry);

void fota_done(void);

#endif /* NCE_MENDER_CLIENT_H__ */
