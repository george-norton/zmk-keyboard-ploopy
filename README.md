# Work in progress ZMK firmware for Ploopy RP2040 devices

This is an experimental ZMK module adding support for Ploopy RP2040 device. Currently there is support for the Ploopy Mouse, but the Adept and Classic 2 are similar.

## TODO

- ZMK studio support

## Untested (use at your own risk)

- Adept support
- Classic 2 support

I expect issues with button mapping and sensor orientation. There is a bootloader combo on buttons 4+5, so you can hoepfully swap back to QMK easily.

## Done

- PMW3360 driver
- Driver for the Ploopy optical scroll wheel
- Mouse support
- Behaviour to cycle through DPI values
