#!/bin/bash
#
# STM32 Board Testing Script
#
# Easily switch between multiple STM32 boards for testing
# Auto-detects UART devices and ST-Link serial numbers
# Always builds with DEBUG_UART=1 and DEBUG_SYMBOLS=1
#

set -e

# Configuration
CONFIG_FILE="${HOME}/.wolfboot_boards"
DEFAULT_BAUD=115200
DEFAULT_FLASH_ADDR=0x08000000
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Global variables
SELECTED_BOARD=""
BOARD_UART=""
BOARD_SERIAL=""
BOARD_TARGET=""
BOARD_CONFIG=""

# Print colored messages
info() { echo -e "${CYAN}[INFO]${NC} $*"; }
success() { echo -e "${GREEN}[OK]${NC} $*"; }
warning() { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

# Detect all STM32 UART devices
# Returns: boards array via global variable DETECTED_BOARDS and DETECTED_UARTS
# Note: ST-Link provides TWO interfaces:
#   - Debug interface (model_id=374b) - for flashing/debugging
#   - Virtual COM Port (model_id=5740) - for UART monitoring
# We detect the VCP interface (5740) for UART, matching by serial number
detect_boards() {
    DETECTED_BOARDS=()
    DETECTED_UARTS=()

    info "Scanning for STM32 boards..."

    local found=0
    local -A vcp_devices  # Map serial -> VCP device

    # First pass: Find all ST-Link debug interfaces and VCP interfaces
    local -A stlink_debug_serials  # Map serial -> device path prefix
    local -A vcp_by_path  # Map device path prefix -> VCP device

    for uart in /dev/ttyACM*; do
        if [ ! -e "$uart" ]; then
            continue
        fi

        # Get USB device info
        local vendor_id=$(udevadm info "$uart" 2>/dev/null | grep "ID_VENDOR_ID=" | cut -d= -f2)
        local model_id=$(udevadm info "$uart" 2>/dev/null | grep "ID_MODEL_ID=" | cut -d= -f2)
        local serial=$(udevadm info "$uart" 2>/dev/null | grep "ID_SERIAL_SHORT=" | cut -d= -f2)
        local devpath=$(udevadm info "$uart" 2>/dev/null | grep "DEVPATH=" | cut -d= -f2)

        if [ "$vendor_id" != "0483" ] || [ -z "$devpath" ]; then
            continue
        fi

        # Extract USB device path prefix (up to the device, before interface)
        # e.g., /devices/pci0000:00/0000:00:14.0/usb1/1-5/1-5.4/1-5.4:1.2/tty/ttyACM1
        # becomes: /devices/pci0000:00/0000:00:14.0/usb1/1-5/1-5.4
        # Remove /tty/* suffix first, then remove interface part
        local path_prefix=$(echo "$devpath" | sed 's|/tty/.*||' | sed 's|:[0-9.]*$||' | sed 's|/[^/]*$||')

        if [ "$model_id" = "374b" ]; then
            # ST-Link debug interface
            if [ -n "$serial" ]; then
                stlink_debug_serials["$serial"]="$path_prefix"
                info "  Found ST-Link debug: $uart -> Serial: $serial"
            fi
        elif [ "$model_id" = "5740" ]; then
            # VCP interface - store by path prefix
            vcp_by_path["$path_prefix"]="$uart"
            info "  Found VCP: $uart -> Path: $path_prefix"
        fi
    done

    # Second pass: Use st-info to identify MCU type for each ST-Link
    # Probe each ST-Link individually since --probe might not show all
    local -A mcu_types  # Map serial -> MCU type (e.g., "stm32c0", "stm32g0")

    if command -v st-info >/dev/null 2>&1; then
        # First try --probe to get all ST-Links
        local st_info_output=$(st-info --probe 2>&1 | grep -E "(serial:|chipid:|dev-type:|flash:)" || true)

        # Parse --probe output
        local current_serial=""
        if [ -n "$st_info_output" ]; then
            while IFS= read -r line; do
                if echo "$line" | grep -q "serial:"; then
                    current_serial=$(echo "$line" | sed 's/.*serial:[[:space:]]*//' | tr -d '[:space:]')
                elif echo "$line" | grep -q "dev-type:" && [ -n "$current_serial" ]; then
                    local dev_type=$(echo "$line" | sed 's/.*dev-type:[[:space:]]*//' | tr -d '[:space:]')
                    case "$dev_type" in
                        *STM32C031xx*|*STM32C0*)
                            mcu_types["$current_serial"]="stm32c0"
                            ;;
                        *STM32G07x*|*STM32G08x*|*STM32G0*)
                            mcu_types["$current_serial"]="stm32g0"
                            ;;
                        *STM32WB*)
                            mcu_types["$current_serial"]="stm32wb"
                            ;;
                        *STM32L4*)
                            mcu_types["$current_serial"]="stm32l4"
                            ;;
                    esac
                fi
            done <<< "$st_info_output"
        fi

        # Also probe each ST-Link individually (in case --probe missed some)
        for serial in "${!stlink_debug_serials[@]}"; do
            if [ -z "${mcu_types[$serial]}" ]; then
                local probe_output=$(st-info --serial="$serial" --probe 2>&1 | grep -E "(dev-type:|flash:)" || true)
                local dev_type=$(echo "$probe_output" | grep "dev-type:" | sed 's/.*dev-type:[[:space:]]*//' | tr -d '[:space:]')
                case "$dev_type" in
                    *STM32C031xx*|*STM32C0*)
                        mcu_types["$serial"]="stm32c0"
                        ;;
                    *STM32G07x*|*STM32G08x*|*STM32G0*)
                        mcu_types["$serial"]="stm32g0"
                        ;;
                    *STM32WB*)
                        mcu_types["$serial"]="stm32wb"
                        ;;
                    *STM32L4*)
                        mcu_types["$serial"]="stm32l4"
                        ;;
                esac
            fi
        done
    fi

    # Third pass: Match VCP devices with ST-Link debuggers by USB device path
    for serial in "${!stlink_debug_serials[@]}"; do
        local path_prefix="${stlink_debug_serials[$serial]}"
        local mcu_type="${mcu_types[$serial]:-unknown}"

        # Try to find VCP on the same USB device (same path prefix)
        if [ -n "${vcp_by_path[$path_prefix]}" ]; then
            # Found VCP on same device
            DETECTED_BOARDS+=("$serial")
            DETECTED_UARTS+=("${vcp_by_path[$path_prefix]}")
            if [ "$mcu_type" != "unknown" ]; then
                info "  Matched ST-Link: Serial $serial -> VCP: ${vcp_by_path[$path_prefix]} (MCU: $mcu_type)"
            else
                info "  Matched ST-Link: Serial $serial -> VCP: ${vcp_by_path[$path_prefix]}"
            fi
            found=1
        else
            # Try to find unmatched VCP interfaces
            # Match unmatched VCPs to unmatched ST-Links in order
            local matched_vcp=""
            local unmatched_vcps=()

            # Find VCPs that aren't matched yet
            for vcp_path in "${!vcp_by_path[@]}"; do
                local vcp_device="${vcp_by_path[$vcp_path]}"
                local already_matched=0
                for matched_uart in "${DETECTED_UARTS[@]}"; do
                    if [ "$matched_uart" = "$vcp_device" ]; then
                        already_matched=1
                        break
                    fi
                done
                if [ $already_matched -eq 0 ]; then
                    unmatched_vcps+=("$vcp_device")
                fi
            done

            # Use first unmatched VCP if available
            if [ ${#unmatched_vcps[@]} -gt 0 ]; then
                matched_vcp="${unmatched_vcps[0]}"
            fi

            if [ -n "$matched_vcp" ]; then
                # Found VCP on nearby device
                DETECTED_BOARDS+=("$serial")
                DETECTED_UARTS+=("$matched_vcp")
                if [ "$mcu_type" != "unknown" ]; then
                    info "  Matched ST-Link: Serial $serial -> VCP: $matched_vcp (MCU: $mcu_type, nearby device)"
                else
                    info "  Matched ST-Link: Serial $serial -> VCP: $matched_vcp (nearby device)"
                fi
                found=1
            else
                # No VCP found - use debug interface as fallback
                warning "ST-Link $serial found but no VCP interface - using debug interface"
                # Find the debug interface device
                for uart in /dev/ttyACM*; do
                    if [ ! -e "$uart" ]; then
                        continue
                    fi
                    local uart_serial=$(udevadm info "$uart" 2>/dev/null | grep "ID_SERIAL_SHORT=" | cut -d= -f2)
                    if [ "$uart_serial" = "$serial" ]; then
                        DETECTED_BOARDS+=("$serial")
                        DETECTED_UARTS+=("$uart")
                        found=1
                        break
                    fi
                done
            fi
        fi
    done

    # Fourth pass: Handle VCP interfaces without matching ST-Link debug interfaces
    # This can happen when multiple ST-Links are connected and some aren't fully enumerated
    for vcp_path in "${!vcp_by_path[@]}"; do
        local vcp_device="${vcp_by_path[$vcp_path]}"
        local already_matched=0

        # Check if this VCP is already matched
        for matched_uart in "${DETECTED_UARTS[@]}"; do
            if [ "$matched_uart" = "$vcp_device" ]; then
                already_matched=1
                break
            fi
        done

        if [ $already_matched -eq 0 ]; then
            # Unmatched VCP - try to identify MCU by probing with st-flash or st-info
            # Use a placeholder serial based on VCP serial
            local vcp_serial=$(udevadm info "$vcp_device" 2>/dev/null | grep "ID_SERIAL_SHORT=" | cut -d= -f2)
            if [ -n "$vcp_serial" ]; then
                # Try to probe this VCP's associated ST-Link (might be on different interface)
                # For now, add it as an unmatched board that user can configure manually
                warning "Found VCP $vcp_device without matching ST-Link debug interface"
                warning "  This may be a second ST-Link board. You can configure it manually using: $0 setup"
                # Don't add it automatically - let user configure it
            fi
        fi
    done

    if [ $found -eq 0 ]; then
        error "No STM32 boards detected!"
        return 1
    fi

    return 0
}

# Load board configuration from file
load_config() {
    if [ ! -f "$CONFIG_FILE" ]; then
        return 1
    fi

    # Source the config file
    . "$CONFIG_FILE" 2>/dev/null || return 1
}

# Save board configuration to file
save_config() {
    local serial=$1
    local target=$2
    local uart=$3
    local config=$4

    # Create config file if it doesn't exist
    if [ ! -f "$CONFIG_FILE" ]; then
        touch "$CONFIG_FILE"
        echo "# wolfBoot STM32 Board Configuration" >> "$CONFIG_FILE"
        echo "# Format: SERIAL:TARGET:UART:CONFIG" >> "$CONFIG_FILE"
        echo "" >> "$CONFIG_FILE"
    fi

    # Remove existing entry for this serial if present
    sed -i "/^${serial}:/d" "$CONFIG_FILE" 2>/dev/null || true

    # Add new entry
    echo "${serial}:${target}:${uart}:${config}" >> "$CONFIG_FILE"

    success "Saved board configuration: $target -> $uart"
}

# Get board info from config by serial number
get_board_info() {
    local serial=$1

    if [ ! -f "$CONFIG_FILE" ]; then
        return 1
    fi

    local line=$(grep "^${serial}:" "$CONFIG_FILE" 2>/dev/null | head -1)
    if [ -z "$line" ]; then
        return 1
    fi

    # Parse: SERIAL:TARGET:UART:CONFIG
    BOARD_SERIAL=$(echo "$line" | cut -d: -f1)
    BOARD_TARGET=$(echo "$line" | cut -d: -f2)
    BOARD_UART=$(echo "$line" | cut -d: -f3)
    BOARD_CONFIG=$(echo "$line" | cut -d: -f4)

    return 0
}

# Get board info by target name
get_board_by_target() {
    local target=$1

    if [ ! -f "$CONFIG_FILE" ]; then
        return 1
    fi

    local line=$(grep ":${target}:" "$CONFIG_FILE" 2>/dev/null | head -1)
    if [ -z "$line" ]; then
        return 1
    fi

    BOARD_SERIAL=$(echo "$line" | cut -d: -f1)
    BOARD_TARGET=$(echo "$line" | cut -d: -f2)
    BOARD_UART=$(echo "$line" | cut -d: -f3)
    BOARD_CONFIG=$(echo "$line" | cut -d: -f4)

    # Verify MCU type matches if st-info is available
    if command -v st-info >/dev/null 2>&1; then
        local st_info_output=$(st-info --probe 2>&1 | grep -E "(serial:|dev-type:)" || true)
        local current_serial=""
        local detected_mcu=""

        while IFS= read -r info_line; do
            if echo "$info_line" | grep -q "serial:"; then
                current_serial=$(echo "$info_line" | sed 's/.*serial:[[:space:]]*//' | tr -d '[:space:]')
            elif echo "$info_line" | grep -q "dev-type:" && [ "$current_serial" = "$BOARD_SERIAL" ]; then
                local dev_type=$(echo "$info_line" | sed 's/.*dev-type:[[:space:]]*//' | tr -d '[:space:]')
                case "$dev_type" in
                    *STM32C031xx*|*STM32C0*)
                        detected_mcu="stm32c0"
                        ;;
                    *STM32G07x*|*STM32G08x*|*STM32G0*)
                        detected_mcu="stm32g0"
                        ;;
                    *STM32WB*)
                        detected_mcu="stm32wb"
                        ;;
                    *STM32L4*)
                        detected_mcu="stm32l4"
                        ;;
                esac
                break
            fi
        done <<< "$st_info_output"

        if [ -n "$detected_mcu" ] && [ "$detected_mcu" != "$target" ]; then
            warning "MCU type mismatch: Board $BOARD_SERIAL is configured as '$target' but detected as '$detected_mcu'"
            warning "Please reconfigure this board using: $0 setup"
            return 1
        fi
    fi

    return 0
}

