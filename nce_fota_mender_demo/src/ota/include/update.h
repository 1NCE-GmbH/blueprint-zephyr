/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef UPDATE_H__
#define UPDATE_H__

#define TLS_SEC_TAG 42

#ifndef CONFIG_USE_HTTPS
#define SEC_TAG (-1)
#else
#define SEC_TAG (TLS_SEC_TAG)
#endif

void modem_configure(void);

int button_init(void);

void button_irq_disable(void);

void button_irq_enable(void);


#endif /* UPDATE_H__ */
