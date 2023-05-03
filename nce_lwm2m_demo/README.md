# 1NCE Zephyr blueprint - LwM2M Demo

## Overview

1NCE Zephyr LwM2M Demo allows customers to communicate with 1NCE endpoints via LwM2M Protocol. 

For `Thingy:91`, the LED colors indicate the following statuses:

- `RED`  the device is currently connecting.
- `BLUE`  the device is currently bootstrapping.
- `GREEN`  the device is registered with 1NCE LwM2M server.

LwM2M Actions can be tested using the [Action API](https://help.1nce.com/dev-hub/reference/post_v1-devices-deviceid-actions). Available objects include:

- `Light Control (3311)` control the LEDs:
    - set "/3311/0/5850" to true to turn on the LED.
    - For `Thingy:91`, write a hexadecimal value (RRGGBB) to "/3311/0/5706" to change the LED color. Example: "0xFF0000" for bright red. 
- `Buzzer (3338)` control an audible alarm:
    - For `Thingy:91`, set "/3311/0/5850" to true to trigger the buzzer.


## Configuration options


The configuration options for LwM2M sample are:

`CONFIG_LWM2M_CLIENT_UTILS_SERVER` is set to 1NCE endpoint with port.


`CONFIG_NCE_ICCID` the ICCID of 1NCE SIM Card.


## Asking for Help

You can ask for help on our email [embedded@1nce.com](mailto:embedded@1nce.com). Please send bug reports and feature requests to GitHub.
