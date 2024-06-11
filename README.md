# Telink USB debugger bridge

## What?

This is a poorly written and hacked together Raspberry Pi Pico-based debugger
bridge for the Telink SWS protocol. It will allow flashing of Telink-based
devices via USB, so far only tested on the incredibly cheap LT716 fitness bands
using the Telink TLSR8232 chip.

It was put together from information pulled from these sources:

- [Reverse engineering the M6 smart fitness
  bracelet](https://rbaron.net/blog/2021/07/06/Reverse-engineering-the-M6-smart-fitness-band.html)
  by rbaron
- [TlsrTools](https://github.com/pvvx/TlsrTools) by pvvx@github

It would theoretically be possible to extend this into a full machine-code
debugger, but the relevant part of the TLSR8232 register map isn't documented
and it would require reverse engineering and I just don't have the energy.
Please get in touch if you're interested.

## How?

You can either get a precompiled binary from [the Github releases
page](https://github.com/davidgiven/telinkdebugger/releases/tag/dev) or build it
yourself. It's a standard Raspberry Pi Pico project, so you should just be able
to run the cmakefile and it'll build. Flash a normal Pico with the resulting
file (no wireless necessary).

Then, connect the Pico to your Telink device as follows:

- Telink RX -> Pico pin 1 (GPIO0)
- Telink TX -> Pico pin 2 (GPIO1)
- Telink SWS -> Pico pin 4 (GPIO2)
- Telink RST -> Pico pin 5 (GPIO3)

No extra components are needed. Just wire it up directly.

When the Pico starts up, it'll put the Telink device into reset and expose two
CDC serial ports via USB. The first is the control port which is used to
communicate with the debugger. The second is a standard USB UART interface and
is connected to the TX and RX pins.

There is a Python script provided for communicating with the debugger:

```
$ ./client.py --serial-port=/dev/ttyACM1 get_soc_id
SOC ID: 0x5316
```

The serial port provided should be that of the control port.

Useful commands include:

- `writeb <address> <value>` --- writes a single byte to RAM
- `dump_ram <address> [<length>]` --- produces a hex dump of RAM at a given
address. (Note that RAM addresses are always 16-bit. You can't read flash with
this.)
- `read_ram <filename> [<address>] [<length>]` --- reads a portion of or all
RAM.
- `read_flash <filename> [<address>] [<length>]` --- reads a portion of or all
the flash. This is very slow.
- `write_flash <filename> [<address>] [<length>]` --- erases and then writes to
the flash. Only the pages needed are erased. Note that the radio calibration
values are set in the factory and stored in flash at 0x77000. If you are ever
going to want to use Bluetooth, don't overwrite this.
- `run` --- takes the device out of reset.

There are others. They may or may not work.

In addition, the control protocol is faintly intended to be human readable ---
connect to it and type a `?` and you'll get a very brief list of commands.

## Why not?

This has only been tested on a TLSR8232 and there will inevitably be problems on
other devices. On the other hand, this will make finding bugs very easy!

## Who?

All the code here has been bodged together from multiple sources, but the main
author of the main bit of code is me, David Given <dg@cowlark.com>. I have a
website at http://cowlark.com. There may or may not be anything interesting
there.

## License

Everything here is, as far as I know, either public domain or redistributable
under the terms of the MIT license. See the (LICENSE.md)[LICENSE.md] file for
the full text. For more information about the authors look at the header of each
file.
