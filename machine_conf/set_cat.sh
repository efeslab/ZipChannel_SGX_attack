#!/bin/bash

modprobe msr
wrmsr 0xc90 0xff00
wrmsr 0xc91 0x0001
wrmsr 0xc92 0x0010
wrmsr -p 0 0xc8f 2	# rest of CPU
wrmsr -p 1 0xc8f 1	# victim
#wrmsr -p 2 0xc8f 2	# thread to empty MEE cache
#wrmsr -p 3 0xc8f 0	# hopefully all the extra noise

