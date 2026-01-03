#!/usr/bin/env python3
"""
vmk180_identify_ports.py — Identify and catalog USB serial ports for VMK180 development.

This script scans all connected USB serial devices and displays their properties
to help identify which port corresponds to which device:
  - VMK180 UART console (typically FTDI or Silicon Labs on the eval board)
  - USB Relay controller (typically ttyACM0)
  - Lauterbach Trace32 (if connected via USB-serial)

Usage:
  python3 vmk180_identify_ports.py              # List all ports with details
  python3 vmk180_identify_ports.py --test       # Test each port with a probe
  python3 vmk180_identify_ports.py --save       # Save config to vmk180_ports.conf
  python3 vmk180_identify_ports.py --udev       # Generate udev rules
"""

import argparse
import glob
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

try:
    import serial
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False


# Known USB device identifiers
KNOWN_DEVICES = {
    # VMK180 onboard FTDI (Silicon Labs CP210x typically)
    ("10c4", "ea60"): "Silicon Labs CP210x (likely VMK180 UART)",
    ("0403", "6010"): "FTDI FT2232H (likely VMK180 UART or JTAG)",
    ("0403", "6011"): "FTDI FT4232H (likely VMK180 UART)",
    ("0403", "6014"): "FTDI FT232H",
    ("0403", "6001"): "FTDI FT232R",
    # USB Relay boards
    ("1a86", "7523"): "CH340 (common USB relay controller)",
    ("067b", "2303"): "Prolific PL2303 (serial adapter)",
    # Lauterbach
    ("0897", "0002"): "Lauterbach TRACE32",
    # Generic CDC-ACM (ttyACM*)
    ("cdc", "acm"): "USB CDC-ACM device",
}


def get_udev_info(device: str) -> Dict[str, str]:
    """Get udev properties for a device using udevadm."""
    info = {}
    try:
        result = subprocess.run(
            ["udevadm", "info", "--query=property", "--name=" + device],
            capture_output=True,
            text=True,
            timeout=5
        )
        if result.returncode == 0:
            for line in result.stdout.strip().split('\n'):
                if '=' in line:
                    key, val = line.split('=', 1)
                    info[key] = val
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass
    return info


def get_usb_path_info(device: str) -> Dict[str, str]:
    """Get USB path information from sysfs."""
    info = {}

    # Find the sysfs path for the device
    dev_name = os.path.basename(device)
    sysfs_paths = [
        f"/sys/class/tty/{dev_name}/device",
        f"/sys/class/tty/{dev_name}",
    ]

    for sysfs_path in sysfs_paths:
        if os.path.exists(sysfs_path):
            # Follow links to find USB device info
            real_path = os.path.realpath(sysfs_path)

            # Walk up to find USB device properties
            path = real_path
            for _ in range(10):  # Max depth
                parent = os.path.dirname(path)

                # Check for USB identifiers
                for prop in ["idVendor", "idProduct", "manufacturer", "product", "serial"]:
                    prop_file = os.path.join(path, prop)
                    if os.path.exists(prop_file):
                        try:
                            with open(prop_file, 'r') as f:
                                info[prop] = f.read().strip()
                        except (IOError, PermissionError):
                            pass

                if "idVendor" in info:
                    break
                path = parent
                if path == "/":
                    break

    return info


def probe_port_baud(device: str, baud: int = 115200, timeout: float = 0.5) -> Optional[str]:
    """Try to read data from a serial port to identify it."""
    if not HAS_SERIAL:
        return None

    try:
        with serial.Serial(device, baud, timeout=timeout) as ser:
            ser.reset_input_buffer()
            # Wait a bit and see if any data arrives
            time.sleep(timeout)
            data = ser.read(ser.in_waiting or 100)
            if data:
                # Try to decode as text
                try:
                    return data.decode('utf-8', errors='replace')[:80]
                except:
                    return f"[binary data: {len(data)} bytes]"
    except (serial.SerialException, OSError, PermissionError) as e:
        return f"[error: {e}]"

    return None


