# amr5_ledctl

Tool for switching the RGB LED lighting modes on the **Acemagic AMR5** mini PC
(Ryzen 5 5600U) via direct I/O port writes.  Runs on both **Linux** and
**Windows** without any background service.

## Supported modes

| Mode       | Description                                      | Status              |
|------------|--------------------------------------------------|---------------------|
| `off`      | Turn all LEDs dark                               | ✅ Verified          |
| `breathing`| LEDs slowly pulse on and off                     | ⚠️ Needs hw test    |
| `cycling`  | LEDs cycle through colors (rainbow)             | ⚠️ Needs hw test    |
| `static`   | LEDs solid on at factory default color          | ⚠️ Needs hw test    |

> **Note:** The `off` mode bytes were verified by reverse engineering
> [AMR5LedOff](https://github.com/sergmuz/AMR5LedOff).  The other modes follow
> the same protocol but their exact command bytes still need confirmation on
> hardware.  See [Contributing mode bytes](#contributing-mode-bytes) below.

## Build

### Linux

```
make
```

Requires GCC and a standard C library.  The resulting binary must be run as
**root** (or with `CAP_SYS_RAWIO`) because it calls `ioperm()` to access
hardware I/O ports.

### Windows

Open a Visual Studio developer command prompt (or use MinGW/MSYS2) and run:

```
cl amr5_ledctl.c /Fe:amr5_ledctl.exe
```

or with MinGW:

```
gcc -O2 -Wall amr5_ledctl.c -o amr5_ledctl.exe
```

The program uses the **inpoutx64** kernel driver for I/O port access.  Install
the driver before running:

1. Download `inpoutx64` from <https://www.highrez.co.uk/downloads/inpout32.htm>
   or from the [AMR5LedOff releases page](https://github.com/sergmuz/AMR5LedOff/releases).
2. Run the installer as Administrator.
3. Run `amr5_ledctl.exe` as Administrator.

## Usage

```
amr5_ledctl <mode>
amr5_ledctl --raw <z1> <z2> <z3> <z4> <z5>
```

### Named mode

```
# Linux (must be root)
sudo ./amr5_ledctl off
sudo ./amr5_ledctl breathing

# Windows (run as Administrator)
amr5_ledctl.exe off
amr5_ledctl.exe cycling
```

### Raw mode

Specify a hex command byte for each of the 5 LED zones directly.  Useful for
testing new mode values before adding them to the source.

```
# Same as 'off'
sudo ./amr5_ledctl --raw 01 00 00 07 03
```

## Protocol details

The AMR5's LED controller is accessed through the onboard **ITE Super I/O
chip** at I/O ports `0x4E` (index) and `0x4F` (data).

A **double-indirect** addressing scheme is used:

```
Outer I/O:       0x4E = Super I/O index port
                 0x4F = Super I/O data port
Inner registers: 0x2E = inner index (selected via outer)
                 0x2F = inner data  (selected via outer)
```

To write value `V` to inner register `R`:

```
outb(0x4E, 0x2E)   ; select inner index register
outb(0x4F,  R )    ; set inner register address
outb(0x4E, 0x2F)   ; select inner data register
outb(0x4F,  V )    ; write value V
```

Each of the 5 LED zones requires three inner-register writes per update:

| Inner reg | Meaning              | Value           |
|-----------|----------------------|-----------------|
| `0x11`    | Bank / reg selector  | `0x04` (fixed)  |
| `0x10`    | Zone device address  | hardware-specific (see table) |
| `0x12`    | Mode command byte    | mode-specific   |

### Zone addresses

| Zone | Device address | Notes                      |
|------|---------------|----------------------------|
| 1    | `0xBD`        |                            |
| 2    | `0xBF`        |                            |
| 3    | `0x5C`        |                            |
| 4    | `0xBE`        |                            |
| 5    | `0xBE`        | shares address with zone 4 |

The complete mode sequence is sent **3 times** with 80 ms between passes and
2 ms between individual zone writes, mirroring the timing used in AMR5LedOff.

## Contributing mode bytes

If you have an AMR5 and the official Acemagic LED software installed on
Windows, you can help fill in the unverified mode bytes:

1. Install a port monitor that logs `inpoutx64` IOCTL calls (e.g., using
   `Process Monitor` with I/O filter, or a custom inpoutx64 wrapper).
2. Use the official software to switch to each LED mode.
3. Record the sequence of `(port, value)` pairs written.
4. Update the `MODE_BREATHING`, `MODE_CYCLING`, and `MODE_STATIC` arrays in
   `amr5_ledctl.c` and open a pull request.

## References

- [AMR5LedOff](https://github.com/sergmuz/AMR5LedOff) – source of the
  verified `off` mode command bytes.
- [inpoutx64 driver](https://www.highrez.co.uk/downloads/inpout32.htm) –
  Windows userspace I/O port access driver.

## License

MIT – see [LICENSE](LICENSE).