# Interactive board selection
select_board_interactive() {
    if ! detect_boards; then
        return 1
    fi

    local boards=("${DETECTED_BOARDS[@]}")
    local uarts=("${DETECTED_UARTS[@]}")

    echo ""
    info "Available boards:"
    echo ""

    # Get MCU types from detect_boards (if available)
    local -A mcu_types
    if command -v st-info >/dev/null 2>&1; then
        local st_info_output=$(st-info --probe 2>&1 | grep -E "(serial:|dev-type:)" || true)
        local current_serial=""
        while IFS= read -r line; do
            if echo "$line" | grep -q "serial:"; then
                current_serial=$(echo "$line" | sed 's/.*serial:[[:space:]]*//' | tr -d '[:space:]')
            elif echo "$line" | grep -q "dev-type:" && [ -n "$current_serial" ]; then
                local dev_type=$(echo "$line" | sed 's/.*dev-type:[[:space:]]*//' | tr -d '[:space:]')
                case "$dev_type" in
                    *STM32C031xx*|*STM32C0*)
                        mcu_types["$current_serial"]="stm32c0"
                        ;;
                    *STM32G07x*|*STM32G08x*|*STM32G0*)
                        mcu_types["$current_serial"]="stm32g0"
                        ;;
                    *STM32WB*)
                        mcu_types["$current_serial"]="stm32wb"
                        ;;
                    *STM32L4*)
                        mcu_types["$current_serial"]="stm32l4"
                        ;;
                esac
            fi
        done <<< "$st_info_output"
    fi

    local i=1
    local board_options=()
    for idx in "${!boards[@]}"; do
        local serial="${boards[$idx]}"
        local uart="${uarts[$idx]}"
        local detected_mcu="${mcu_types[$serial]:-}"

        # Try to get board info from config
        if get_board_info "$serial"; then
            local mcu_info=""
            if [ -n "$detected_mcu" ] && [ "$detected_mcu" != "$BOARD_TARGET" ]; then
                mcu_info=" ${YELLOW}(detected: $detected_mcu)${NC}"
            fi
            echo -e "  ${CYAN}[$i]${NC} $uart -> ${GREEN}$BOARD_TARGET${NC}${mcu_info} (Serial: ${serial:0:12}...)"
            board_options+=("$serial:$BOARD_TARGET:$uart:$BOARD_CONFIG")
        else
            local mcu_info=""
            if [ -n "$detected_mcu" ]; then
                mcu_info=" ${GREEN}(detected: $detected_mcu)${NC}"
            fi
            echo -e "  ${CYAN}[$i]${NC} $uart -> ${YELLOW}Unconfigured${NC}${mcu_info} (Serial: ${serial:0:12}...)"
            board_options+=("$serial:$detected_mcu:$uart:")
        fi
        ((i++))
    done

    echo ""
    echo -n "Select board [1-${#boards[@]}]: "
    read -r choice

    if ! [[ "$choice" =~ ^[0-9]+$ ]] || [ "$choice" -lt 1 ] || [ "$choice" -gt "${#boards[@]}" ]; then
        error "Invalid selection"
        return 1
    fi

    local selected="${board_options[$((choice-1))]}"
    BOARD_SERIAL=$(echo "$selected" | cut -d: -f1)
    BOARD_TARGET=$(echo "$selected" | cut -d: -f2)
    BOARD_UART=$(echo "$selected" | cut -d: -f3)
    BOARD_CONFIG=$(echo "$selected" | cut -d: -f4)

    # If unconfigured, prompt for configuration
    if [ -z "$BOARD_TARGET" ]; then
        echo ""
        warning "Board $BOARD_UART is not configured."
        echo "Available STM32 targets:"
        echo "  stm32c0, stm32g0, stm32wb, stm32l4, stm32f1, stm32f4, stm32f7, stm32h5, stm32h7"
        echo ""
        echo -n "Enter target name: "
        read -r BOARD_TARGET

        if [ -z "$BOARD_TARGET" ]; then
            error "Target name required"
            return 1
        fi

        # Default config file
        BOARD_CONFIG="config/examples/${BOARD_TARGET}.config"
        if [ ! -f "$REPO_ROOT/$BOARD_CONFIG" ]; then
            warning "Config file $BOARD_CONFIG not found, using default"
            BOARD_CONFIG=""
        fi

        # Save configuration
        save_config "$BOARD_SERIAL" "$BOARD_TARGET" "$BOARD_UART" "$BOARD_CONFIG"
    fi

    SELECTED_BOARD="$BOARD_TARGET"
    success "Selected board: $BOARD_TARGET ($BOARD_UART)"

    return 0
}

