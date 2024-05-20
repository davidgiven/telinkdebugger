#!/usr/bin/python3
import argparse
import serial
import time

serial_port = None


def readchar():
    while True:
        c = serial_port.read()
        if c == b"#":
            while c != b"\n":
                c = serial_port.read()

        if c not in [b"\n", b"\r"]:
            return c


def readhex():
    h = []
    while True:
        c = readchar()
        if c == b"E":
            raise BaseException("Protocol error")
        if c == b"S":
            break
        h.append(str(c, "ascii"))
    return bytes.fromhex("".join(h))


def connect():
    serial_port.write(b"i")
    c = readchar()
    if c != b"S":
        raise BaseException("Connection failed")


def read_bytes_from_target(addr, len):
    serial_port.write(
        b"TC%02xD%02xD%02xD80R%02x" % (0x5A, addr >> 8, addr & 0xFF, len)
    )
    return readhex()


def read_word_from_target(addr):
    return int.from_bytes(read_bytes_from_target(addr, 2), byteorder="little")


def get_soc_id_main():
    connect()
    b = read_word_from_target(0x007E)
    print("SOC ID: 0x%04x\n" % b)


def main():
    args_parser = argparse.ArgumentParser(description="Telink debugger client")
    args_parser.add_argument("--serial-port", type=str, required=True)
    subparsers = args_parser.add_subparsers(dest="cmd", required=True)

    get_soc_id_parser = subparsers.add_parser("get_soc_id")
    get_soc_id_parser.set_defaults(func=get_soc_id_main)

    args = args_parser.parse_args()

    global serial_port
    serial_port = serial.Serial(
        args.serial_port,
        115200,
        serial.EIGHTBITS,
        serial.PARITY_NONE,
        serial.STOPBITS_ONE,
    )

    args.func()


if __name__ == "__main__":
    main()
