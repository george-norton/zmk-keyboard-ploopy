# Work in progress ZMK firmware for Ploopy RP2040 devices

This is an experimental ZMK module adding support for Ploopy RP2040 device. I have teste the mouse and it works great, the Adept, Classic 2 builds are untested - let me know if you try them. it is possible the sensor orientation or button mappings are wrong. Only recent RP2040 variants of these devices are supported. There is no AVR support in Zephyr.

## TODO

- Optimize power consumption

## Untested (use at your own risk)

- Adept support
- Classic 2 support
- Thumb support
- ZMK Studio support for adept, classic and thumb

I expect issues with button mapping and sensor orientation. There is a bootloader combo on buttons 4+5, so you can hoepfully swap back to QMK easily.

## Done

- PMW3360 driver
- Driver for the Ploopy optical scroll wheel
- Mouse support
- ZMK Studio support for the mouse
- Behaviour to cycle through DPI values

## Studio support

If you flash the mouse firmware from the github actions, or you take this PR https://github.com/zmkfirmware/zmk/pull/2950 into your local zmk checkout, you can use ZMK studio to remap your keys.

Go to https://zmk.studio/ in a chrome/chromium based brower, then select your device from the list of serial ports.

To unlock yours mouse so studio can access it, press both the small outer buttons together (the copy + paste buttons).

It is not currently possible to remap the scroll wheel, but all the buttons can be reassigned.