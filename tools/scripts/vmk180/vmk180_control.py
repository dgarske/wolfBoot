#!/usr/bin/env python3
"""
vmk180_control.py — High-level board control for VMK180 development.

This script wraps relay control and serial console functionality to provide
simple commands for common development tasks.

Boot Mode Pin Mapping (directly from user's relay configuration):
  Relay 1: Reset line (active high = reset asserted)
  Relay 2-4: Boot mode pins

Boot Modes (relay bits 4321 = R4 R3 R2 R1):
  0111 = JTAG boot mode      (relays 2,3,4 ON, reset OFF)
  0011 = QSPI boot mode      (relays 2,3 ON, 4 OFF, reset OFF)
  0000 = SDCard boot mode    (all relays OFF)
  1000 = Reset asserted      (relay 1 ON)

Usage:
  vmk180_control.py reset          # Reset the board
  vmk180_control.py jtag           # Set JTAG boot mode
  vmk180_control.py sdcard         # Set SDCard boot mode
  vmk180_control.py qspi           # Set QSPI boot mode
  vmk180_control.py boot-jtag      # Reset + set JTAG mode + boot
  vmk180_control.py boot-sdcard    # Reset + set SDCard mode + boot
  vmk180_control.py boot-qspi      # Reset + set QSPI mode + boot
  vmk180_control.py status         # Show current relay status
  vmk180_control.py console        # Open UART console
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

try:
    import serial
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False


# Default configuration
DEFAULT_RELAY_PORT = "/dev/ttyACM0"
DEFAULT_UART_PORT = "/dev/ttyUSB0"
DEFAULT_UART_BAUD = 115200

# Boot mode definitions (relay pattern: R4 R3 R2 R1)
BOOT_MODES = {
    "jtag":   "0111",  # JTAG boot
    "sdcard": "0000",  # SDCard boot
    "qspi":   "0011",  # QSPI boot
    "reset":  "1000",  # Reset asserted (relay 1 on)
}

# Configuration file path (relative to this script or in current dir)
CONFIG_FILE = "vmk180_ports.conf"


def load_config() -> dict:
    """Load configuration from vmk180_ports.conf if it exists."""
    config = {
        "relay_port": DEFAULT_RELAY_PORT,
        "uart_port": DEFAULT_UART_PORT,
        "uart_baud": DEFAULT_UART_BAUD,
    }

    # Check multiple locations for config file
    search_paths = [
        Path.cwd() / CONFIG_FILE,
        Path(__file__).parent / CONFIG_FILE,
        Path.home() / ".config" / CONFIG_FILE,
    ]

    for config_path in search_paths:
        if config_path.exists():
            with open(config_path, 'r') as f:
                for line in f:
                    line = line.strip()
                    if line.startswith('#') or '=' not in line:
                        continue
                    key, value = line.split('=', 1)
                    key = key.strip().lower()
                    value = value.strip()
                    if key == "relay_port":
                        config["relay_port"] = value
                    elif key == "uart_port":
                        config["uart_port"] = value
                    elif key == "uart_baud":
                        config["uart_baud"] = int(value)
            break

    return config


def find_relays_script() -> Optional[str]:
    """Find the relays.py script in common locations."""
    search_paths = [
        Path.home() / "relays.py",
        Path.cwd() / "relays.py",
        Path(__file__).parent / "relays.py",
        Path("/usr/local/bin/relays.py"),
    ]

    for path in search_paths:
        if path.exists():
            return str(path)

    return None


class VMK180Controller:
    """Controller for VMK180 development board."""

    def __init__(self, relay_port: str = DEFAULT_RELAY_PORT,
                 uart_port: str = DEFAULT_UART_PORT,
                 uart_baud: int = DEFAULT_UART_BAUD,
                 relays_script: Optional[str] = None,
                 verbose: bool = False):
        self.relay_port = relay_port
        self.uart_port = uart_port
        self.uart_baud = uart_baud
        self.relays_script = relays_script or find_relays_script()
        self.verbose = verbose

        if not self.relays_script:
            raise RuntimeError("Cannot find relays.py script")

    def _run_relay_cmd(self, *args) -> bool:
        """Run a relay command using the relays.py script."""
        cmd = ["python3", self.relays_script, "--port", self.relay_port] + list(args)

        if self.verbose:
            print(f"  Running: {' '.join(cmd)}")

        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
            if self.verbose and result.stdout:
                print(f"  Output: {result.stdout.strip()}")
            if result.returncode != 0:
                if result.stderr:
                    print(f"  Error: {result.stderr.strip()}", file=sys.stderr)
                return False
            return True
        except subprocess.TimeoutExpired:
            print("  Relay command timed out!", file=sys.stderr)
            return False
        except FileNotFoundError:
            print(f"  Cannot find: {self.relays_script}", file=sys.stderr)
            return False

    def set_mode(self, mode_pattern: str) -> bool:
        """Set relay mode pattern (4 binary digits)."""
        return self._run_relay_cmd("mode", mode_pattern)

    def reset_board(self, hold_time: float = 0.2) -> bool:
        """Perform a board reset (assert then deassert reset line)."""
        print("Resetting board...")

        # Assert reset
        if not self.set_mode(BOOT_MODES["reset"]):
            return False

        # Hold reset for specified time
        time.sleep(hold_time)

        # Deassert reset (return to SDCard mode as default)
        if not self.set_mode(BOOT_MODES["sdcard"]):
            return False

        print("Reset complete.")
        return True

    def set_boot_mode(self, mode: str) -> bool:
        """Set the boot mode (jtag, sdcard, qspi)."""
        if mode not in BOOT_MODES:
            print(f"Unknown boot mode: {mode}", file=sys.stderr)
            print(f"Valid modes: {', '.join(BOOT_MODES.keys())}", file=sys.stderr)
            return False

        pattern = BOOT_MODES[mode]
        print(f"Setting boot mode: {mode} (pattern: {pattern})")
        return self.set_mode(pattern)

    def boot_with_mode(self, mode: str, reset_hold: float = 0.2,
                       boot_delay: float = 0.1) -> bool:
        """Reset the board and boot with specified mode."""
        if mode not in BOOT_MODES or mode == "reset":
            print(f"Invalid boot mode: {mode}", file=sys.stderr)
            return False

        print(f"Booting in {mode.upper()} mode...")

        # Set desired boot mode FIRST (while reset is asserted)
        # Reset pattern: 1XXX where XXX is the boot mode
        boot_pattern = BOOT_MODES[mode]
        reset_with_mode = "1" + boot_pattern[1:]  # Keep boot mode, add reset

        # Assert reset with boot mode set
        print(f"  Assert reset with boot mode...")
        if not self.set_mode(reset_with_mode):
            return False

        time.sleep(reset_hold)

        # Release reset while maintaining boot mode
        print(f"  Release reset...")
        if not self.set_mode(boot_pattern):
            return False

        time.sleep(boot_delay)
        print(f"Board is now booting in {mode.upper()} mode.")
        return True

    def status(self) -> bool:
        """Query and display current relay status."""
        print("Querying relay status...")
        return self._run_relay_cmd("status", "all")

    def all_off(self) -> bool:
        """Turn all relays off (SDCard mode, reset deasserted)."""
        print("All relays off...")
        return self.set_mode("0000")

    def console(self, timeout: Optional[float] = None) -> bool:
        """Open a serial console to the board."""
        if not HAS_SERIAL:
            # Fall back to minicom/screen
            print(f"Opening console on {self.uart_port} at {self.uart_baud} baud...")
            print("(Press Ctrl+A then X to exit minicom, or Ctrl+A then \\ for screen)")

            # Try minicom first, then screen
            for prog in ["minicom", "screen"]:
                try:
                    if prog == "minicom":
                        cmd = ["minicom", "-D", self.uart_port, "-b", str(self.uart_baud)]
                    else:
                        cmd = ["screen", self.uart_port, str(self.uart_baud)]

                    subprocess.run(cmd)
                    return True
                except FileNotFoundError:
                    continue

            print("No serial terminal found. Install minicom or screen.", file=sys.stderr)
            return False

        # Use pyserial directly
        print(f"Opening console on {self.uart_port} at {self.uart_baud} baud...")
        print("Press Ctrl+C to exit.")
        print("-" * 60)

        try:
            with serial.Serial(self.uart_port, self.uart_baud, timeout=0.1) as ser:
                start_time = time.time()
                while True:
                    if timeout and (time.time() - start_time) > timeout:
                        print("\n[Timeout reached]")
                        break

                    # Read available data
                    if ser.in_waiting:
                        data = ser.read(ser.in_waiting)
                        try:
                            sys.stdout.write(data.decode('utf-8', errors='replace'))
                            sys.stdout.flush()
                        except:
                            pass
                    else:
                        time.sleep(0.01)

        except KeyboardInterrupt:
            print("\n[Console closed]")
        except serial.SerialException as e:
            print(f"Serial error: {e}", file=sys.stderr)
            return False

        return True

    def capture_boot_log(self, timeout: float = 30.0,
                         output_file: Optional[str] = None) -> str:
        """Capture boot log output to a string/file."""
        if not HAS_SERIAL:
            print("pyserial required for boot log capture", file=sys.stderr)
            return ""

        print(f"Capturing boot log for {timeout}s from {self.uart_port}...")

        log_data = []
        try:
            with serial.Serial(self.uart_port, self.uart_baud, timeout=0.1) as ser:
                ser.reset_input_buffer()
                start_time = time.time()

                while (time.time() - start_time) < timeout:
                    if ser.in_waiting:
                        data = ser.read(ser.in_waiting)
                        try:
                            text = data.decode('utf-8', errors='replace')
                            log_data.append(text)
                            sys.stdout.write(text)
                            sys.stdout.flush()
                        except:
                            pass
                    else:
                        time.sleep(0.01)

        except KeyboardInterrupt:
            print("\n[Capture interrupted]")
        except serial.SerialException as e:
            print(f"Serial error: {e}", file=sys.stderr)

        log_text = "".join(log_data)

        if output_file:
            with open(output_file, 'w') as f:
                f.write(log_text)
            print(f"\nLog saved to: {output_file}")

        return log_text


def main():
    parser = argparse.ArgumentParser(
        description="VMK180 Board Control",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Commands:
  reset         Reset the board
  jtag          Set JTAG boot mode
  sdcard        Set SDCard boot mode
  qspi          Set QSPI boot mode
  boot-jtag     Reset and boot in JTAG mode
  boot-sdcard   Reset and boot in SDCard mode
  boot-qspi     Reset and boot in QSPI mode
  status        Show relay status
  off           Turn all relays off
  console       Open UART console
  capture       Capture boot log (with reset)

Examples:
  %(prog)s reset                    # Just reset the board
  %(prog)s boot-sdcard              # Reset and boot from SD
  %(prog)s boot-jtag --console      # Boot JTAG + open console
  %(prog)s capture -t 30 -o log.txt # Capture 30s of boot log
        """
    )

    parser.add_argument("command",
                        choices=["reset", "jtag", "sdcard", "qspi",
                                "boot-jtag", "boot-sdcard", "boot-qspi",
                                "status", "off", "console", "capture"],
                        help="Command to execute")
    parser.add_argument("--relay-port", default=None,
                        help=f"Relay controller port (default: from config or {DEFAULT_RELAY_PORT})")
    parser.add_argument("--uart-port", default=None,
                        help=f"UART console port (default: from config or {DEFAULT_UART_PORT})")
    parser.add_argument("--uart-baud", type=int, default=None,
                        help=f"UART baud rate (default: {DEFAULT_UART_BAUD})")
    parser.add_argument("--relays-script", default=None,
                        help="Path to relays.py script")
    parser.add_argument("-t", "--timeout", type=float, default=30.0,
                        help="Timeout for capture command (default: 30s)")
    parser.add_argument("-o", "--output", default=None,
                        help="Output file for capture command")
    parser.add_argument("--console", action="store_true",
                        help="Open console after boot command")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Verbose output")

    args = parser.parse_args()

    # Load config
    config = load_config()

    # Override with command line args
    relay_port = args.relay_port or config["relay_port"]
    uart_port = args.uart_port or config["uart_port"]
    uart_baud = args.uart_baud or config["uart_baud"]

    # Create controller
    try:
        ctrl = VMK180Controller(
            relay_port=relay_port,
            uart_port=uart_port,
            uart_baud=uart_baud,
            relays_script=args.relays_script,
            verbose=args.verbose
        )
    except RuntimeError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    # Execute command
    success = False

    if args.command == "reset":
        success = ctrl.reset_board()

    elif args.command in ["jtag", "sdcard", "qspi"]:
        success = ctrl.set_boot_mode(args.command)

    elif args.command.startswith("boot-"):
        mode = args.command.replace("boot-", "")
        success = ctrl.boot_with_mode(mode)
        if success and args.console:
            ctrl.console()

    elif args.command == "status":
        success = ctrl.status()

    elif args.command == "off":
        success = ctrl.all_off()

    elif args.command == "console":
        success = ctrl.console()

    elif args.command == "capture":
        # Reset and capture boot log
        if ctrl.boot_with_mode("sdcard"):
            ctrl.capture_boot_log(timeout=args.timeout, output_file=args.output)
            success = True

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())

