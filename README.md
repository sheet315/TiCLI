# TiCLI
A Command-line interface for managing a Texas Instruments calculator, to, for example disable test mode on Linux.

I made this because I needed a reliable way to disable a Ti84's test mode on Linux, since Ti Connect CE is only compatible with Windows and Mac.

# How to compile
Dependencies:
- libusb 1.0
- g++

Compile:
```
make build
```
The binary will be built in the `bin/` directory.

Move to path:
```
sudo make install
```
