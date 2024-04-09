in `/etc/default/grub` add `isolcpus=1`, `nohz_full`, `default_hugepagesz=1G hugepagesz=1G` as boot parameter to isolate cpu1
Then run `sudo update-grub`
