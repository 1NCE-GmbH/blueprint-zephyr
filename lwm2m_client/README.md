# 1NCE Zephyr blueprint - LwM2M Demo

## Overview

1NCE Zephyr LwM2M Demo allows customers to communicate with 1NCE endpoints via LwM2M Protocol. 

## Configuration options


The configuration options for LwM2M sample are:

`CONFIG_LWM2M_CLIENT_UTILS_SERVER` is set to 1NCE endpoint with port.


`CONFIG_APP_ENDPOINT_PREFIX` the ICCID of 1NCE SIM Card.

`CONFIG_APP_LWM2M_PSK` should be empty since we connect with unsecure port of LwM2M.

## Asking for Help

You can ask for help on our email [embedded@1nce.com](mailto:embedded@1nce.com). Please send bug reports and feature requests to GitHub.
