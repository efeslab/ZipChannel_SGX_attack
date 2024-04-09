/*
 * Copyright (C) 2011-2020 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#include "Enclave.h"
#include "Enclave_t.h" /* print_string */
#include <stdarg.h>
#include <stdio.h> /* vsnprintf */
#include <string.h>
#include "sgx_trts.h"

#define BZ_NO_STDIO
extern "C" {
	void bz_internal_error ( int errcode );
	#include "../../../bzip2-1.0.6/bzlib.h"
}

/* 
 * printf: 
 *   Invokes OCALL to display the enclave buffer to the terminal.
 */
int printf(const char* fmt, ...)
{
    char buf[BUFSIZ] = { '\0' };
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, BUFSIZ, fmt, ap);
    va_end(ap);
    ocall_print_string(buf);
    return (int)strnlen(buf, BUFSIZ - 1) + 1;
}

extern "C" {
	void bz_internal_error ( int errcode )
	{
		printf("bz_internal_error! errcode = %d\n", errcode);
	}
}

char plaintext[10000] = "I am plaintext\n";
char compressed[20000] = { 0 };
unsigned int plaintext_size = sizeof(plaintext);
unsigned int compressed_size = sizeof(compressed);

void ecall_bzip2()
{
	printf("Hello! I am ecall_zbip2 %p\n", (void*)BZ2_bzBuffToBuffCompress);

	sgx_read_rand((uint8_t*)plaintext, sizeof(plaintext));
//	plaintext[0] = 'k';
//	plaintext[10000 - 1 - 0] = 'w';
//	plaintext[10000 - 1 - 1] = 'M';
//	plaintext[10000 - 1 - 2] = 'a';
//	plaintext[10000 - 1 - 3] = 'r';
//	plaintext[10000 - 1 - 4] = 'i';
//	plaintext[10000 - 1 - 5] = 'n';
//	plaintext[10000 - 1 - 6] = 'a';
//	plaintext[10000 - 1 - 7] = 'a';
//	plaintext[10000 - 1 - 8] = 'M';
//	plaintext[10000 - 1 - 9] = 'a';

	int res = BZ2_bzBuffToBuffCompress(
		compressed, &compressed_size,
		plaintext, plaintext_size,
		9, 0, 30);
	printf("BZ2_bzBuffToBuffCompress returned %d (should be 0)\n", res);
}

void ecall_get_secret(char* copy_to) {
	memcpy(copy_to, plaintext, 10000);
}
