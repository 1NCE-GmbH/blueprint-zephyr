/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef LWM2M_CLIENT_APP_H__
#define LWM2M_CLIENT_APP_H__

#include <zephyr/kernel.h>
#include <zephyr/net/lwm2m.h>

#ifdef __cplusplus
extern "C" {
#endif

int lwm2m_app_init_device(char *serial_num);

#if defined(CONFIG_LWM2M_APP_LIGHT_CONTROL)
int lwm2m_init_light_control(void);
#endif

#if defined(CONFIG_LWM2M_APP_BUZZER)
int lwm2m_init_buzzer(void);
#endif

#if defined(CONFIG_LWM2M_APP_PUSH_BUTTON)
int lwm2m_init_push_button(void);
#endif


#ifdef __cplusplus
}
#endif

#endif /* LWM2M_CLIENT_APP_H__ */