# List all detected and configured boards
list_boards() {
    if ! detect_boards; then
        return 1
    fi

    local boards=("${DETECTED_BOARDS[@]}")
    local uarts=("${DETECTED_UARTS[@]}")

    echo ""
    info "Detected STM32 boards:"
    echo ""

    for idx in "${!boards[@]}"; do
        local serial="${boards[$idx]}"
        local uart="${uarts[$idx]}"

        echo -e "  ${CYAN}UART:${NC} $uart"
        echo -e "  ${CYAN}Serial:${NC} $serial"

        if get_board_info "$serial"; then
            echo -e "  ${GREEN}Target:${NC} $BOARD_TARGET"
            if [ -n "$BOARD_CONFIG" ]; then
                echo -e "  ${GREEN}Config:${NC} $BOARD_CONFIG"
            fi
        else
            echo -e "  ${YELLOW}Status:${NC} Unconfigured"
        fi
        echo ""
    done
}

# Setup wizard for initial configuration
setup_boards() {
    info "STM32 Board Setup Wizard"
    echo ""

    if ! detect_boards; then
        return 1
    fi

    local boards=("${DETECTED_BOARDS[@]}")
    local uarts=("${DETECTED_UARTS[@]}")

    for idx in "${!boards[@]}"; do
        local serial="${boards[$idx]}"
        local uart="${uarts[$idx]}"

        echo ""
        info "Configuring board on $uart (Serial: ${serial:0:12}...)"

        # Check if already configured
        if get_board_info "$serial"; then
            echo -e "  Already configured as: ${GREEN}$BOARD_TARGET${NC}"
            echo -n "  Reconfigure? [y/N]: "
            read -r reconfigure
            if [ "$reconfigure" != "y" ] && [ "$reconfigure" != "Y" ]; then
                continue
            fi
        fi

        echo ""
        echo "Available STM32 targets:"
        echo "  stm32c0, stm32g0, stm32wb, stm32l4, stm32f1, stm32f4, stm32f7, stm32h5, stm32h7"
        echo ""
        echo -n "Enter target name: "
        read -r target

        if [ -z "$target" ]; then
            warning "Skipping $uart"
            continue
        fi

        # Default config file
        local config="config/examples/${target}.config"
        if [ ! -f "$REPO_ROOT/$config" ]; then
            warning "Config file $config not found"
            config=""
        fi

        # Save configuration
        save_config "$serial" "$target" "$uart" "$config"
    done

    success "Setup complete!"
}

