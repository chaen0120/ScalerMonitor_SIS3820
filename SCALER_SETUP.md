# SIS3820 Scaler Control with Wiener VM-USB

How to set up Ubuntu and use `scaler_ctl` to talk to an SIS3820 scaler through a Wiener VM-USB.

---

## 1. Hardware

1. Power **off** the VME crate.
2. Install the **VM-USB** in **slot 1** (system controller).
3. Install the **SIS3820** (e.g. slot 2).
4. Set SIS3820 jumpers / switches:
   - **J1:** only **EN_A32** jumpered (top); **EN_GEO** open.
   - **SW4 / SW3:** set the VME base address (high two hex digits).
     - Factory example: SW4=`3`, SW3=`8` → base `0x38000000`
     - This lab’s board: SW4=`8`, SW3=`8` → base **`0x88000000`**
   - SW1 / SW2 are **not** used for the A32 base address.
5. Power **on** the crate.
6. Confirm SIS3820 front panel: **P** (power) and **R** (ready) on.
7. Connect VM-USB to the PC with a USB cable.

After changing switches or J1, always **power-cycle** the crate so the new address is taken.

---

## 2. Ubuntu packages

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  pkg-config \
  libusb-0.1-4 libusb-dev \
  libusb-1.0-0 libusb-1.0-0-dev \
  usbutils
```

Wiener **libxxusb** uses the older **libusb-0.1** API (`-lusb`).

---

## 3. See the VM-USB on USB

```bash
lsusb
```

You should see something like:

```text
ID 16dc:000b Wiener, Plein & Baus VM
```

If not: check cable, crate power, and USB port.

### Optional: non-root USB access (udev)

```bash
sudo tee /etc/udev/rules.d/99-wiener-vmusb.rules >/dev/null <<'EOF'
SUBSYSTEM=="usb", ATTR{idVendor}=="16dc", ATTR{idProduct}=="000b", MODE="0666", GROUP="plugdev"
EOF
sudo udevadm control --reload-rules
sudo udevadm trigger
sudo usermod -aG plugdev "$USER"
```

Log out and back in. Unplug/replug the USB cable.

Until udev is set up, you can run tools with `sudo` for testing.

---

## 4. Build Wiener libxxusb (64-bit)

The CD often ships a **32-bit** `libxx_usb.so`. On Ubuntu 24.04 x86_64 you must build a **64-bit** library.

From the XX-USB tree (adjust path if yours differs):

```bash
cd ~/Projects/XX-USB_CD_CURRENT/xxusb_3.3/src
gcc -shared -fPIC -o libxx_usb.so libxxusb.c -lusb
cp libxx_usb.so ../lib/
# optional system-wide:
# sudo cp libxx_usb.so /usr/local/lib/
# sudo cp ../include/libxxusb.h /usr/local/include/
# sudo ldconfig
```

Check architecture:

```bash
file ../lib/libxx_usb.so
# expect: ELF 64-bit LSB shared object, x86-64
```

If `gcc` later says `skipping incompatible .../libxx_usb.so`, you are still linking a 32-bit `.so`. Replace it with the 64-bit build and fix `-L` / `-rpath` to that directory.

---

## 5. Build `scaler_ctl`

```bash
cd ~/Projects/XX-USB_CD_CURRENT/xxusb_3.3/codes

gcc -O -Wall -fPIC -g -I../include scaler_ctl.c \
  -lxx_usb -lm -lusb -L../lib -Wl,-rpath="$(pwd)/../lib" \
  -o scaler_ctl
```

Or:

```bash
./run_scaler_ctl.sh
```

### Base address in the code

`scaler_ctl.c` uses:

```c
#define BASE  0x88000000UL   /* SW4=8, SW3=8 */
#define AM    0x09           /* A32 user data */
```

If your switches differ, change `BASE` to match (`SW4`/`SW3` → `0xSW4SW3000000`) and rebuild.

To discover the base with a scan, use a small program that loops `VME_read_32` over `0x00000000 … 0xF0000000` step `0x01000000` and look for ID `0x3820xxxx` (see session notes / `ipsearch.c` / earlier scan). Watch the SIS3820 **A** (Access) LED.

---

## 6. Run

```bash
./scaler_ctl
```

On success you should see the VM-USB serial and:

```text
SIS3820 found (firmware 0x....)
```

### USB / first-access quirk

The first VME access after open often times out (`ret=-110`). `scaler_ctl` already does a warm-up (`xxusb_reset_toggle` + retry). If opens fail intermittently, unplug/replug USB or power-cycle the crate.

---

## 7. Commands

| Command | Meaning |
|---------|---------|
| `r` | Full module **reset** (S off; must `g` again) |
| `g` | **Go** / start: configure, arm, enable (S on) |
| `s` | **Stop** counting (S off) |
| `c` | **Clear** counters only |
| `m` | **Monitor** once (dashboard) |
| `a` | **Auto** monitor at current period (Enter to stop) |
| `a 5` | Set period to 5 s and auto |
| `p` | Show auto period |
| `p 2` | Set auto period to 2 s |
| `cga` | **Clear + Go + Auto** |
| `cga 2` | Same with 2 s period |
| `h` | Help |
| `q` | Quit |

### Typical workflow

```text
scaler> cga
```

Or step by step:

```text
scaler> g          # start counting
scaler> a          # live dashboard (Enter to stop)
scaler> s          # stop
```

### LEDs (SIS3820)

| LED | Meaning |
|-----|---------|
| **P** | Power |
| **R** | Ready (FPGA up) |
| **A** | VME access (blinks on read/write) |
| **S** | Scaler enable (on while counting) |
| **CLR** | Clear / LNE (blinks when monitor latches) |

Monitor uses **non-clearing** latch mode: `m` / `a` do **not** zero totals. Use `c` or `r` to clear.

Auto mode redraws the screen in place (2 channels per row) and shows approximate rates (`/s`).

---

## 8. Troubleshooting

| Symptom | What to try |
|---------|-------------|
| No `16dc:000b` in `lsusb` | Cable, crate power, USB port |
| Permission denied on open | udev rule / `plugdev` / `sudo` |
| `skipping incompatible libxx_usb.so` | Rebuild 64-bit `.so`; fix `-L` |
| ID read `ret=-110`, `id=0x5` | Timeout / first access; warm-up; wrong base |
| ID `0xffffffff` | No module at that address |
| **A** never blinks | Wrong base, bus, or controller not mastering VME |
| **P**/**R** off | Module/crate power or seating |
| Wrong counts / always zero | Not started (`g`); no input signals; unplug **control** inputs (old builds used inhibit mode 4); rebuild current `scaler_ctl` and run `r` then `g` |

Confirm ID register: `BASE+0x04` should read `0x3820xxxx`.

---

## 9. Files in this directory

| File | Role |
|------|------|
| `scaler_ctl.c` | Interactive scaler control program |
| `run_scaler_ctl.sh` | Build and run helper |
| `read.c` / `read_id` | Earlier ID / readout experiments |
| `../include/libxxusb.h` | Wiener API header |
| `../lib/libxx_usb.so` | Shared library (use 64-bit build) |
| `../src/libxxusb.c` | Library source |

---

## 10. Reference

- Wiener VM-USB: product page / user manual (libxxusb chapter)
- Struck SIS3820 user manual (address map, J1, SW3/SW4, key registers)
- Key offsets used by `scaler_ctl`: reset `+0x400`, clear `+0x40C`, LNE `+0x410`, arm `+0x414`, enable `+0x418`, disable `+0x41C`, shadow counters `+0x800`
