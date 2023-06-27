Intel GMBus I2C Driver

* NOTE: This driver only supports the Google Pixel (2013) and is not certified for any other device. It requires the correct ACPI tables set up by coreboot

Implements Windows's SPB protocol so most existing I2C drivers should attach and work as child devices off this driver

Tested operations:
* Connect
* Read
* Write

Implemented but untested:
* Sequence

Do note that the Intel GPU driver may access the gmbus at any time. On any other device, try to avoid connecting i2c devices to ports where displays may be attached.