def test_relay_port(device: str) -> bool:
    """Test if this looks like the USB relay controller."""
    if not HAS_SERIAL:
        return False

    # Relay protocol: send query command and see if we get response
    try:
        with serial.Serial(device, 115200, timeout=0.5) as ser:
            ser.reset_input_buffer()
            # Send a status query for relay 1: [0xA0, 0x01, 0x02, checksum]
            cmd = bytes([0xA0, 0x01, 0x02, (0xA0 + 0x01 + 0x02) & 0xFF])
            ser.write(cmd)
            ser.flush()
            time.sleep(0.2)
            response = ser.read(10)
            # Relay typically responds with similar frame format
            if response and len(response) >= 4 and response[0] == 0xA0:
                return True
    except (serial.SerialException, OSError, PermissionError):
        pass

    return False


def scan_ports() -> List[Dict]:
    """Scan all USB serial ports and gather information."""
    ports = []

    # Find all ttyUSB* and ttyACM* devices
    devices = sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))

    for device in devices:
        port_info = {
            "device": device,
            "type": "ttyACM" if "ACM" in device else "ttyUSB",
            "exists": os.path.exists(device),
            "readable": os.access(device, os.R_OK),
            "writable": os.access(device, os.W_OK),
        }

        # Get udev info
        udev = get_udev_info(device)
        port_info["udev"] = udev

        # Get USB sysfs info
        usb_info = get_usb_path_info(device)
        port_info["usb"] = usb_info

        # Determine likely identity
        vid = usb_info.get("idVendor", udev.get("ID_VENDOR_ID", "")).lower()
        pid = usb_info.get("idProduct", udev.get("ID_MODEL_ID", "")).lower()

        # Check known devices
        device_id = (vid, pid)
        if device_id in KNOWN_DEVICES:
            port_info["likely"] = KNOWN_DEVICES[device_id]
        elif "ACM" in device:
            port_info["likely"] = "USB CDC-ACM (possibly relay controller)"
        else:
            port_info["likely"] = "Unknown USB serial"

        # Get human-readable names
        port_info["manufacturer"] = usb_info.get("manufacturer", udev.get("ID_VENDOR", "Unknown"))
        port_info["product"] = usb_info.get("product", udev.get("ID_MODEL", "Unknown"))
        port_info["serial"] = usb_info.get("serial", udev.get("ID_SERIAL_SHORT", ""))
        port_info["vid"] = vid
        port_info["pid"] = pid

        ports.append(port_info)

    return ports


def print_port_table(ports: List[Dict], verbose: bool = False):
    """Print a formatted table of ports."""
    print("\n" + "=" * 80)
    print("USB Serial Port Inventory")
    print("=" * 80)

    if not ports:
        print("No USB serial ports found!")
        return

    for port in ports:
        device = port["device"]
        access = ""
        if port["readable"]:
            access += "R"
        if port["writable"]:
            access += "W"

        print(f"\n{device} [{access}]")
        print("-" * 40)
        print(f"  VID:PID       : {port['vid']}:{port['pid']}")
        print(f"  Manufacturer  : {port['manufacturer']}")
        print(f"  Product       : {port['product']}")
        if port["serial"]:
            print(f"  Serial        : {port['serial']}")
        print(f"  Likely Device : {port['likely']}")

        if verbose:
            print(f"  udev info     : {port.get('udev', {})}")


def test_ports(ports: List[Dict]):
    """Test ports to help identify them."""
    print("\n" + "=" * 80)
    print("Port Testing")
    print("=" * 80)

    for port in ports:
        device = port["device"]

        if not (port["readable"] and port["writable"]):
            print(f"\n{device}: Skipping (no access)")
            continue

        print(f"\n{device}:")

        # Test for relay
        if "ACM" in device:
            print("  Testing for USB relay...", end=" ")
            if test_relay_port(device):
                print("YES - This appears to be the relay controller!")
                port["identified"] = "relay"
            else:
                print("No response")

        # Probe for data
        print("  Probing for serial data...", end=" ")
        data = probe_port_baud(device)
        if data:
            print(f"\n    Received: {data[:60]}...")
        else:
            print("No data")


