gcc -O -Wall -fPIC -g -I./../include/ read_id.c \
  -lxx_usb -lm -lusb -L./../lib/ -Wl,-rpath="$(pwd)/../lib" \
  -o read_id
./read_id
