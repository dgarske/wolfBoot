# VMK180 wolfBoot Development Tools

This directory contains automation scripts for developing and testing wolfBoot on the AMD Versal VMK180 evaluation board.

## Hardware Setup

### USB Connections

The VMK180 development setup includes:

| Device | Port | Description |
|--------|------|-------------|
| USB Relay Controller | `/dev/ttyACM0` | STM32-based relay board for boot mode & reset control |
| VMK180 APU UART | `/dev/ttyUSB6` | Main console (Cortex-A72) |
| VMK180 PMC UART | `/dev/ttyUSB5` | Platform Management Controller |
| VMK180 RPU UART | `/dev/ttyUSB7` | Real-time Processing Unit (Cortex-R5F) |
| VMK180 JTAG | `/dev/ttyUSB4` | JTAG/Debug interface |

### Boot Mode Control

The relay board controls the VMK180 boot mode pins and reset line:

| Relay | Function |
|-------|----------|
| Relay 1 | Reset line (ON = reset asserted) |
| Relay 2 | Boot mode bit 0 |
| Relay 3 | Boot mode bit 1 |
| Relay 4 | Boot mode bit 2 |

Boot mode patterns (format: R4 R3 R2 R1):

| Mode | Pattern | Description |
|------|---------|-------------|
| SDCard | `0000` | Boot from SD card |
| QSPI | `0011` | Boot from QSPI flash |
| JTAG | `0111` | JTAG boot mode (for debugging) |
| Reset | `1000` | Reset asserted |

## Scripts

### vmk180_identify_ports.py

Scans and identifies USB serial ports:

```bash
# List all ports with details
./vmk180_identify_ports.py

# Verbose output
./vmk180_identify_ports.py -v

# Test ports to identify them
./vmk180_identify_ports.py --test

# Generate udev rules for persistent naming
./vmk180_identify_ports.py --udev

# Save configuration file
./vmk180_identify_ports.py --save
```

### vmk180_control.py

High-level board control for common development tasks:

```bash
# Set boot mode
./vmk180_control.py sdcard      # SDCard boot mode
./vmk180_control.py jtag        # JTAG boot mode
./vmk180_control.py qspi        # QSPI boot mode

# Reset the board
./vmk180_control.py reset

# Reset and boot with specific mode
./vmk180_control.py boot-sdcard
./vmk180_control.py boot-jtag
./vmk180_control.py boot-qspi

# Open UART console
./vmk180_control.py console

# Reset, boot, and open console
./vmk180_control.py boot-sdcard --console

# Capture boot log (reset + capture for 30 seconds)
./vmk180_control.py capture -t 30 -o boot_log.txt

# Turn all relays off
./vmk180_control.py off

# Query relay status
./vmk180_control.py status

# Verbose mode
./vmk180_control.py -v reset
```

### Configuration Options

The scripts can be configured via command line or `vmk180_ports.conf`:

```bash
# Command line options
./vmk180_control.py --relay-port /dev/ttyACM0 --uart-port /dev/ttyUSB6 reset

# Or edit vmk180_ports.conf
```

## Installation

### Prerequisites

```bash
# Install pyserial
pip install pyserial

# Add user to dialout group (for serial port access)
sudo usermod -a -G dialout $USER
# Log out and back in for group change to take effect
```

### udev Rules (Recommended)

For persistent device naming, install udev rules:

```bash
# Generate rules
./vmk180_identify_ports.py --udev

# Install rules
sudo cp 99-vmk180.rules /etc/udev/rules.d/

# Reload udev
sudo udevadm control --reload-rules
sudo udevadm trigger
```

This creates symlinks like:
- `/dev/vmk180_uart` → VMK180 APU UART
- `/dev/vmk180_relay` → USB relay controller

## Typical Development Workflow

### 1. Initial Setup

```bash
# Identify ports
./vmk180_identify_ports.py -v

# Save configuration
./vmk180_identify_ports.py --save

# Test relay control
./vmk180_control.py -v sdcard
```

### 2. Boot from SD Card

```bash
# Build wolfBoot
cd /path/to/wolfboot
cp config/examples/versal_vmk180.config .config
make clean
make

# Copy to SD card
# ... (board-specific steps)

# Reset and boot
./vmk180_control.py boot-sdcard --console
```

### 3. JTAG Debugging

```bash
# Set JTAG boot mode
./vmk180_control.py boot-jtag

# Connect Trace32 debugger
# ...
```

### 4. Capture Boot Log

```bash
# Capture 60 seconds of boot output
./vmk180_control.py capture -t 60 -o wolfboot_boot.log
```

## Files

| File | Description |
|------|-------------|
| `vmk180_identify_ports.py` | USB port identification script |
| `vmk180_control.py` | Board control wrapper script |
| `vmk180_ports.conf` | Port configuration file |
| `99-vmk180.rules` | udev rules (generated) |
| `README.md` | This file |

## Related wolfBoot Files

| File | Description |
|------|-------------|
| `config/examples/versal_vmk180.config` | Build configuration (TODO) |
| `hal/versal.c` | HAL implementation (TODO) |
| `hal/versal.h` | Register definitions (TODO) |

## Troubleshooting

### Permission Denied on Serial Ports

```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER
# Log out and back in
```

### Relay Not Responding

1. Check that `/dev/ttyACM0` exists
2. Verify the relay board is powered
3. Test with: `python3 /home/davidgarske/relays.py status all`

### Wrong UART Port

The VMK180 has 4 UART ports. The main console is typically:
- Interface 2 (ttyUSB6 if VMK180 is the first FTDI device)

Use `vmk180_identify_ports.py` to scan and identify the correct port.

## Hardware Reference

- **Board**: AMD Versal VMK180 Evaluation Kit
- **SoC**: Versal Prime VM1802
- **CPU**: Dual ARM Cortex-A72 (APU) + Dual ARM Cortex-R5F (RPU)
- **Boot Controller**: Platform Management Controller (PMC)

