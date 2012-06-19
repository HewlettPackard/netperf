# one of these days I should make it take a parameter or three
grep eth0 /proc/interrupts | awk -F ":" '{printf("echo 1 > /proc/irq/%d/smp_affinity\n",$1)}' | sh
