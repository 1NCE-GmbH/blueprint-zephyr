# 1NCE Zephyr blueprint

## Overview

The Zephyr Project is a scalable real-time operating system (RTOS) supporting multiple hardware architectures, optimized for resource constrained devices, and built with security in mind.

The Zephyr OS is based on a small-footprint kernel designed for use on resource-constrained systems: from simple embedded environmental sensors and LED wearables to sophisticated smart watches and IoT wireless gateways.

1NCE Zephyr blueprint is a concise application that provides an  overview of various features of 1NCE OS including Device Authenticator, IoT Integrator and Energy Saver. In combination with 1NCE SDK

## Supported Boards

The Blueprint works with following boards: 
* [nRF9160 Development Kit](https://www.nordicsemi.com/Products/Development-hardware/nrf9160-dk)
* [Thingy:91](https://www.nordicsemi.com/Products/Development-hardware/Nordic-Thingy-91)


## Getting Started
Follow this guide to: 
* Set up the 1NCE IoT C SDK with different features.
* Get the source code.
* Build, flash, and run Secure CoAP Application.

### Prerequisites
* [nRF Connect SDK (v2.2.0)](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/2.2.0/nrf/gs_assistant.html)
* [VS Code)](https://code.visualstudio.com/)
* [West](https://docs.zephyrproject.org/3.1.0/develop/west/install.html)

### Integrate 1NCE IoT C SDK

[1NCE IoT C SDK](https://github.com/1NCE-GmbH/1nce-iot-c-sdk) is a collection of C source files that can be used to connect and benefit from different services from 1NCE OS. 

In order to integrate 1NCE IoT C SDK, you will need to go to `nrf connect` in your path (you can see in your 
Quick Setup `VS Code`

NOTE: It is recommended to extract the Zephyr SDK at `%HOMEPATH%`

* Open the `west.yml`
`%HOMEPATH%\ncs\v2.2.0\nrf\west.yml`
* Add in `name-allowlist` name of our sdk module `nce-sdk` (NOTE: the list with alphabetical order).
*  Go to `%HOMEPATH%\ncs\v2.2.0\zephyr\submanifests` rename `example.yaml.sample` to `example.yaml` and paste the following code on it.
```
manifest:
	projects:
		- name: nce-sdk
		  url: https://github.com/1NCE-GmbH/1nce-iot-c-sdk
		  revision: main
```
* Open a `cmd.exe` window by pressing the Windows key typing “cmd.exe”
```
cd %HOMEPATH%\ncs\v2.2.0
west update
```
## Running the 1NCE Zephyr blueprint
* Clone the Repository
```
git clone https://github.com/1NCE-GmbH/blueprint-zephyr.git
```
* Open ` VS Code`
* Go `nRF Connect` extension or press `Ctrl+Alt+N`
* Press Add an existing application
*  Add the 1NCE Zephyr blueprint, which cloned in the first step. 
* Add build configuration 
* Choose the Board `nrf9160dk_nrf9160_ns` or `thingy91_nrf9160_ns` 
* Press Build Configuration
*  Connect your device with the Laptop
* Press `DEBUG` or `Flash` to flash the device.

To show the Log press `NRF TERMINAL`
* Press Start Terminal with new configuration.
* Press Serial Port 
* Choose the Port used by nRF DK

## Asking for Help

The most effective communication with our team is through GitHub. Simply create a [new issue](https://github.com/1NCE-GmbH/blueprint-zephyr/issues/new/choose) and select from a range of templates covering bug reports, feature requests, documentation issue, or Gerneral Question.