# Build for selected board
build_board() {
    if [ -z "$BOARD_TARGET" ]; then
        error "No board selected"
        return 1
    fi

    info "Building for $BOARD_TARGET..."

    cd "$REPO_ROOT"

    local build_cmd="make clean && make TARGET=$BOARD_TARGET DEBUG_UART=1 DEBUG_SYMBOLS=1"

    if [ -n "$BOARD_CONFIG" ] && [ -f "$BOARD_CONFIG" ]; then
        build_cmd="cp $BOARD_CONFIG .config && $build_cmd"
    fi

    info "Build command: $build_cmd"
    eval "$build_cmd"

    if [ $? -eq 0 ]; then
        success "Build completed successfully"
        return 0
    else
        error "Build failed"
        return 1
    fi
}

# Flash board
flash_board() {
    if [ -z "$BOARD_TARGET" ]; then
        error "No board selected"
        return 1
    fi

    cd "$REPO_ROOT"

    # Check if factory.bin exists
    if [ ! -f "factory.bin" ]; then
        error "factory.bin not found. Build first?"
        return 1
    fi

    info "Flashing $BOARD_TARGET..."

    # Try st-flash first
    if command -v st-flash >/dev/null 2>&1; then
        info "Using st-flash..."
        # Note: st-flash may reset automatically, so --reset might not be needed
        # Remove --reset if board resets twice
        st-flash write factory.bin "$DEFAULT_FLASH_ADDR"
        if [ $? -eq 0 ]; then
            success "Flash completed successfully"
            return 0
        else
            error "st-flash failed"
        fi
    fi

    # Try stm32flash as fallback
    if command -v stm32flash >/dev/null 2>&1; then
        warning "st-flash not available, trying stm32flash..."
        # Note: stm32flash uses different addressing
        stm32flash -w factory.bin -v -g "$DEFAULT_FLASH_ADDR" "$BOARD_UART"
        if [ $? -eq 0 ]; then
            success "Flash completed successfully"
            return 0
        else
            error "stm32flash failed"
        fi
    fi

    error "No flash tool available (st-flash or stm32flash)"
    error "Install stlink package: sudo apt-get install stlink-tools"
    return 1
}

