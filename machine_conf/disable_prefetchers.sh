sudo modprobe msr
sudo wrmsr -a 0x1a4 15 # disable HW prefetchers
