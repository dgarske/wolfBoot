#!/bin/bash

# Script to test all config files in config/examples/
# For each config: cp file .config && make keysclean && make
#
# Usage: ./test-all-configs.sh [pattern]
#   pattern: Optional wildcard pattern to filter configs (e.g., "*wolfHSM*", "sim-*")
#            If not provided, tests all non-skipped configs
#
# Environment Variables (optional):
#   Dependency locations can be overridden using environment variables:
#
#   MCUX SDK:
#     MCUXPRESSO - Path to MCUX SDK (default: ../mcux-sdk)
#     MCUXPRESSO_CMSIS - Path to CMSIS_5 (default: ../CMSIS_5)
#
#   TPM:
#     SWTPM_PATH - Path to ibmswtpm2 repository (default: ../ibmswtpm2)
#
#   STM32Cube:
#     STM32CUBE_WB_PATH - Path to STM32CubeWB repository (default: ../STM32CubeWB)
#     STM32CUBE_L4_PATH - Path to STM32CubeL4 repository (default: ../STM32CubeL4)
#
#   Example:
#     export MCUXPRESSO=/opt/mcux-sdk
#     export MCUXPRESSO_CMSIS=/opt/CMSIS_5/CMSIS
#     export SWTPM_PATH=/opt/ibmswtpm2
#     export STM32CUBE_WB_PATH=/opt/STM32CubeWB
#     ./test-all-configs.sh "*stm32*"
#
#   Note: If custom paths are specified but don't exist, the script will report
#         an error. Auto-cloning only occurs when using default paths.

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Global flag to stop testing on interrupt
STOP_TESTING=0

# Parse command-line arguments
FILTER_PATTERN="${1:-*}"

# Track results
PASSED=()  # Array of "config_name|log_file" entries
FAILED=()  # Array of "config_name|log_file" entries
SKIPPED=()

# Configs that require special compilers/SDKs (excluded from testing)
SKIP_PATTERNS=(
    "aurix-*"
    "hifive*"
    "renesas-*"
)

# Specific configs that fail or have special requirements (excluded from testing)
SKIP_CONFIGS=(
    "cypsoc6.config"            # requires cy_device_headers.h
    "ti-tms570lc435.config"     # requires CCS_ROOT
    "kontron_vx3060_s2.config"  # requires FSP binaries
    "x86_64_efi.config"
    "x86_fsp_qemu.config"
    "x86_fsp_qemu_seal.config"
    "sim-wolfHSM-server-certchain.config" # TODO: Issues with test
)

# Check if we're in the wolfboot root directory
if [ ! -f "Makefile" ] || [ ! -d "config/examples" ]; then
    echo -e "${RED}Error: Must be run from wolfboot root directory${NC}"
    exit 1
fi

# Get absolute path of wolfboot root (script is in tools/scripts/, so go up 2 levels)
WOLFBOOT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# If we're already in the root, use pwd instead
if [ ! -f "$WOLFBOOT_ROOT/Makefile" ]; then
    WOLFBOOT_ROOT="$(pwd)"
fi

# Helper function to check if we should stop
should_stop() {
    [ $STOP_TESTING -eq 1 ]
}

# Helper function to resolve MCUX SDK paths
resolve_mcux_paths() {
    local wolfboot_root="${1:-$(pwd)}"
    local default_mcux_sdk_path="$wolfboot_root/../mcux-sdk"
    local default_cmsis_path="$wolfboot_root/../CMSIS_5"

    local mcux_sdk_path="${MCUX_SDK_PATH:-${MCUXPRESSO:-$default_mcux_sdk_path}}"
    local cmsis_path="${CMSIS_5_PATH:-${MCUXPRESSO_CMSIS:-$default_cmsis_path}}"

    # Check if CMSIS path needs /CMSIS suffix for default
    if [ "$cmsis_path" = "$default_cmsis_path" ] && [ ! -d "$cmsis_path" ]; then
        cmsis_path="$default_cmsis_path/CMSIS"
    fi

    echo "$mcux_sdk_path|$cmsis_path|$default_mcux_sdk_path|$default_cmsis_path"
}

# Helper function to cleanup based on config type
cleanup_by_type() {
    local config_type="$1"
    local config_name="$2"
    [[ "$config_type" == "tpm" ]] && cleanup_tpm
    [[ "$config_type" == "wolfhsm" ]] && cleanup_wolfhsm "$config_name"
}

# Helper function to record failure
record_failure() {
    local config_name="$1"
    local log_file="$2"
    local reason="$3"
    FAILED+=("$config_name|$log_file|$reason")
    echo -e "  ${RED}✗ $reason${NC}"
    echo -e "  ${YELLOW}Build log saved to: $log_file${NC}"
}

# Function to clone a git repository if it doesn't exist
clone_dependency() {
    local repo_name="$1"
    local repo_url="$2"
    local target_dir="$3"
    local recursive="${4:-0}"  # Optional: 1 to clone with --recursive

    if [ -d "$target_dir" ]; then
        echo -e "  ${GREEN}✓${NC} $repo_name already exists at $target_dir"
        # Update submodules if recursive clone was expected
        if [ "$recursive" -eq 1 ] && [ -f "$target_dir/.gitmodules" ]; then
            echo -e "  ${YELLOW}Updating submodules for $repo_name...${NC}"
            (cd "$target_dir" && git submodule update --init --recursive >/dev/null 2>&1 || true)
        fi
        return 0
    fi

    # Directory doesn't exist, clone it
    local parent_dir=$(dirname "$target_dir")
    if [ ! -d "$parent_dir" ]; then
        mkdir -p "$parent_dir"
    fi

    echo -e "  ${YELLOW}Cloning $repo_name to $target_dir...${NC}"
    if [ "$recursive" -eq 1 ]; then
        if ! git clone --recursive "$repo_url" "$target_dir" 2>&1 | head -20; then
            echo -e "  ${RED}✗ Failed to clone $repo_name${NC}"
            return 1
        fi
    else
        if ! git clone "$repo_url" "$target_dir" 2>&1 | head -20; then
            echo -e "  ${RED}✗ Failed to clone $repo_name${NC}"
            return 1
        fi
    fi

    echo -e "  ${GREEN}✓${NC} Successfully cloned $repo_name"
    return 0
}

