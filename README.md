# ScalerMonitor_SIS3820

Interactive monitoring for a **Struck SIS3820** scaler via a **Wiener VM-USB** (no NSCLDAQ).

Main tool: **`scaler_ctl`** — reset / start / stop / clear, one-shot and auto live dashboard (`cga` = clear → go → auto).

---

## Status

| | |
|--|--|
| **OS** | Ubuntu **24.04** (x86_64) |
| **Controller** | [Wiener VM-USB](https://www.wiener-d.com/product/vm-usb-vme-controller-with-usb-2-interface/) |
| **Scaler** | Struck SIS3820 |
| **Library** | Wiener **libxxusb** (64-bit build) + Ubuntu **libusb** 0.1 |
| **Default VME base** | `0x88000000` (edit in `scaler_ctl.c` if your switches differ) |

---

## Setup & usage

Full install (packages, udev, libxxusb, hardware, troubleshooting, all commands):

**→ [SCALER_SETUP.md](SCALER_SETUP.md)**

```bash
# after following SCALER_SETUP.md
./run_scaler_ctl.sh
# or: ./scaler_ctl
```

---

## Commands (cheat sheet)

| | |
|--|--|
| `r` `g` `s` `c` | reset / go / stop / clear |
| `m` / `a` `[N]` | monitor once / auto every *N* s |
| `p` `[N]` | show / set auto period |
| `cga` `[N]` | clear + go + auto |
| `h` `q` | help / quit |

---

## Links

- [SCALER_SETUP.md](SCALER_SETUP.md) — complete guide  
- [Wiener VM-USB](https://www.wiener-d.com/product/vm-usb-vme-controller-with-usb-2-interface/)
