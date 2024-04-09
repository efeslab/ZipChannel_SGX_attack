I never fully automated the process of pre-computing the cache slices.  
[`..SampleEnclave/pfn2slice.h`](..SampleEnclave/pfn2slice.h)  contains sample output from running all the tools in this directory.  

High level instructions to generate the file:
```
for i in `seq 10` ; do time sudo taskset -c 1 ./app >> log_may26_2021 ; done
```
this will generate a file that looks like this:
```
0x716df: 2
0x716e0: 1
0x716e1: 1
0x716e2: 1
0x716e3: 1
...
```
Then do `sort`, `uniq`, remove the last digit (`0x716df` --> `0x716d`). Then `uniq` again. Finally, just leave the numbers and format it nicely to have 20 numbers in a line with the commas.

Note: our Ubuntu installation always allocates the same physical page, which is hard-coded in the code.  
Right now the main attacker code
`make && taskset -c 1 sudo ./app`
outputs that it uses the page of `0x7c0000`. This matces what the C file in this directory uses.