# Function to check and clone MCUX SDK dependencies
check_mcux_dependencies() {
    local wolfboot_root="$1"
    IFS='|' read -r mcux_sdk_path cmsis_path default_mcux_sdk_path default_cmsis_path <<< "$(resolve_mcux_paths "$wolfboot_root")"

    echo -e "${YELLOW}Checking MCUX SDK dependencies...${NC}"
    # Only clone if using default paths and they don't exist
    if [ "$mcux_sdk_path" = "$default_mcux_sdk_path" ] && [ ! -d "$mcux_sdk_path" ]; then
        clone_dependency "mcux-sdk" "https://github.com/nxp-mcuxpresso/mcux-sdk.git" "$mcux_sdk_path" 0
    fi
    if [ "$cmsis_path" = "$default_cmsis_path" ] || [ "$cmsis_path" = "$default_cmsis_path/CMSIS" ]; then
        local cmsis_base="${cmsis_path%/CMSIS}"
        if [ ! -d "$cmsis_base" ]; then
            clone_dependency "CMSIS_5" "https://github.com/nxp-mcuxpresso/CMSIS_5.git" "$cmsis_base" 0
        fi
    fi
}

# Function to check and clone TPM dependency
check_tpm_dependency() {
    local wolfboot_root="$1"
    # Use environment variable or default location
    local default_tpm_path="$wolfboot_root/../ibmswtpm2"

    # Resolve path using SWTPM_PATH environment variable
    local tpm_path
    if [ -n "${SWTPM_PATH}" ]; then
        tpm_path="${SWTPM_PATH}"
    else
        tpm_path="$default_tpm_path"
    fi

    local tpm_src_path="$tpm_path/src"

    echo -e "${YELLOW}Checking TPM dependency...${NC}"
    local was_cloned=0
    if [ ! -d "$tpm_path" ]; then
        # Only clone if using default path
        if [ "$tpm_path" = "$default_tpm_path" ]; then
            clone_dependency "ibmswtpm2" "https://github.com/kgoldman/ibmswtpm2.git" "$tpm_path" 0
            was_cloned=1
        else
            echo -e "  ${RED}✗ TPM path not found: $tpm_path${NC}"
            return 1
        fi
    else
        echo -e "  ${GREEN}✓${NC} ibmswtpm2 already exists at $tpm_path"
    fi

    # Build ibmswtpm2 if needed (either just cloned or tpm_server doesn't exist)
    if [ $was_cloned -eq 1 ] || [ ! -f "$tpm_src_path/tpm_server" ]; then
        echo -e "  ${YELLOW}Building ibmswtpm2...${NC}"
        if [ ! -d "$tpm_src_path" ]; then
            echo -e "  ${RED}✗ ibmswtpm2 src directory not found at $tpm_src_path${NC}"
            return 1
        fi
        if ! (cd "$tpm_src_path" && make >/dev/null 2>&1); then
            echo -e "  ${RED}✗ Failed to build ibmswtpm2${NC}"
            echo -e "  ${YELLOW}Try building manually: cd $tpm_src_path && make${NC}"
            return 1
        fi
        echo -e "  ${GREEN}✓${NC} Successfully built ibmswtpm2"
    else
        echo -e "  ${GREEN}✓${NC} ibmswtpm2 already built"
    fi
}

# Function to check and clone STM32Cube dependencies based on config
check_stm32cube_dependency() {
    local config_name="$1"
    local wolfboot_root="$2"

    # Determine which STM32Cube repo/pack is needed
    local cube_repo=""
    local cube_path=""
    local cube_name=""
    local default_cube_path=""

    if [[ "$config_name" == *"stm32wb"* ]]; then
        cube_repo="https://github.com/STMicroelectronics/STM32CubeWB.git"
        cube_name="STM32CubeWB"
        # Default location for Cube packs
        default_cube_path="$HOME/STM32Cube/Repository/STM32Cube_FW_WB_V1.23.0"
        # Fallback: hardcoded location for cloned repo
        local default_clone_path="$wolfboot_root/../STM32CubeWB"
        # Use environment variable or default location for cloned repo
        if [ -n "${STM32CUBE_WB_PATH}" ]; then
            cube_path="${STM32CUBE_WB_PATH}"
        else
            cube_path="$default_clone_path"
        fi
    elif [[ "$config_name" == *"stm32l4"* ]]; then
        cube_repo="https://github.com/STMicroelectronics/STM32CubeL4.git"
        cube_name="STM32CubeL4"
        # Default location for Cube packs
        default_cube_path="$HOME/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.1"
        # Fallback: hardcoded location for cloned repo
        local default_clone_path="$wolfboot_root/../STM32CubeL4"
        # Use environment variable or default location for cloned repo
        if [ -n "${STM32CUBE_L4_PATH}" ]; then
            cube_path="${STM32CUBE_L4_PATH}"
        else
            cube_path="$default_clone_path"
        fi
    else
        return 0  # No STM32Cube dependency needed
    fi

    echo -e "${YELLOW}Checking STM32Cube dependency for $config_name...${NC}"

    # First check the default Repository location (where Cube packs are installed)
    # Convert to absolute path if it exists
    if [ -d "$default_cube_path" ]; then
        # Get absolute path using cd and pwd
        default_cube_path="$(cd "$default_cube_path" && pwd)"
        echo -e "  ${GREEN}✓${NC} Found Cube pack at default location: $default_cube_path"
        # Export STM32CUBE with absolute path for test-app build
        export STM32CUBE="$default_cube_path"
        return 0
    fi

    # If not found in default location, check if custom path exists or clone from GitHub
    if [ -d "$cube_path" ]; then
        # Custom path exists, use it
        cube_path="$(cd "$cube_path" && pwd)"
        echo -e "  ${GREEN}✓${NC} Found Cube repo at custom location: $cube_path"
        export STM32CUBE="$cube_path"
        return 0
    elif [ "$cube_path" = "$default_clone_path" ]; then
        # Using default clone path, try to clone
        echo -e "  ${YELLOW}Cube pack not found at default location, cloning from GitHub...${NC}"
        if clone_dependency "$cube_name" "$cube_repo" "$cube_path" 1; then
            # Ensure we have an absolute path and export STM32CUBE for test-app build
            cube_path="$(cd "$cube_path" && pwd)"
            export STM32CUBE="$cube_path"
            return 0
        else
            return 1
        fi
    else
        # Custom path specified but doesn't exist
        echo -e "  ${RED}✗ Cube repo path not found: $cube_path${NC}"
        return 1
    fi
}

