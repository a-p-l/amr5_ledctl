#!/usr/bin/env python3
import argparse
import os
import sys
import time

INDEX_PORT = 0x4E
DATA_PORT = 0x4F

REG_MODE = 0x4BE
REG_PARAM1 = 0x45B
REG_PARAM2 = 0x45C

DELAY = 0.01

MODES = {
    "off": 7,
    "button": 3, # only the power button is constantly illuminated
    "static": 3, # everthing in a single color
    "default": 1, # rainbow + power button in single color
    "rainbow": 5,
    "breath": 2,
    "cycle": 6,
}


class PortIO:
    def __init__(self, path="/dev/port"):
        self.path = path
        self.fd = None

    def __enter__(self):
        self.fd = os.open(self.path, os.O_RDWR)
        return self

    def __exit__(self, exc_type, exc, tb):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def write8(self, port: int, value: int) -> None:
        if not (0 <= value <= 0xFF):
            raise ValueError(f"value out of range: {value}")
        os.lseek(self.fd, port, os.SEEK_SET)
        os.write(self.fd, bytes([value]))


def write_reg(io: PortIO, reg: int, value: int) -> None:
    reg_high = (reg >> 8) & 0xFF
    reg_low = reg & 0xFF

    io.write8(INDEX_PORT, 0x2E)
    io.write8(DATA_PORT, 0x11)
    io.write8(INDEX_PORT, 0x2F)
    io.write8(DATA_PORT, reg_high)

    io.write8(INDEX_PORT, 0x2E)
    io.write8(DATA_PORT, 0x10)
    io.write8(INDEX_PORT, 0x2F)
    io.write8(DATA_PORT, reg_low)

    io.write8(INDEX_PORT, 0x2E)
    io.write8(DATA_PORT, 0x12)
    io.write8(INDEX_PORT, 0x2F)
    io.write8(DATA_PORT, value & 0xFF)


def apply_mode(io: PortIO, mode: str) -> None:
    write_reg(io, REG_PARAM2, 0x00)

    if mode == "static":
        write_reg(io, REG_MODE, 0x02)
        time.sleep(DELAY)
    elif mode == "button":
        write_reg(io, REG_MODE, 0x07)
        time.sleep(DELAY)
    elif mode == "rainbow":
        write_reg(io, REG_PARAM1, 0x32) # speed
        write_reg(io, REG_PARAM2, 0x07)

    mode_value = MODES[mode]
    write_reg(io, REG_MODE, mode_value)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Set ACEMAGIC AMR5 LED mode via direct I/O ports"
    )
    parser.add_argument(
        "mode",
        choices=sorted(MODES.keys()),
        help="LED mode to set",
    )
    parser.add_argument(
        "--devport",
        default="/dev/port",
        help="path to devport device (default: /dev/port)",
    )
    args = parser.parse_args()

    if os.geteuid() != 0:
        print("This tool must be run as root.", file=sys.stderr)
        return 1

    if not os.path.exists(args.devport):
        print(
            f"{args.devport} does not exist. Kernel/devfs may not expose /dev/port.",
            file=sys.stderr,
        )
        return 2

    try:
        with PortIO(args.devport) as io:
            apply_mode(io, args.mode)
    except PermissionError:
        print(f"Permission denied opening {args.devport}", file=sys.stderr)
        return 3
    except OSError as e:
        print(f"I/O error: {e}", file=sys.stderr)
        return 4
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 5

    print(f"LED mode set to: {args.mode}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