# Monitor serial output using background capture (like nxp-s32k142-flash.sh)
monitor_board() {
    local timeout=${1:-0}  # 0 = no timeout (interactive)
    local log_file="${2:-}"  # Optional log file

    if [ -z "$BOARD_UART" ]; then
        error "No board UART device selected"
        return 1
    fi

    if [ ! -e "$BOARD_UART" ]; then
        error "UART device $BOARD_UART not found"
        return 1
    fi

    # Configure UART
    stty -F "$BOARD_UART" "$DEFAULT_BAUD" raw -echo -hupcl cs8 -cstopb -parenb 2>/dev/null || {
        error "Failed to configure UART device $BOARD_UART"
        return 1
    }

    info "Starting UART capture on $BOARD_UART (baud: $DEFAULT_BAUD)"
    if [ -n "$log_file" ]; then
        info "Logging to: $log_file"
    fi
    if [ "$timeout" -gt 0 ]; then
        info "Will capture for $timeout seconds"
    else
        info "Press Ctrl+C to stop"
    fi
    echo ""

    # Cleanup function for background process
    local uart_pid=""
    cleanup_uart() {
        if [ -n "$uart_pid" ] && kill -0 "$uart_pid" 2>/dev/null; then
            kill "$uart_pid" 2>/dev/null || true
            wait "$uart_pid" 2>/dev/null || true
        fi
    }
    trap cleanup_uart EXIT INT TERM

    # Start background UART capture
    if [ -n "$log_file" ]; then
        # Capture to both terminal and file using tee (unbuffered for real-time output)
        if command -v stdbuf >/dev/null 2>&1; then
            stdbuf -o0 -e0 cat "$BOARD_UART" 2>/dev/null | stdbuf -o0 -e0 tee "$log_file" &
        else
            # Fallback to regular tee if stdbuf not available
            cat "$BOARD_UART" | tee "$log_file" &
        fi
        uart_pid=$!
    else
        # Capture to terminal only
        cat "$BOARD_UART" &
        uart_pid=$!
    fi

    if [ "$timeout" -gt 0 ]; then
        # Wait for timeout
        sleep "$timeout"
        cleanup_uart
        trap - EXIT INT TERM
        info "UART capture completed"
    else
        # Wait for user interrupt
        wait "$uart_pid" 2>/dev/null || true
        cleanup_uart
        trap - EXIT INT TERM
    fi
}