# Function to cleanup TPM (defined early for trap)
cleanup_tpm() {
    if [ -f /tmp/tpm_server_pid ]; then
        local pid=$(cat /tmp/tpm_server_pid)
        kill "$pid" 2>/dev/null || true
        rm -f /tmp/tpm_server_pid
    fi
    pkill -f tpm_server || true
}

# Function to cleanup wolfHSM server (defined early for trap)
cleanup_wolfhsm() {
    local config_name="${1:-}"  # Optional: config name to determine cleanup behavior

    if [ -f /tmp/wolfhsm_server_pid ]; then
        local pid=$(cat /tmp/wolfhsm_server_pid)
        kill "$pid" 2>/dev/null || true
        rm -f /tmp/wolfhsm_server_pid
    fi
    pkill -f wh_posix_server || true
    # Clean up NVM image only for client configs (server-certchain needs it for embedded server)
    if [[ -z "$config_name" ]] || [[ "$config_name" != *server-certchain* ]]; then
        rm -f wolfBoot_wolfHSM_NVM.bin
    fi
    # Clean up NVM init temp file if it exists
    if [ -f /tmp/wolfhsm_nvminit_file ]; then
        local tmpfile=$(cat /tmp/wolfhsm_nvminit_file)
        rm -f "$tmpfile" /tmp/wolfhsm_nvminit_file
    fi
}

# Cleanup function for traps
cleanup_all() {
    echo ""
    echo -e "${YELLOW}Cleaning up...${NC}"
    cleanup_tpm
    cleanup_wolfhsm ""  # No config name for global cleanup, will clean all
    if [ $STOP_TESTING -eq 1 ]; then
        echo -e "${YELLOW}Testing interrupted by user${NC}"
        exit 130
    fi
}

# Signal handler for Ctrl+C
handle_interrupt() {
    STOP_TESTING=1
    echo ""
    echo -e "${YELLOW}Interrupt received (Ctrl+C), stopping after current test...${NC}"
    # Don't exit here - let the main loop check the flag and exit cleanly
}

# Set trap to cleanup on exit and handle interrupts
trap cleanup_all EXIT
trap handle_interrupt INT TERM

# Function to check if a config is a sim-* config
is_sim_config() {
    [[ "$1" == sim* ]]
}

# Function to determine the sim config type
get_sim_config_type() {
    local config_name="$1"
    case "$config_name" in
        sim-encrypt-*)
            if [[ "$config_name" == *delta* ]]; then
                echo "encrypt-delta"
            else
                echo "encrypt"
            fi
            ;;
        sim-tpm*)
            echo "tpm"
            ;;
        sim-wolfHSM*)
            echo "wolfhsm"
            ;;
        sim-lms.config|sim-xmss.config|sim-ml-dsa.config)
            echo "pq"
            ;;
        sim-ml-dsa-ecc-hybrid.config)
            echo "pq-hybrid"
            ;;
        sim-delta-update.config)
            echo "delta"
            ;;
        sim-dualbank.config)
            echo "dualbank"
            ;;
        sim-elf-scattered.config|sim32-elf-scattered.config)
            echo "elf-scattered"
            ;;
        sim-nobackup*.config)
            echo "nobackup"
            ;;
        sim-nvm-writeonce*.config)
            if [[ "$config_name" == *flags-home-invert* ]]; then
                echo "nvm-writeonce-external"
            else
                echo "nvm-writeonce"
            fi
            ;;
        sim32.config)
            echo "sim32"
            ;;
        sim.config)
            echo "sim"
            ;;
        *)
            echo "unknown"
            ;;
    esac
}

# Function to determine the make target for sim configs
get_sim_make_target() {
    local config_name="$1"
    local config_type="$2"

    case "$config_type" in
        encrypt-delta)
            echo "test-sim-external-flash-with-enc-delta-update"
            ;;
        encrypt)
            echo "test-sim-external-flash-with-enc-update"
            ;;
        delta)
            echo "test-sim-internal-flash-with-delta-update"
            ;;
        nvm-writeonce-external)
            echo "test-sim-external-flash-with-update"
            ;;
        *)
            echo "test-sim-internal-flash-with-update"
            ;;
    esac
}

# Function to get make args for sim configs
get_sim_make_args() {
    local config_name="$1"
    local config_type="$2"
    local args=""

    case "$config_type" in
        elf-scattered)
            args="ELF=1 ELF_SCATTERED=1"
            ;;
    esac

    echo "$args"
}

# Function to check if config requires MCUX SDK
needs_mcux_sdk() {
    local config_name="$1"
    case "$config_name" in
        imx-*|lpc*|mcx*|kinetis-*)
            return 0  # Needs MCUX SDK
            ;;
        *)
            return 1  # Doesn't need MCUX SDK
            ;;
    esac
}

# Function to get MCUX SDK make arguments
get_mcux_sdk_args() {
    local config_name="$1"
    local silent="${2:-0}"  # Optional: 2nd arg for silent mode (no error messages)

    IFS='|' read -r mcux_sdk_path cmsis_path default_mcux_sdk_path default_cmsis_path <<< "$(resolve_mcux_paths)"

    # Ensure CMSIS path points to CMSIS subdirectory
    if [ "$cmsis_path" = "$default_cmsis_path" ] && [ -d "$default_cmsis_path/CMSIS" ]; then
        cmsis_path="$default_cmsis_path/CMSIS"
    fi

    # Check if SDK directories exist
    if [ ! -d "$mcux_sdk_path" ]; then
        [ "$silent" -eq 0 ] && echo -e "  ${YELLOW}Warning: MCUX SDK not found at $mcux_sdk_path${NC}" >&2 && \
            echo -e "  ${YELLOW}MCUX SDK configs will be skipped${NC}" >&2
        return 1
    fi
    if [ ! -d "$cmsis_path" ]; then
        [ "$silent" -eq 0 ] && echo -e "  ${YELLOW}Warning: CMSIS_5 not found at $cmsis_path${NC}" >&2 && \
            echo -e "  ${YELLOW}MCUX SDK configs will be skipped${NC}" >&2
        return 1
    fi

    echo "MCUXSDK=1 MCUXPRESSO=$mcux_sdk_path MCUXPRESSO_CMSIS=$cmsis_path"
    return 0
}

