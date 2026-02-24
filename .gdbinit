file wolfboot.elf
tar rem:3333
add-symbol-file ../hart-software-services/build/hss-l2scratch.elf
set pagination off
foc c

set $target_riscv=1
set mem inaccessible-by-default off
set architecture riscv:rv64