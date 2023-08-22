# 1NCE Zephyr blueprint - UDP Demo

## Overview

1NCE Zephyr UDP Demo allows customers to communicate with 1NCE endpoints via UDP Protocol, and it can send compressed payload using the Energy Saver feature. 

With `Thingy:91`, the LED colors indicate the following status: `RED` -> Connecting, `BLUE` -> Network connection established, `GREEN` -> Message Sent to 1NCE OS 

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


## Device Controller

The Device Controller is an API that allows you to interact with devices integrated into the 1NCE API. You can use this API to send requests to devices, and the devices will respond accordingly. For more details you can visit our [DevHub](https://help.1nce.com/dev-hub/docs/1nce-os-device-controller)

### Sending a Request

To send a request to a specific device, you can use the following `curl` command:

```
curl -X 'POST' 'https://api.1nce.com/management-api/v1/integrate/devices/<ICCID>/actions/UDP' \
-H 'accept: application/json' \
-H 'Authorization: Bearer <your Access Token >' \
-H 'Content-Type: application/json' \
-d '{
  "payload": "enable_sensor",
  "payloadType": "STRING",
  "port": 3000,
  "requestMode": "SEND_NOW"
}'
```
Replace `<ICCID>` with the ICCID (International Mobile Subscriber Identity) of the target device and `<your Access Token>` with the authentication token from [Obtain Access Token](https://help.1nce.com/dev-hub/reference/postaccesstokenpost).

##### Requested Parameters

* `payload`: The data you want to send to the device. This should be provided as a string.
* `payloadType`: The type of the payload. In this example, it is set to "STRING".
* `port`: The port number on which the device will receive the request. In the code, this is defined as CONFIG_NCE_RECV_PORT.
* `requestMode`: The mode of the request. In this example, it is set to "SEND_NOW". There are other possible value like SEND_WHEN_ACTIVE.

#### Zephyr Configuration

To handle the incoming request from the 1NCE API, the configuration of certain parameters needed
* `CONFIG_NCE_RECV_PORT`: This is the port number where your device will listen for incoming requests. It should match the port parameter used in the request (by default is 3000).
* `CONFIG_NCE_RECEIVE_BUFFER_SIZE` : This is the size of the buffer that will be used to receive the incoming data from the 1NCE API (by default 256).

#### Zephyr Output

When the Zephyr application receives a request from the 1NCE API, it will produce output similar to the following:

```
Listening for incoming messages...

Received message: enable_sensor
```

## Asking for Help

The most effective communication with our team is through GitHub. Simply create a [new issue](https://github.com/1NCE-GmbH/blueprint-zephyr/issues/new/choose) and select from a range of templates covering bug reports, feature requests, documentation issue, or Gerneral Question.