# Usage information
usage() {
    cat << EOF
Usage: $0 [COMMAND] [BOARD] [OPTIONS]

Commands:
  list                    List all detected boards
  select                  Interactive board selection
  setup                   Setup wizard for board configuration
  build [BOARD]           Build for board (with DEBUG_UART=1 DEBUG_SYMBOLS=1)
  flash [BOARD]           Flash factory.bin to board
  monitor [BOARD]          Open serial monitor
  [BOARD]                 Build + flash + monitor (default action)

Options:
  --skip-build            Skip build step
  --skip-flash            Skip flash step
  --skip-monitor          Skip monitor step
  --timeout SECS          Monitor timeout in seconds (default: interactive)
  --log FILE              Save UART output to file (also displays on terminal)
  --uart DEV              Override UART device
  --baud RATE             Override baud rate (default: $DEFAULT_BAUD)
  -h, --help              Show this help

Examples:
  $0 setup                # First time setup
  $0 list                 # List detected boards
  $0 select               # Select board interactively
  $0 stm32c0              # Build, flash, and monitor C0 board
  $0 build stm32g0        # Just build for G0
  $0 flash stm32c0        # Just flash C0
  $0 monitor stm32g0      # Just monitor G0
  $0 monitor stm32c0 --log uart.log  # Monitor and log to file
  $0 stm32c0 --skip-monitor  # Build and flash only

EOF
}

