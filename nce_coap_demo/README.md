# 1NCE Zephyr blueprint - CoAP Demo

## Overview

1NCE Zephyr CoAP Demo allows customers to establish a secure communication with 1NCE endpoints via CoAPs after receiving DTLS credentials from Device Authenticator using the SDK. It can also send compressed payload using the Energy Saver feature. 

For `Thingy:91`, the LED colors convey different statuses:

- `RED` indicates that the device is currently connecting
- `BLUE` signifies that a network connection has been established
- `GREEN` means that a message has been successfully sent to 1NCE OS.

## Secure Communication with DTLS using 1NCE SDK

By default, the demo uses 1NCE SDK to send a CoAP GET request to 1NCE OS Device Authenticator. The response is then processed by the SDK and the credentials are used to connect to 1NCE endpoint via CoAP with DTLS. 

 ## Using 1NCE Energy saver
 The demo can send optimized payload using 1NCE Energy saver. To enable this feature, add the following flag to `prj.conf`

```
CONFIG_NCE_ENERGY_SAVER=y
```
 ## Unsecure CoAP Communication 

To test unsecure communication, disable the device authenticator by adding the following flag to `prj.conf`

```
CONFIG_NCE_DEVICE_AUTHENTICATOR=n
``` 

## Configuration options


The configuration options for CoAP sample are:

`CONFIG_DTLS_SECURITY_TAG` DTLS TAG that will be used to store the received credential,set by default to 1111.

`CONFIG_OVERWRITE_CREDENTIALS_IF_EXISTS` If the DTLS TAG already exists in the modem, it will be overwritten with the new credentials (enabled by default).

`CONFIG_COAP_SERVER_HOSTNAME` is set to 1NCE endpoint.

`CONFIG_COAP_SERVER_PORT` is set automatically based on security options (with/without DTLS).

`CONFIG_COAP_URI_QUERY` the URI Query option used to set the MQTT topic for 1NCE IoT integrator.

`CONFIG_COAP_DATA_UPLOAD_FREQUENCY_SECONDS` the interval between CoAP packets.

`CONFIG_PAYLOAD` Message to send to 1NCE IoT Integrator.

`CONFIG_PAYLOAD_DATA_SIZE` Used when 1NCE Energy Saver is enabled to define the payload data size of the translation template.



## Asking for Help

You can ask for help on our email [embedded@1nce.com](mailto:embedded@1nce.com). Please send bug reports and feature requests to GitHub.
