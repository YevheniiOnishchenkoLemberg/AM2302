# AM2302 Linux driver
Driver written in C language in 2 options: 
* am2302_char_driver can work as a regular character driver without any compatibility checks.
* am2302_platform_char_driver works as a regular character driver but should be described in DTS. The DTS overlay is compiled during the build.

---
## How to compile
Require make, gcc and dtc. Define `CROSS_COMPILE` and `KDIR` variables for cross-compilation.
- Type `make` to build kernel objects (*.ko).
- Type `make clean` to clean the project.

*Note: you can choose what driver to compile in environment.env file by commenting out/uncommenting lines*

---
## How to use
Copy the DTS overlay (*.dtbo) to your device. Apply it:
- `mkdir -p /sys/kernel/config/device-tree/overlays/am2302`
- `cat <path-to-your-dtbo> > /sys/kernel/config/device-tree/overlays/am2302/dtbo`

Copy desired kernel object file (*.ko). Load the module:
- `insmod <path-to-your-ko>`

Get the data about temperature and humidity:
- `cat /dev/am2302`

Example output:

![Example_output](example_output.png)

---
## Send data to ssd1306 display
Connect ssd1206 display to your linux-based device.
Clone and build binary file driver for the display from:
- https://github.com/armlabs/ssd1306_linux/tree/master

*Note: don't forget about cross-compile stuff if you need it*

Copy the driver to your device. Copy display_data_from_driver.sh bash script to the device.
Give execute permission to the script:
`chmod +x <path-to-the-script>`

Run the script:
`<path-to-the-script>`