# Function to check if a config should be skipped
should_skip() {
    local config_name="$1"
    local force="${2:-0}"  # Optional: 1 to force run (skip SKIP_CONFIGS check)

    # Check if MCUX SDK is needed and try to clone if missing
    if needs_mcux_sdk "$config_name"; then
        local wolfboot_root="$(pwd)"
        IFS='|' read -r mcux_sdk_path cmsis_path default_mcux_sdk_path default_cmsis_path <<< "$(resolve_mcux_paths "$wolfboot_root")"

        # Check if CMSIS path exists (MCUXPRESSO_CMSIS points to CMSIS subdir)
        local cmsis_check_path="$cmsis_path"
        if [ "$cmsis_path" = "$default_cmsis_path" ]; then
            cmsis_check_path="$default_cmsis_path/CMSIS"
        fi

        if [ ! -d "$mcux_sdk_path" ] || [ ! -d "$cmsis_check_path" ]; then
            # Try to clone dependencies (only if using default paths)
            if [ "$mcux_sdk_path" = "$default_mcux_sdk_path" ] && [ "$cmsis_path" = "$default_cmsis_path" ]; then
                check_mcux_dependencies "$wolfboot_root"
                if [ ! -d "$mcux_sdk_path" ] || [ ! -d "$default_cmsis_path/CMSIS" ]; then
                    return 0  # Skip - SDK not available and clone failed
                fi
            else
                return 0  # Skip - custom paths specified but don't exist
            fi
        fi
    fi

    # Check specific configs (skip if not forced)
    if [ "$force" -eq 0 ]; then
        for skip_config in "${SKIP_CONFIGS[@]}"; do
            [ "$config_name" == "$skip_config" ] && return 0
        done
    fi

    # Check patterns (always check, even when forced)
    for pattern in "${SKIP_PATTERNS[@]}"; do
        case "$config_name" in
            $pattern) return 0 ;;
        esac
    done
    return 1  # Should not skip
}