def generate_udev_rules(ports: List[Dict]) -> str:
    """Generate udev rules for persistent device naming."""
    rules = []
    rules.append("# VMK180 Development Board USB Device Rules")
    rules.append("# Install to: /etc/udev/rules.d/99-vmk180.rules")
    rules.append("# Reload with: sudo udevadm control --reload-rules && sudo udevadm trigger")
    rules.append("")

    for port in ports:
        vid = port["vid"]
        pid = port["pid"]
        serial = port["serial"]
        product = port["product"]

        if not vid or not pid:
            continue

        # Determine symlink name based on what we know
        if port.get("identified") == "relay" or "ACM" in port["device"]:
            symlink = "vmk180_relay"
            comment = "USB Relay Controller"
        elif "FTDI" in port.get("likely", "") or "CP210" in port.get("likely", ""):
            # Multiple FTDI ports, use interface number
            symlink = "vmk180_uart"
            comment = "VMK180 UART Console"
        else:
            symlink = f"vmk180_{os.path.basename(port['device'])}"
            comment = port.get("likely", "Unknown device")

        rule = f'# {comment}: {product}'
        rules.append(rule)

        if serial:
            rule = f'SUBSYSTEM=="tty", ATTRS{{idVendor}}=="{vid}", ATTRS{{idProduct}}=="{pid}", ATTRS{{serial}}=="{serial}", SYMLINK+="{symlink}"'
        else:
            rule = f'SUBSYSTEM=="tty", ATTRS{{idVendor}}=="{vid}", ATTRS{{idProduct}}=="{pid}", SYMLINK+="{symlink}"'
        rules.append(rule)
        rules.append("")

    return "\n".join(rules)


def save_config(ports: List[Dict], filename: str = "vmk180_ports.conf"):
    """Save port configuration to a file."""
    config = []
    config.append("# VMK180 Port Configuration")
    config.append("# Generated by vmk180_identify_ports.py")
    config.append(f"# Date: {time.strftime('%Y-%m-%d %H:%M:%S')}")
    config.append("")

    for port in ports:
        device = port["device"]
        likely = port.get("likely", "unknown")
        vid = port["vid"]
        pid = port["pid"]

        if port.get("identified") == "relay" or "relay" in likely.lower() or "ACM" in device:
            config.append(f"RELAY_PORT={device}")
        elif "UART" in likely.upper() or "FTDI" in likely.upper() or "CP210" in likely.upper():
            config.append(f"UART_PORT={device}")

    config.append("")
    config.append("# All detected ports:")
    for port in ports:
        config.append(f"# {port['device']}: VID={port['vid']} PID={port['pid']} - {port.get('likely', 'unknown')}")

    with open(filename, 'w') as f:
        f.write("\n".join(config))

    print(f"\nConfiguration saved to: {filename}")


def main():
    parser = argparse.ArgumentParser(
        description="Identify USB serial ports for VMK180 development",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s              # List all ports
  %(prog)s --test       # Test ports to identify them
  %(prog)s --udev       # Generate udev rules
  %(prog)s --save       # Save configuration
  %(prog)s -v           # Verbose output
        """
    )
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    parser.add_argument("--test", action="store_true", help="Test ports to identify them")
    parser.add_argument("--udev", action="store_true", help="Generate udev rules")
    parser.add_argument("--save", action="store_true", help="Save config to vmk180_ports.conf")

    args = parser.parse_args()

    if not HAS_SERIAL:
        print("Warning: pyserial not installed. Some features disabled.")
        print("Install with: pip install pyserial")
        print()

    # Scan ports
    ports = scan_ports()

    # Print table
    print_port_table(ports, verbose=args.verbose)

    # Test if requested
    if args.test:
        test_ports(ports)

    # Generate udev rules
    if args.udev:
        print("\n" + "=" * 80)
        print("Suggested udev Rules")
        print("=" * 80)
        rules = generate_udev_rules(ports)
        print(rules)

        # Offer to save
        rules_file = "99-vmk180.rules"
        with open(rules_file, 'w') as f:
            f.write(rules)
        print(f"\nRules saved to: {rules_file}")
        print(f"To install: sudo cp {rules_file} /etc/udev/rules.d/")
        print("To reload:  sudo udevadm control --reload-rules && sudo udevadm trigger")

    # Save config
    if args.save:
        save_config(ports)

    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())

