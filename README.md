This reposiroty contains artifacts for the SGX arrack in the ZipChannel paper. The main repository for the artifacts can be found here: https://github.com/efeslab/ZipChannel

This repository contains 3 directories:
* [`machine_conf`](machine_conf) contains the machine's settings that we use in the attack.
* [`map_epc_slices`](map_epc_slices) contains tools for pre-computing the slicing funstion.
* [`SampleEnclave`](SampleEnclave) contains the Enclave and attacker code used in the attack.
* `bzip2-1.0.6` - does not exist in the repo, but is required for running the attack. To add it to the repo:
  ```
  git clome https://github.com/enthought/bzip2-1.0.6.git
  git checkout 288acf97a15d558f96c24c89f578b724d6e06b0c
  ```
  Then to compile Bzip2 as a stand-alone library that does not rely on system IO (required for running it inside SGX):  
  replace this line in the Makefile:  
  `CFLAGS=-Wall -Winline -O2 -g $(BIGFILES)`  
  with this  
  `CFLAGS=-Wall -Winline -O2 -g $(BIGFILES) -DBZ_NO_STDIO`
