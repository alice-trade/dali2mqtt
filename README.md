# daliMQTT for ESP32

DALI MQTT observer for ESP32.

## Overview

ESP-DALI-LIB is a DALI (Digital Addressable Lighting Interface) protocol library for the ESP-IDF platform, leveraging the ESP32's RMT (Remote Control) peripheral to manage DALI communication. DALI is a robust lighting control protocol widely used in building automation and lighting systems. This library enables ESP32-based devices to act as a DALI controller, sending and receiving commands from DALI-compatible devices such as dimmers, ballasts, and sensors.

## Features

- **DALI Standard Commands**: Support for standard DALI commands for controlling brightness, scenes, and power states.
- **Query Commands**: Ability to query status, scene levels, and other parameters from DALI devices.
- **Special Commands**: Implementation of special commands, including addressing and device configuration.
- **Data Transfer Register (DTR) Support**: Use of DTR registers for setting configuration parameters.
- **Addressing**: Support for short, group, and broadcast addresses, as well as special addressing modes.
- **Flexible Communication**: Customizable timeouts and adjustable inter-frame delays for communication.
