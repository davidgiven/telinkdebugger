#!/usr/bin/python3
import argparse
import serial
from tqdm import tqdm
import sys
import os

serial_port = None

FLASH_SECTOR_SIZE = 4096


def readchar():
    while True:
        c = serial_port.read()
        if c == b"#":
            while c != b"\n":
                c = serial_port.read()

        if c not in [b"\n", b"\r", b" "]:
            return c


def readhex():
    h = bytearray()
    while True:
        c = readchar()
        if c == b"E":
            raise BaseException("Protocol error")
        if c == b"S":
            break
        h.append(c[0])
    return bytes.fromhex(str(h, "ascii"))


def connect():
    serial_port.write(b"i")
    c = readchar()
    if c != b"S":
        raise BaseException("Connection failed")


def run():
    serial_port.write(b"g")


def read_bytes_from_target(addr, len):
    s = b"R%04x%04x" % (addr, len)
    serial_port.write(s)
    return readhex()


def read_byte_from_target(addr):
    return int.from_bytes(read_bytes_from_target(addr, 1))


def read_word_from_target(addr):
    return int.from_bytes(read_bytes_from_target(addr, 2), byteorder="little")


def read_quad_from_target(addr):
    return int.from_bytes(read_bytes_from_target(addr, 4), byteorder="little")


def write_bytes_to_target(addr, bytes):
    serial_port.write(b"W%04x%04x" % (addr, len(bytes)))
    for b in bytes:
        serial_port.write(b"%02x" % b)
    readhex()


def write_byte_to_target(addr, byte):
    write_bytes_to_target(addr, byte.to_bytes(1, "little"))


def write_word_to_target(addr, word):
    write_bytes_to_target(addr, word.to_bytes(2, "little"))


def write_quad_to_target(addr, quad):
    write_bytes_to_target(addr, quad.to_bytes(4, "little"))


def read_flash_status():
    write_byte_to_target(0x0D, 0x00)  # flash CS enable
    write_byte_to_target(0x0C, 0x03)  # read flash command

    write_byte_to_target(0x0C, 0xFF)
    byte = read_byte_from_target(0x0C)

    write_byte_to_target(0x0D, 0x01)  # flash CS disable

    return byte


def read_flash_block(addr, len):
    write_byte_to_target(0x0D, 0x00)  # flash CS enable
    write_byte_to_target(0x0C, 0x03)  # read flash command
    write_byte_to_target(0x0C, (addr >> 16) & 0xFF)
    write_byte_to_target(0x0C, (addr >> 8) & 0xFF)
    write_byte_to_target(0x0C, addr & 0xFF)

    write_byte_to_target(0xB3, 0x80)  # SWS to FIFO mode

    data = bytearray()
    for i in range(0, len):
        write_byte_to_target(0x0C, 0xFF)
        data.append(read_byte_from_target(0x0C))

    write_byte_to_target(0xB3, 0x00)  # SWS to RAM mode
    write_byte_to_target(0x0D, 0x01)  # flash CS disable

    return data


def wait_for_flash_chip():
    while True:
        write_byte_to_target(0x0D, 0x00)  # flash CS enable
        write_byte_to_target(0x0C, 0x05)  # read_status_command
        write_byte_to_target(0x0C, 0xFF)  # dummy
        s = read_byte_from_target(0x0C)
        write_byte_to_target(0x0D, 0x01)  # flash CS disable

        if s & 0x20:
            raise BaseException("flash write failed")
        if not (s & 0x01):
            break


def erase_flash_sector(addr):
    write_byte_to_target(0x0D, 0x00)  # flash CS enable
    write_byte_to_target(0x0C, 0x06)  # write enable command
    write_byte_to_target(0x0D, 0x01)  # flash CS disable

    write_byte_to_target(0x0D, 0x00)  # flash CS enable
    write_byte_to_target(0x0C, 0x20)  # erase flash sector command
    write_byte_to_target(0x0C, (addr >> 16) & 0xFF)
    write_byte_to_target(0x0C, (addr >> 8) & 0xFF)
    write_byte_to_target(0x0C, addr & 0xFF)
    write_byte_to_target(0x0D, 0x01)  # flash CS disable

    wait_for_flash_chip()


def write_flash_block(addr, block):
    write_byte_to_target(0x0D, 0x00)  # flash CS enable
    write_byte_to_target(0x0C, 0x06)  # write enable command
    write_byte_to_target(0x0D, 0x01)  # flash CS disable

    write_byte_to_target(0x0D, 0x00)  # flash CS enable
    write_byte_to_target(0x0C, 0x02)  # write flash command
    write_byte_to_target(0x0C, (addr >> 16) & 0xFF)
    write_byte_to_target(0x0C, (addr >> 8) & 0xFF)
    write_byte_to_target(0x0C, addr & 0xFF)

    for b in block:
        write_byte_to_target(0x0C, b)

    write_byte_to_target(0x0D, 0x01)  # flash CS disable

    wait_for_flash_chip()


def hexdump(bytes, address):
    a = address & ~15
    end = address + len(bytes)
    while True:
        if (a & 15) == 0:
            sys.stdout.write("%08x : " % a)
            ascii = ""

        if (a >= address) and (a < end):
            b = bytes[a - address]
            sys.stdout.write("%02x " % b)
            if (b >= 32) and (b <= 126):
                ascii += chr(b)
            else:
                ascii += "."
        else:
            sys.stdout.write("   ")
            ascii += " "

        if (a & 15) == 15:
            sys.stdout.write(": |%s|\n" % ascii)
            if a >= end:
                return

        a += 1


