# jtag_load.tcl - load wolfboot.elf onto a ZC702 via Xilinx Platform Cable II.
#
# Uses the prebuilt Zynq-7000 FSBL (zynq_fsbl.elf) to bring DDR / MIO /
# clocks / UART up, then loads wolfboot.elf over the top and starts it.
#
# Usage:
#   source /opt/Xilinx/2025.2/Vitis/settings64.sh
#   xsdb tools/scripts/zc702/jtag_load.tcl
#
# Set the JTAG boot mode straps on the ZC702 (SW16 = all OFF) before use.
# After this script runs the board may need a power-cycle to recover the
# CPU into a JTAG-loadable state again.
#
# Override paths via env:
#   FSBL_ELF=...      FSBL ELF path
#   WOLFBOOT_ELF=...  wolfboot ELF path

set fsbl_default      "$::env(HOME)/GitHub/soc-prebuilt-firmware/zc702-zynq/zynq_fsbl.elf"
set wolfboot_default  "[file dirname [info script]]/../../../wolfboot.elf"

if {[info exists ::env(FSBL_ELF)]}     { set fsbl_elf $::env(FSBL_ELF) } \
   else                                { set fsbl_elf $fsbl_default }
if {[info exists ::env(WOLFBOOT_ELF)]} { set wolfboot_elf $::env(WOLFBOOT_ELF) } \
   else                                { set wolfboot_elf $wolfboot_default }

if {![file exists $fsbl_elf]} {
    puts "ERROR: FSBL not found at $fsbl_elf"
    puts "Clone wolfSSL/soc-prebuilt-firmware next to wolfboot or set FSBL_ELF."
    exit 1
}
if {![file exists $wolfboot_elf]} {
    puts "ERROR: wolfboot.elf not found at $wolfboot_elf"
    exit 1
}

connect

# Sometimes the chain comes up empty if the previous run left the CPU in an
# off-chain state (e.g. WFI with clock gated). Retry the target lookup.
for {set i 0} {$i < 5} {incr i} {
    catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}} rc
    if {[string first "no targets" $rc] < 0} { break }
    puts "Cortex-A9 not on chain yet, retry $i ..."
    after 500
}
if {[string first "no targets" $rc] >= 0} {
    puts "ERROR: no Cortex-A9 targets visible after retries."
    puts "Power-cycle the ZC702 (SW10) and try again."
    exit 1
}

# Full PS reset, then wait for BootROM to enter JTAG-mode poll loop.
rst -system
after 1500
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

# Run FSBL to completion. It does ps7_init (DDR/MIO/clocks/UART), then
# parks itself since no bundled second-stage exists. 2-3s is plenty.
puts "Loading FSBL: $fsbl_elf"
dow $fsbl_elf
con
after 3000

# Stop where FSBL parked, but do NOT rst -processor here - that would drop
# us back into BootROM and lose FSBL's PS state.
stop

# Load wolfBoot at its DDR address. xsdb's `dow` does NOT consistently set
# PC after a second target dow, so set PC and CPSR explicitly.
puts "Loading wolfBoot: $wolfboot_elf"
dow $wolfboot_elf
rwr pc 0x04000000
rwr cpsr 0xD3                  ;# SVC mode, IRQ+FIQ masked

puts "Resuming - watch UART1 (115200 8N1) for the wolfBoot banner."
con