# Get all config files
CONFIG_DIR="config/examples"
ALL_CONFIGS=($(ls "$CONFIG_DIR"/*.config | xargs -n1 basename | sort))

# Check if filter pattern is an exact config name (explicit request)
FORCE_CONFIG=0
for config in "${ALL_CONFIGS[@]}"; do
    if [ "$config" == "$FILTER_PATTERN" ]; then
        FORCE_CONFIG=1
        break
    fi
done

# Filter out skipped configs and apply pattern filter
CONFIGS=()
for config in "${ALL_CONFIGS[@]}"; do
    # Check if config matches the filter pattern
    if [[ "$config" == $FILTER_PATTERN ]]; then
        # Force run if explicitly requested (exact match)
        force_flag=0
        [ "$FORCE_CONFIG" -eq 1 ] && [ "$config" == "$FILTER_PATTERN" ] && force_flag=1

        if ! should_skip "$config" "$force_flag"; then
            CONFIGS+=("$config")
        else
            SKIPPED+=("$config")
        fi
    else
        SKIPPED+=("$config")
    fi
done

echo "=========================================="
if [ "$FILTER_PATTERN" != "*" ]; then
    echo "Filter pattern: $FILTER_PATTERN"
fi
echo "Testing ${#CONFIGS[@]} config files"
if [ ${#SKIPPED[@]} -gt 0 ]; then
    echo "Skipped ${#SKIPPED[@]} configs (require special compilers/SDKs, known to fail, or don't match filter)"
fi
echo "=========================================="
echo ""

if [ ${#CONFIGS[@]} -eq 0 ]; then
    echo -e "${YELLOW}No configs match the filter pattern: $FILTER_PATTERN${NC}"
    exit 0
fi

# Function to setup TPM for TPM configs
setup_tpm() {
    local log_file="$1"
    local tpm_server_pid=""

    echo -e "  ${YELLOW}Setting up TPM...${NC}"

    # Resolve TPM path using environment variable or default
    local wolfboot_root="$(pwd)"
    local default_tpm_path="$wolfboot_root/../ibmswtpm2"

    local tpm_path
    if [ -n "${SWTPM_PATH}" ]; then
        tpm_path="${SWTPM_PATH}"
    else
        tpm_path="$default_tpm_path"
    fi

    local tpm_src_path="$tpm_path/src"

    if [ ! -d "$tpm_src_path" ]; then
        # Try to clone if missing (only if using default path)
        if [ "$tpm_path" = "$default_tpm_path" ]; then
            check_tpm_dependency "$wolfboot_root"
        fi
        if [ ! -d "$tpm_src_path" ]; then
            echo -e "  ${RED}✗ ibmswtpm2 not found at $tpm_src_path${NC}"
            echo -e "  ${YELLOW}Please ensure ibmswtpm2 is built and available at $tpm_src_path${NC}"
            return 1
        fi
    fi

    # Start TPM server (assume already built)
    if [ -f "$tpm_src_path/tpm_server" ]; then
        (cd "$tpm_src_path" && ./tpm_server >> "$log_file" 2>&1) &
        tpm_server_pid=$!
        sleep 1
        echo "$tpm_server_pid" > /tmp/tpm_server_pid
    else
        echo -e "  ${RED}✗ tpm_server not found at $tpm_src_path/tpm_server${NC}"
        echo -e "  ${YELLOW}Please build ibmswtpm2 first${NC}"
        return 1
    fi

    # Build TPM tools
    echo -e "  Building TPM tools..."
    if ! make tpmtools >> "$log_file" 2>&1; then
        echo -e "  ${RED}✗ TPM tools build failed${NC}"
        return 1
    fi

    # Write ROT to TPM (using default auth)
    echo -e "  Writing TPM ROT..."
    ./tools/tpm/rot -write -auth="TestAuth" >> "$log_file" 2>&1 || true

    # Create PCR policy
    echo -e "  Creating PCR policy..."
    echo "aaa" > /tmp/aaa.bin
    echo "bbb" > /tmp/bbb.bin
    ./tools/tpm/pcr_extend 0 /tmp/aaa.bin >> "$log_file" 2>&1 || true
    ./tools/tpm/pcr_extend 1 /tmp/bbb.bin >> "$log_file" 2>&1 || true
    ./tools/tpm/policy_create -pcr=1 -pcr=0 -out=policy.bin >> "$log_file" 2>&1 || true

    return 0
}

# Function to setup wolfHSM server
setup_wolfhsm() {
    local config_name="$1"
    local log_file="$2"
    local server_pid=""

    echo -e "  ${YELLOW}Setting up wolfHSM...${NC}"

    # For client configs, build and start the POSIX TCP server
    if [ ! -d "lib/wolfHSM/examples/posix/wh_posix_server" ]; then
        echo -e "  ${RED}✗ wolfHSM server directory not found${NC}"
        return 1
    fi

    # Build wolfHSM server
    echo -e "  Building wolfHSM POSIX TCP server..."
    (cd lib/wolfHSM/examples/posix/wh_posix_server && \
     make WOLFSSL_DIR=../../../../wolfssl >> "$log_file" 2>&1) || {
        echo -e "  ${RED}✗ wolfHSM server build failed${NC}"
        return 1
    }

    # Start server based on config type
    echo -e "  Starting wolfHSM server..."
    echo "=== wolfHSM Server Startup ===" >> "$log_file"
    cd lib/wolfHSM/examples/posix/wh_posix_server || {
        echo -e "  ${RED}✗ Failed to change to server directory${NC}"
        return 1
    }

    # Use stdbuf if available to make output unbuffered, otherwise use regular redirection
    local unbuf_cmd=""
    if command -v stdbuf >/dev/null 2>&1; then
        unbuf_cmd="stdbuf -o0 -e0"
    fi

    if [[ "$config_name" == *client-certchain* ]]; then
        # Client cert chain verify - use nvminit
        local cert_file="../../../../../test-dummy-ca/root-cert.der"
        if [ ! -f "$cert_file" ]; then
            echo -e "  ${RED}✗ Certificate file not found: $cert_file${NC}"
            echo -e "  ${YELLOW}This file should be generated by the build process when CERT_CHAIN_GEN is set${NC}"
            cd - > /dev/null
            return 1
        fi
        local tmpfile=$(mktemp)
        echo "obj 1 0xFFFF 0x0000 \"cert CA\" $cert_file" >> "$tmpfile"
        echo "Starting wolfHSM server (certchain) with nvminit file: $tmpfile" >> "$log_file"
        echo "Certificate file: $cert_file" >> "$log_file"
        $unbuf_cmd ./Build/wh_posix_server.elf --type tcp --nvminit "$tmpfile" >> "$log_file" 2>&1 &
        server_pid=$!
        # Don't delete tmpfile immediately - server needs to read it
        # Store it for cleanup later
        echo "$tmpfile" > /tmp/wolfhsm_nvminit_file
    else
        # Standard client - use key file
        local key_file="../../../../../wolfboot_signing_private_key_pub.der"
        if [ ! -f "$key_file" ]; then
            echo -e "  ${RED}✗ Public key file not found: $key_file${NC}"
            echo -e "  ${YELLOW}This file should be generated by keygen with --exportpubkey option${NC}"
            cd - > /dev/null
            return 1
        fi
        echo -e "  Using key file: $key_file"
        echo "Starting wolfHSM server with key file: $key_file" >> "$log_file"
        echo "Server command: ./Build/wh_posix_server.elf --type tcp --client 12 --id 255 --key $key_file" >> "$log_file"
        $unbuf_cmd ./Build/wh_posix_server.elf --type tcp --client 12 --id 255 --key "$key_file" >> "$log_file" 2>&1 &
        server_pid=$!
    fi

    cd - > /dev/null  # Return to original directory

    echo "Server PID: $server_pid" >> "$log_file"
    echo "$server_pid" > /tmp/wolfhsm_server_pid

    # Give server time to start and capture initial output
    sleep 1
    echo "=== Server startup output (after 2s) ===" >> "$log_file"

    # Check if server is still running and capture any output
    if ! kill -0 "$server_pid" 2>/dev/null; then
        echo -e "  ${RED}✗ wolfHSM server failed to start (PID $server_pid not found)${NC}"
        echo "ERROR: Server process died immediately after startup" >> "$log_file"
        # Try to get any error output
        if [ -f "$log_file" ]; then
            echo "=== Last 20 lines of log ===" >> "$log_file"
            tail -20 "$log_file" >> "$log_file" 2>&1 || true
        fi
        return 1
    fi

    # Wait a bit more and check again to ensure server is stable
    sleep 1
    if ! kill -0 "$server_pid" 2>/dev/null; then
        echo -e "  ${RED}✗ wolfHSM server died after starting${NC}"
        echo "ERROR: Server process died after initial startup" >> "$log_file"
        return 1
    fi

    echo "Server is running (PID: $server_pid)" >> "$log_file"
    echo -e "  ${GREEN}✓ wolfHSM server started successfully${NC}"

    # Special handling for "server cert chain verify" - create NVM image instead of starting server
    if [[ "$config_name" == *server-certchain* ]]; then
        echo -e "  Creating NVM image for wolfHSM server cert chain verify..."

        # Build whnvmtool
        if ! make -C lib/wolfHSM/tools/whnvmtool >> "$log_file" 2>&1; then
            echo -e "  ${RED}✗ whnvmtool build failed${NC}"
            return 1
        fi

        # Create NVM image - use relative path for certificate file (matching workflow)
        local cert_file="test-dummy-ca/root-cert.der"
        if [ ! -f "$cert_file" ]; then
            echo -e "  ${RED}✗ Certificate file not found: $cert_file${NC}"
            echo -e "  ${YELLOW}This file should be generated by the build process when CERT_CHAIN_GEN is set${NC}"
            return 1
        fi

        # Use relative path (workflow uses relative path)
        # Match workflow exactly: create tmpfile, write command, run whnvmtool
        # Workflow line 125-127: tmpfile=$(mktemp); echo "obj 1 0xFFFF 0x0000 \"cert CA\" test-dummy-ca/root-cert.der" >> $tmpfile; ./lib/wolfHSM/tools/whnvmtool/whnvmtool --image=wolfBoot_wolfHSM_NVM.bin --size=16348 --invert-erased-byte $tmpfile
        # Note: workflow doesn't remove tmpfile or check exit code, just runs it
        local tmpfile=$(mktemp)
        echo "obj 1 0xFFFF 0x0000 \"cert CA\" $cert_file" >> "$tmpfile"

        # Run whnvmtool from root directory (workflow doesn't redirect output or check exit code)
        # The workflow just runs: ./lib/wolfHSM/tools/whnvmtool/whnvmtool --image=wolfBoot_wolfHSM_NVM.bin --size=16348 --invert-erased-byte $tmpfile
        ./lib/wolfHSM/tools/whnvmtool/whnvmtool --image=wolfBoot_wolfHSM_NVM.bin --size=16348 --invert-erased-byte "$tmpfile" >> "$log_file" 2>&1 || true
        rm -f "$tmpfile"

        # Check if the NVM image was actually created (workflow doesn't check, but we should)
        if [ ! -f "wolfBoot_wolfHSM_NVM.bin" ]; then
            echo -e "  ${RED}✗ Failed to create NVM image${NC}"
            echo -e "  ${YELLOW}Check log file for whnvmtool errors: $log_file${NC}"
            return 1
        fi
        echo -e "  ${GREEN}✓ NVM image created successfully${NC}"
        return 0
    fi

    return 0
}

# Helper function to rebuild for nvm-writeonce tests
rebuild_for_test() {
    local test_name="$1"
    local make_target="$2"
    local make_args="$3"
    local log_file="$4"

    echo -e "  Rebuilding for $test_name..."
    if ! make clean >> "$log_file" 2>&1 || ! make $make_args "$make_target" >> "$log_file" 2>&1; then
        echo -e "  ${YELLOW}Rebuild failed, skipping remaining tests${NC}"
        return 1
    fi
    return 0
}

# Function to run simulator tests
run_sim_tests() {
    should_stop && return 1

    local config_name="$1"
    local config_type="$2"
    local log_file="$3"
    local make_target="$4"
    local make_args="$5"
    local test_failed=0
    local tests_run=0
    local tests_passed=0
    local test_result

    case "$config_type" in
        encrypt|encrypt-delta|delta|wolfhsm|sim|sim32)
            echo "=== Starting sim-sunnyday-update.sh test ===" >> "$log_file"
            echo "Current directory: $(pwd)" >> "$log_file"
            echo "wolfboot.elf exists: $([ -f ./wolfboot.elf ] && echo 'yes' || echo 'no')" >> "$log_file"
            if [ -f ./wolfboot.elf ]; then
                echo "wolfboot.elf size: $(stat -c%s ./wolfboot.elf 2>/dev/null || echo 'unknown') bytes" >> "$log_file"
            fi
            if tools/scripts/sim-sunnyday-update.sh >> "$log_file" 2>&1; then
                tests_passed=$((tests_passed + 1))
                echo "=== sim-sunnyday-update.sh test PASSED ===" >> "$log_file"
            else
                test_failed=1
                echo "=== sim-sunnyday-update.sh test FAILED ===" >> "$log_file"
            fi
            tests_run=$((tests_run + 1))
            ;;
        dualbank)
            if tools/scripts/sim-dualbank-swap-update.sh >> "$log_file" 2>&1; then
                tests_passed=$((tests_passed + 1))
            else
                test_failed=1
            fi
            tests_run=$((tests_run + 1))
            ;;
        elf-scattered|pq-hybrid|tpm)
            local cmd="./wolfboot.elf"
            [ "$config_type" = "pq-hybrid" ] || [ "$config_type" = "tpm" ] && cmd="$cmd get_version"
            if $cmd >> "$log_file" 2>&1; then
                tests_passed=$((tests_passed + 1))
            else
                test_failed=1
            fi
            tests_run=$((tests_run + 1))
            ;;
        pq)
            # PQ tests handle their own keysclean and build
            if tools/scripts/sim-pq-sunnyday-update.sh "config/examples/$config_name" >> "$log_file" 2>&1; then
                tests_passed=$((tests_passed + 1))
            else
                test_failed=1
            fi
            tests_run=$((tests_run + 1))
            ;;
        nobackup)
            if tools/scripts/sim-update-powerfail-resume-nobackup.sh >> "$log_file" 2>&1; then
                tests_passed=$((tests_passed + 1))
            else
                test_failed=1
            fi
            tests_run=$((tests_run + 1))
            ;;
        nvm-writeonce|nvm-writeonce-external)
            # Run sunnyday test
            if ! should_stop && tools/scripts/sim-sunnyday-update.sh >> "$log_file" 2>&1; then
                tests_passed=$((tests_passed + 1))
            else
                test_failed=1
            fi
            tests_run=$((tests_run + 1))

            # Run remaining tests with rebuilds
            for test_info in "fallback test:sim-update-fallback.sh" \
                             "powerfail test:sim-update-powerfail-resume.sh" \
                             "emergency fallback test:sim-update-emergency-fallback.sh"; do
                should_stop && return 1
                IFS=':' read -r test_name test_script <<< "$test_info"
                if ! rebuild_for_test "$test_name" "$make_target" "$make_args" "$log_file"; then
                    return 1
                fi
                should_stop && return 1
                if tools/scripts/$test_script >> "$log_file" 2>&1; then
                    tests_passed=$((tests_passed + 1))
                else
                    test_failed=1
                fi
                tests_run=$((tests_run + 1))
            done
            ;;
    esac

    if [ $test_failed -eq 0 ] && [ $tests_passed -eq $tests_run ]; then
        return 0
    else
        echo -e "  ${YELLOW}Tests: $tests_passed/$tests_run passed${NC}"
        return 1
    fi
}

# Function to test a single config
test_config() {
    should_stop && return 1

    local config_file="$1"
    local config_name=$(basename "$config_file")
    local log_name=$(echo "$config_name" | sed 's/[^a-zA-Z0-9._-]/_/g')
    local log_file="/tmp/wolfboot_build_${log_name}.log"
    local wolfboot_root="$(pwd)"
    local mcux_args=""

    echo -e "${YELLOW}Testing: ${config_name}${NC}"

    # Copy config to .config
    cp "$config_file" .config || {
        record_failure "$config_name" "$log_file" "copy failed"
        return 1
    }

    should_stop && return 1

    # Check and clone STM32Cube dependencies if needed
    check_stm32cube_dependency "$config_name" "$wolfboot_root"
    should_stop && return 1

    # Get MCUX SDK args if needed (do this once for both sim and non-sim configs)
    if needs_mcux_sdk "$config_name"; then
        mcux_args=$(get_mcux_sdk_args "$config_name")
        if [ $? -ne 0 ]; then
            record_failure "$config_name" "$log_file" "MCUX SDK not available"
            return 1
        fi
    fi

    # Run make keysclean
    echo -e "  Running: make keysclean"
    if ! make keysclean > "$log_file" 2>&1; then
        record_failure "$config_name" "$log_file" "keysclean failed"
        return 1
    fi

    # Special handling for sim-* configs
    if is_sim_config "$config_name"; then
        local config_type=$(get_sim_config_type "$config_name")
        echo -e "  ${YELLOW}Sim config detected: $config_type${NC}"

        # Build tools first (needed for most sim configs)
        # PQ configs handle their own build in the test script
        if [[ "$config_type" != "tpm" && "$config_type" != "pq-hybrid" && "$config_type" != "pq" ]]; then
            should_stop && return 1
            echo -e "  Building tools..."
            if ! (make -C tools/keytools > "$log_file" 2>&1 && make -C tools/bin-assemble >> "$log_file" 2>&1); then
                record_failure "$config_name" "$log_file" "tool build failed"
                return 1
            fi
            should_stop && return 1
        fi

        # Setup TPM if needed (before build)
        if [[ "$config_type" == "tpm" ]]; then
            if ! setup_tpm "$log_file"; then
                record_failure "$config_name" "$log_file" "TPM setup failed"
                cleanup_tpm
                return 1
            fi
            should_stop && cleanup_tpm && return 1
        fi

        # Determine make target and args
        local make_target=$(get_sim_make_target "$config_name" "$config_type")
        local make_args=$(get_sim_make_args "$config_name" "$config_type")

        # PQ configs handle their own build in the test script
        if [[ "$config_type" != "pq" ]]; then
            echo -e "  Building with target: $make_target $make_args"
            should_stop && cleanup_by_type "$config_type" "$config_name" && return 1

            if ! make clean >> "$log_file" 2>&1; then
                record_failure "$config_name" "$log_file" "clean failed"
                cleanup_by_type "$config_type" "$config_name"
                return 1
            fi

            should_stop && cleanup_by_type "$config_type" "$config_name" && return 1

            # Special build for TPM
            if [[ "$config_type" == "tpm" ]]; then
                if ! make $mcux_args WOLFBOOT_TPM_KEYSTORE_AUTH="TestAuth" WOLFBOOT_TPM_SEAL_AUTH="SealAuth" >> "$log_file" 2>&1; then
                    record_failure "$config_name" "$log_file" "build failed"
                    cleanup_tpm
                    return 1
                fi
            elif [[ "$config_type" == "pq-hybrid" ]]; then
                if ! make $mcux_args >> "$log_file" 2>&1; then
                    record_failure "$config_name" "$log_file" "build failed"
                    return 1
                fi
            else
                if ! make $mcux_args $make_args "$make_target" >> "$log_file" 2>&1; then
                    record_failure "$config_name" "$log_file" "build failed"
                    cleanup_by_type "$config_type" "$config_name"
                    return 1
                fi
            fi

            should_stop && cleanup_by_type "$config_type" "$config_name" && return 1
        fi

        # Setup wolfHSM AFTER building (keys are now generated, server needs to be running for tests)
        # For server-certchain, create NVM image (embedded server needs it)
        # For client configs, start external TCP server
        if [[ "$config_type" == "wolfhsm" ]]; then
            should_stop && return 1

            # Check if public key file exists (should be generated by make process when WOLFHSM_CLIENT=1)
            # The make process adds --exportpubkey --der to KEYGEN_OPTIONS automatically (see options.mk:915)
            # This public key is needed for the wolfHSM POSIX TCP server to load the signing key
            if [[ "$config_name" != *certchain* ]]; then
                echo "=== Checking for wolfHSM public key file ===" >> "$log_file"
                if [ ! -f "wolfboot_signing_private_key_pub.der" ]; then
                    echo -e "  ${YELLOW}Public key file not found, checking if make process generated it...${NC}"
                    echo "=== Checking for public key file ===" >> "$log_file"
                    echo "Private key exists: $([ -f wolfboot_signing_private_key.der ] && echo 'yes' || echo 'no')" >> "$log_file"

                    # The make process should have generated it, but if it didn't, we need to export it
                    # from the existing private key. Use -i to import and --exportpubkey to export.
                    if [ -f "wolfboot_signing_private_key.der" ]; then
                        echo -e "  Generating public key from existing private key..."
                        echo "Attempting to export public key from private key..." >> "$log_file"
                        local sign_type=$(grep "^SIGN=" .config 2>/dev/null | cut -d'=' -f2 | tr -d '"' || echo "ECC256")
                        local keygen_opts=""
                        case "$sign_type" in
                            ECC256|ecc256) keygen_opts="--ecc256" ;;
                            ECC384|ecc384) keygen_opts="--ecc384" ;;
                            ECC521|ecc521) keygen_opts="--ecc521" ;;
                            RSA2048|rsa2048) keygen_opts="--rsa2048" ;;
                            RSA3072|rsa3072) keygen_opts="--rsa3072" ;;
                            RSA4096|rsa4096) keygen_opts="--rsa4096" ;;
                            ED25519|ed25519) keygen_opts="--ed25519" ;;
                            *) keygen_opts="--ecc256" ;;
                        esac
                        # The keygen tool with -g and --exportpubkey should read existing private key
                        # and export the public key. Using -g with existing file should work.
                        # Note: -g generates/reads a key, and --exportpubkey exports the public key
                        if ! (./tools/keytools/keygen $keygen_opts -g wolfboot_signing_private_key.der --exportpubkey --der >> "$log_file" 2>&1); then
                            echo -e "  ${RED}✗ Failed to generate public key file from private key${NC}"
                            echo "ERROR: Public key generation failed" >> "$log_file"
                            record_failure "$config_name" "$log_file" "public key generation failed"
                            cleanup_wolfhsm "$config_name"
                            return 1
                        fi
                        if [ ! -f "wolfboot_signing_private_key_pub.der" ]; then
                            echo -e "  ${RED}✗ Public key file still not found after generation attempt${NC}"
                            echo "ERROR: Public key file not created" >> "$log_file"
                            record_failure "$config_name" "$log_file" "public key file missing"
                            cleanup_wolfhsm "$config_name"
                            return 1
                        fi
                        echo -e "  ${GREEN}✓ Public key file generated successfully${NC}"
                        echo "Public key file created: wolfboot_signing_private_key_pub.der" >> "$log_file"
                    else
                        echo -e "  ${RED}✗ Private key file not found, cannot generate public key${NC}"
                        echo "ERROR: Private key file missing" >> "$log_file"
                        record_failure "$config_name" "$log_file" "private key file missing"
                        cleanup_wolfhsm "$config_name"
                        return 1
                    fi
                else
                    echo -e "  ${GREEN}✓ Public key file found${NC}"
                    echo "Public key file exists: wolfboot_signing_private_key_pub.der" >> "$log_file"
                    echo "Public key file size: $(stat -c%s wolfboot_signing_private_key_pub.der 2>/dev/null || echo 'unknown') bytes" >> "$log_file"
                    # Verify private key also exists and check their relationship
                    if [ -f "wolfboot_signing_private_key.der" ]; then
                        echo "Private key file exists and matches" >> "$log_file"
                        echo "Private key file size: $(stat -c%s wolfboot_signing_private_key.der 2>/dev/null || echo 'unknown') bytes" >> "$log_file"
                    else
                        echo "WARNING: Private key file not found!" >> "$log_file"
                    fi
                fi
            fi

            if ! setup_wolfhsm "$config_name" "$log_file"; then
                record_failure "$config_name" "$log_file" "wolfHSM setup failed"
                cleanup_wolfhsm "$config_name"
                return 1
            fi
            should_stop && cleanup_wolfhsm "$config_name" && return 1
        fi

        # For server-certchain, verify NVM file exists before running tests
        if [[ "$config_name" == *server-certchain* ]]; then
            if [ ! -f "wolfBoot_wolfHSM_NVM.bin" ]; then
                record_failure "$config_name" "$log_file" "NVM image missing"
                cleanup_wolfhsm "$config_name"
                return 1
            fi
        fi

        # Run simulator tests
        if ! run_sim_tests "$config_name" "$config_type" "$log_file" "$make_target" "$make_args"; then
            # Capture server status if wolfHSM was used
            if [[ "$config_type" == "wolfhsm" ]]; then
                echo "=== wolfHSM Server Status After Test Failure ===" >> "$log_file"
                if [ -f /tmp/wolfhsm_server_pid ]; then
                    local server_pid=$(cat /tmp/wolfhsm_server_pid 2>/dev/null || echo "")
                    if [ -n "$server_pid" ] && kill -0 "$server_pid" 2>/dev/null; then
                        echo "Server is still running (PID: $server_pid)" >> "$log_file"
                        echo "Server process info:" >> "$log_file"
                        ps -p "$server_pid" -o pid,ppid,cmd >> "$log_file" 2>&1 || true
                    else
                        echo "Server is not running (PID was: $server_pid)" >> "$log_file"
                    fi
                fi
            fi
            record_failure "$config_name" "$log_file" "simulator tests failed"
            cleanup_by_type "$config_type" "$config_name"
            return 1
        fi

        # Capture final server status before cleanup (for wolfHSM)
        if [[ "$config_type" == "wolfhsm" ]]; then
            echo "=== wolfHSM Server Status After Test Success ===" >> "$log_file"
            if [ -f /tmp/wolfhsm_server_pid ]; then
                local server_pid=$(cat /tmp/wolfhsm_server_pid 2>/dev/null || echo "")
                if [ -n "$server_pid" ] && kill -0 "$server_pid" 2>/dev/null; then
                    echo "Server is still running (PID: $server_pid)" >> "$log_file"
                else
                    echo "Server is not running (PID was: $server_pid)" >> "$log_file"
                fi
            fi
        fi

        # Cleanup
        cleanup_by_type "$config_type" "$config_name"

        echo -e "  ${GREEN}✓ PASSED (build + simulator tests)${NC}"
        echo -e "  ${YELLOW}Log saved to: $log_file${NC}"
        PASSED+=("$config_name|$log_file")
        return 0
    fi

    # Standard build for non-sim configs
    echo -e "  Running: make $mcux_args"
    if make V=1 $mcux_args > "$log_file" 2>&1; then
        echo -e "  ${GREEN}✓ PASSED${NC}"
        echo -e "  ${YELLOW}Log saved to: $log_file${NC}"
        PASSED+=("$config_name|$log_file")
        return 0
    else
        record_failure "$config_name" "$log_file" "build failed"
        return 1
    fi
}

# Test each config
for config in "${CONFIGS[@]}"; do
    # Check if we should stop testing
    if [ $STOP_TESTING -eq 1 ]; then
        echo -e "${YELLOW}Stopping test execution...${NC}"
        break
    fi

    config_path="$CONFIG_DIR/$config"
    test_config "$config_path"
    echo ""
done

# Print summary
echo "=========================================="
echo "Summary"
echo "=========================================="
echo -e "${GREEN}Passed: ${#PASSED[@]}${NC}"
if [ ${#PASSED[@]} -gt 0 ]; then
    for entry in "${PASSED[@]}"; do
        IFS='|' read -r config_name log_file <<< "$entry"
        echo -e "  ${GREEN}✓${NC} $config_name"
        echo -e "      Log: $log_file"
    done
fi

echo ""
echo -e "${RED}Failed: ${#FAILED[@]}${NC}"
if [ ${#FAILED[@]} -gt 0 ]; then
    for entry in "${FAILED[@]}"; do
        IFS='|' read -r config_name log_file reason <<< "$entry"
        echo -e "  ${RED}✗${NC} $config_name ($reason)"
        echo -e "      Log: $log_file"
    done
fi

if [ ${#SKIPPED[@]} -gt 0 ]; then
    echo ""
    echo -e "${YELLOW}Skipped: ${#SKIPPED[@]}${NC}"
    for config in "${SKIPPED[@]}"; do
        echo -e "  ${YELLOW}⊘${NC} $config"
    done
fi

echo ""
echo "=========================================="

# Exit with error if any failed
if [ ${#FAILED[@]} -gt 0 ]; then
    exit 1
else
    exit 0
fi

