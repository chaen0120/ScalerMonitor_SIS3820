#!/bin/bash
# Build and run interactive SIS3820 control
gcc -O -Wall -fPIC -g -I./../include/ scaler_ctl.c \
  -lxx_usb -lm -lusb -L./../lib/ -Wl,-rpath="$(pwd)/../lib" \
  -o scaler_ctl
exec ./scaler_ctl
