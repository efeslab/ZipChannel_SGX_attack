Files in this directory:

* [`boot.md`](boot.md) contains what we add in the grub boot loader to enable huge pages.
* [`enable_huge_pages.sh`](enable_huge_pages.sh) is the next step required in enabling hguge pages.
* [`disable_prefetchers.sh`](disable_prefetchers.sh) disable prefetchers.
* [`set_cat.sh`](set_cat.sh) configures CAT in the way that the attack uses it. Note: this code is machine specific and assumes a machine with 2 physical cores and hyperthreading disabled from the BIOS.
* [`all.sh`](all.sh) executes all the `.sh` scripts above.
