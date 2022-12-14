# 1NCE Zephyr blueprint - UDP Demo

## Overview

1NCE Zephyr UDP Demo allows customers to communicate with 1NCE endpoints via UDP Protocol, and it can send compressed payload using the Energy Saver feature. 

 ## Using 1NCE Energy saver
 The demo can send optimized payload using 1NCE Energy saver. To enable this feature, add the following flag to `prj.conf`

```
CONFIG_NCE_ENERGY_SAVER=y
```
 
## Configuration options


The configuration options for UDP sample are:

`CONFIG_UDP_SERVER_HOSTNAME` is set to 1NCE endpoint.

`CONFIG_UDP_SERVER_PORT` is set by default to the 1NCE UDP endpoint port 4445.

`CONFIG_UDP_DATA_UPLOAD_FREQUENCY_SECONDS` the interval between UDP packets.

`CONFIG_PAYLOAD` Message to send to 1NCE IoT Integrator.

`CONFIG_PAYLOAD_DATA_SIZE` Used when 1NCE Energy Saver is enabled to define the payload data size of the translation template.

`CONFIG_UDP_PSM_ENABLE` PSM mode configuration
   This configuration option, if set, allows the sample to request PSM from the modem or cellular network.

`CONFIG_UDP_EDRX_ENABLE` eDRX mode configuration
   This configuration option, if set, allows the sample to request eDRX from the modem or cellular network.

`CONFIG_UDP_RAI_ENABLE` RAI configuration
   This configuration option, if set, allows the sample to request RAI for transmitted messages.

## Asking for Help

You can ask for help on our email [embedded@1nce.com](mailto:embedded@1nce.com). Please send bug reports and feature requests to GitHub.
