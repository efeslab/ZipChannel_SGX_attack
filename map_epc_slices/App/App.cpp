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

unsigned long read_pagemap(unsigned long virt_addr){
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
	//printf("Vaddr: 0x%lx, Page_size: %d, Entry_size: %d\n", virt_addr, getpagesize(), PAGEMAP_ENTRY);
	//printf("Reading %s at 0x%llx\n", path_buf, (unsigned long long) file_offset);
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
		//printf("[%d]0x%x ", i, c);
	}
	for(i=0; i < PAGEMAP_ENTRY; i++){
		//printf("%d ",c_buf[i]);
		read_val = (read_val << 8) + c_buf[i];
	}
	//printf("\n");
	//printf("Result: 0x%llx\n", (unsigned long long) read_val);
	//if(GET_BIT(read_val, 63))
	if(GET_BIT(read_val, 63)) {
		//printf("PFN: 0x%llx, virt: 0x%lx\n",(unsigned long long) GET_PFN(read_val), virt_addr);
		return GET_PFN(read_val);
	}
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
void init_g_buf()
{

	g_buf = (unsigned char*)mmap(NULL, 1<<30,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0); 
	assert(g_buf != MAP_FAILED);
	//printf("g_buf = %p\n", g_buf);
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

void add_slice_scores(unsigned long sgx_pfn, int slice_scores[], unsigned long offset)
{
	unsigned long set = sgx_pfn & 0xf;
	for(size_t i = 0 ; i < L3_SIZE/L3_WAYS ; i += 64) {
		if((i & 0xf000) != set * 0x1000)
			continue;
		if((i % 0x1000) != offset)
			continue;
		
		unsigned long val = *((unsigned long*)(g_buf + i));

		int slice = i & 0xf0000;
		slice /= 0x10000;
		assert(slice >= 0 && slice < 4);

		if(val >= 300) {
			slice_scores[slice] ++;
		}
	}
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
	sgx_status_t ret = SGX_ERROR_UNEXPECTED;

	/* Initialize the enclave */
	if(initialize_enclave() < 0){
		printf("Enter a character before exit ...\n");
		getchar();
		return -1; 
	}
	//printf("==========================================================\n");

	OpenDriver();

	init_g_buf();
	unsigned long g_buf_pfn = read_pagemap((unsigned long)g_buf);
	assert(g_buf_pfn == 0x7c0000); // I always get this pfn, which is nice

	unsigned long sgx_page = 0;
	ret = ecall_get_page_addr(global_eid, &sgx_page);
	assert((sgx_page & 0xfff) == 0);
	assert(ret == SGX_SUCCESS);

	for(int i = 0 ; i < 10000 ; i ++) {
		swap_out_in(sgx_page);
		unsigned long sgx_pfn = virt2pfn(sgx_page);
		
		int slice_scores[4] = { 0 };
		int cnt_repeat = 0;

		for(int j = 0 ; j < 5 ; j++) {
			for(int offset = 0 ; offset < 0x1000 ; offset += 0x40) {
				fill_g_buf();
				ret = ecall_touch_page(global_eid, offset);
				probe_g_buf();

				assert(ret == SGX_SUCCESS);
				//print_g_buf();
				add_slice_scores(sgx_pfn, slice_scores, offset);
				cnt_repeat ++;
			}
		}
		int winner = -1;
		int cnt_winners = 0;
		for(int j = 0 ; j < 4 ; j ++) {
			if(slice_scores[j] == cnt_repeat) {
				winner = j;
				cnt_winners ++;
			}
		}
		printf("0x%lx: ", sgx_pfn);
		if(cnt_winners != 1) {
			printf("undecided\n");
		} else {
			printf("%d\n", winner);
		}
	}

	//ret = ecall_bzip2(global_eid);
	//assert(ret == SGX_SUCCESS);

	/* Destroy the enclave */
	sgx_destroy_enclave(global_eid);
	
	//printf("Info: SampleEnclave successfully returned.\n");

	return 0;
}