def dump_ram_main(args):
    connect()
    b = read_bytes_from_target(args.address, args.length)
    hexdump(b, args.address)


def get_soc_id_main(args):
    connect()
    b = read_word_from_target(0x007E)
    print("SOC ID: 0x%04x\n" % b)


def flash_status_main(arg):
    connect()
    b = read_flash_status()
    print("Flash status byte: 0x%02x\n" % b)


def read_ram_main(args):
    connect()
    print(
        "Reading RAM from 0x%04x-0x%04x into '%s':"
        % (args.address, args.address + args.length, args.filename)
    )
    with open(args.filename, "wb") as file:
        for base in tqdm(
            iterable=range(args.address, args.address + args.length, 1024),
            unit_scale=1024,
            unit="B",
        ):
            b = read_bytes_from_target(base, 1024)
            file.write(b)


def read_flash_main(args):
    connect()
    print(
        "Reading flash from 0x%08x-0x%08x into '%s':"
        % (args.address, args.address + args.length, args.filename)
    )
    with open(args.filename, "wb") as file:
        for base in tqdm(
            iterable=range(args.address, args.address + args.length, 1024),
            unit_scale=1024,
            unit="B",
        ):
            b = read_flash_block(base, 1024)
            file.write(b)


def do_erase_flash(args):
    print(
        "Erasing flash from 0x%08x-0x%08x:"
        % (args.address, args.address + args.length)
    )
    for base in tqdm(
        iterable=range(
            args.address, args.address + args.length, FLASH_SECTOR_SIZE
        ),
        unit_scale=FLASH_SECTOR_SIZE,
        unit="B",
    ):
        erase_flash_sector(base)


def erase_flash_main(args):
    connect()
    do_erase_flash()


def write_flash_main(args):
    connect()
    with open(args.filename, "rb") as file:
        file.seek(0, os.SEEK_END)
        args.length = min(args.length, file.tell())
        file.seek(0, os.SEEK_SET)
        do_erase_flash(args)
        print(
            "Writing flash from 0x%08x-0x%08x from '%s':"
            % (args.address, args.address + args.length, args.filename)
        )
        for base in tqdm(
            iterable=range(args.address, args.address + args.length, 256),
            unit_scale=256,
            unit="B",
        ):
            b = file.read(256)
            write_flash_block(base, b)


def writeb_main(args):
    print("Writing 0x%02x to 0x%04x" % (args.value, args.address))
    write_byte_to_target(args.address, args.value)

def run_main(args):
    run()


def main():
    args_parser = argparse.ArgumentParser(description="Telink debugger client")
    args_parser.add_argument("--serial-port", type=str, required=True)
    subparsers = args_parser.add_subparsers(dest="cmd", required=True)

    dump_ram_parser = subparsers.add_parser("dump_ram")
    dump_ram_parser.set_defaults(func=dump_ram_main)
    dump_ram_parser.add_argument("address", type=lambda x: int(x, 0))
    dump_ram_parser.add_argument(
        "length", nargs="?", default=0x100, type=lambda x: int(x, 0)
    )

    read_ram_parser = subparsers.add_parser("read_ram")
    read_ram_parser.set_defaults(func=read_ram_main)
    read_ram_parser.add_argument("filename", type=str)
    read_ram_parser.add_argument(
        "address", nargs="?", default=0, type=lambda x: int(x, 0)
    )
    read_ram_parser.add_argument(
        "length", nargs="?", default=0xC000, type=lambda x: int(x, 0)
    )

    flash_status_parser = subparsers.add_parser("flash_status")
    flash_status_parser.set_defaults(func=flash_status_main)

    read_flash_parser = subparsers.add_parser("read_flash")
    read_flash_parser.set_defaults(func=read_flash_main)
    read_flash_parser.add_argument("filename", type=str)
    read_flash_parser.add_argument(
        "address", nargs="?", default=0, type=lambda x: int(x, 0)
    )
    read_flash_parser.add_argument(
        "length", nargs="?", default=0x7D000, type=lambda x: int(x, 0)
    )

    write_flash_parser = subparsers.add_parser("write_flash")
    write_flash_parser.set_defaults(func=write_flash_main)
    write_flash_parser.add_argument("filename", type=str)
    write_flash_parser.add_argument(
        "address", nargs="?", default=0, type=lambda x: int(x, 0)
    )
    write_flash_parser.add_argument(
        "length", nargs="?", default=0x7D000, type=lambda x: int(x, 0)
    )

    erase_flash_parser = subparsers.add_parser("erase_flash")
    erase_flash_parser.set_defaults(func=erase_flash_main)
    erase_flash_parser.add_argument(
        "address", nargs="?", default=0, type=lambda x: int(x, 0)
    )
    erase_flash_parser.add_argument(
        "length", nargs="?", default=0x7D000, type=lambda x: int(x, 0)
    )

    get_soc_id_parser = subparsers.add_parser("get_soc_id")
    get_soc_id_parser.set_defaults(func=get_soc_id_main)

    run_parser = subparsers.add_parser("run")
    run_parser.set_defaults(func=run_main)

    writeb_parser = subparsers.add_parser("writeb")
    writeb_parser.set_defaults(func=writeb_main)
    writeb_parser.add_argument("address", type=lambda x: int(x, 0))
    writeb_parser.add_argument("value", type=lambda x: int(x, 0))

    args = args_parser.parse_args()

    global serial_port
    serial_port = serial.Serial(
        args.serial_port,
        115200,
        serial.EIGHTBITS,
        serial.PARITY_NONE,
        serial.STOPBITS_ONE,
    )

    args.func(args)


if __name__ == "__main__":
    main()