# Main function
main() {
    local command=""
    local board_arg=""
    local skip_build=0
    local skip_flash=0
    local skip_monitor=0
    local monitor_timeout=0
    local monitor_log_file=""

    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            list|select|setup|build|flash|monitor)
                command="$1"
                shift
                ;;
            --skip-build)
                skip_build=1
                shift
                ;;
            --skip-flash)
                skip_flash=1
                shift
                ;;
            --skip-monitor)
                skip_monitor=1
                shift
                ;;
            --timeout)
                monitor_timeout="$2"
                shift 2
                ;;
            --log)
                monitor_log_file="$2"
                shift 2
                ;;
            --uart)
                BOARD_UART="$2"
                shift 2
                ;;
            --baud)
                DEFAULT_BAUD="$2"
                shift 2
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            stm32*)
                board_arg="$1"
                shift
                ;;
            *)
                error "Unknown option: $1"
                usage
                exit 1
                ;;
        esac
    done

    # Load configuration (ignore if file doesn't exist)
    load_config || true

    # Handle commands
    case "$command" in
        list)
            list_boards
            exit 0
            ;;
        setup)
            setup_boards
            exit 0
            ;;
        select)
            select_board_interactive
            exit 0
            ;;
        build)
            if [ -n "$board_arg" ]; then
                if ! get_board_by_target "$board_arg"; then
                    error "Board $board_arg not configured. Run 'setup' first."
                    exit 1
                fi
            elif [ -z "$BOARD_TARGET" ]; then
                if ! select_board_interactive; then
                    exit 1
                fi
            fi
            build_board
            exit $?
            ;;
        flash)
            if [ -n "$board_arg" ]; then
                if ! get_board_by_target "$board_arg"; then
                    error "Board $board_arg not configured. Run 'setup' first."
                    exit 1
                fi
            elif [ -z "$BOARD_TARGET" ]; then
                if ! select_board_interactive; then
                    exit 1
                fi
            fi
            flash_board
            exit $?
            ;;
        monitor)
            if [ -n "$board_arg" ]; then
                if ! get_board_by_target "$board_arg"; then
                    error "Board $board_arg not configured. Run 'setup' first."
                    exit 1
                fi
            elif [ -z "$BOARD_UART" ]; then
                if ! select_board_interactive; then
                    exit 1
                fi
            fi
            monitor_board "$monitor_timeout" "$monitor_log_file"
            exit $?
            ;;
        "")
            # Default: build + flash + monitor
            if [ -n "$board_arg" ]; then
                if ! get_board_by_target "$board_arg"; then
                    error "Board $board_arg not configured. Run 'setup' first."
                    exit 1
                fi
            elif [ -z "$BOARD_TARGET" ]; then
                if ! select_board_interactive; then
                    exit 1
                fi
            fi

            # Start UART capture BEFORE building/flashing (to catch boot messages)
            local uart_pid=""
            cleanup_uart_capture() {
                if [ -n "$uart_pid" ] && kill -0 "$uart_pid" 2>/dev/null; then
                    kill "$uart_pid" 2>/dev/null || true
                    wait "$uart_pid" 2>/dev/null || true
                fi
            }
            trap cleanup_uart_capture EXIT INT TERM

            if [ $skip_monitor -eq 0 ] && [ -n "$BOARD_UART" ] && [ -e "$BOARD_UART" ]; then
                if [ "$monitor_timeout" -eq 0 ]; then
                    monitor_timeout=10  # Default 10 second timeout
                fi
                info "Starting UART capture before flash..."
                stty -F "$BOARD_UART" "$DEFAULT_BAUD" raw -echo -hupcl cs8 -cstopb -parenb 2>/dev/null || true
                if [ -n "$monitor_log_file" ]; then
                    if command -v stdbuf >/dev/null 2>&1; then
                        stdbuf -o0 -e0 cat "$BOARD_UART" 2>/dev/null | stdbuf -o0 -e0 tee "$monitor_log_file" &
                    else
                        cat "$BOARD_UART" | tee "$monitor_log_file" &
                    fi
                else
                    cat "$BOARD_UART" &
                fi
                uart_pid=$!
                sleep 0.5  # Brief delay to ensure capture is ready
            fi

            # Build
            if [ $skip_build -eq 0 ]; then
                if ! build_board; then
                    [ -n "$uart_pid" ] && kill "$uart_pid" 2>/dev/null || true
                    exit 1
                fi
            fi

            # Flash
            if [ $skip_flash -eq 0 ]; then
                if ! flash_board; then
                    [ -n "$uart_pid" ] && kill "$uart_pid" 2>/dev/null || true
                    exit 1
                fi
            fi

            # Monitor (wait for timeout, then cleanup)
            if [ $skip_monitor -eq 0 ] && [ -n "$uart_pid" ]; then
                info "Waiting for UART output (timeout: ${monitor_timeout}s)..."
                sleep "$monitor_timeout"
                kill "$uart_pid" 2>/dev/null || true
                wait "$uart_pid" 2>/dev/null || true
            elif [ $skip_monitor -eq 0 ]; then
                # Fallback to regular monitor if UART wasn't started earlier
                if [ "$monitor_timeout" -eq 0 ]; then
                    monitor_timeout=10
                fi
                monitor_board "$monitor_timeout" "$monitor_log_file"
            fi
            ;;
        *)
            error "Unknown command: $command"
            usage
            exit 1
            ;;
    esac
}

# Run main function
main "$@"
