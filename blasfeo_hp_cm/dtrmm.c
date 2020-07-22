/**************************************************************************************************
*                                                                                                 *
* This file is part of BLASFEO.                                                                   *
*                                                                                                 *
* BLASFEO -- BLAS for embedded optimization.                                                      *
* Copyright (C) 2019 by Gianluca Frison.                                                          *
* Developed at IMTEK (University of Freiburg) under the supervision of Moritz Diehl.              *
* All rights reserved.                                                                            *
*                                                                                                 *
* The 2-Clause BSD License                                                                        *
*                                                                                                 *
* Redistribution and use in source and binary forms, with or without                              *
* modification, are permitted provided that the following conditions are met:                     *
*                                                                                                 *
* 1. Redistributions of source code must retain the above copyright notice, this                  *
*    list of conditions and the following disclaimer.                                             *
* 2. Redistributions in binary form must reproduce the above copyright notice,                    *
*    this list of conditions and the following disclaimer in the documentation                    *
*    and/or other materials provided with the distribution.                                       *
*                                                                                                 *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND                 *
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED                   *
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE                          *
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR                 *
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES                  *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;                    *
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND                     *
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT                      *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS                   *
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                    *
*                                                                                                 *
* Author: Gianluca Frison, gianluca.frison (at) imtek.uni-freiburg.de                             *
*                                                                                                 *
**************************************************************************************************/

#include <stdlib.h>
#include <stdio.h>

#include <blasfeo_target.h>
#include <blasfeo_block_size.h>
#include <blasfeo_common.h>
#include <blasfeo_d_aux.h>
#include <blasfeo_d_kernel.h>



// TODO move to a header file to reuse across routines
#define EL_SIZE 8 // double precision

#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_ARMV8A_ARM_CORTEX_A53)
#define M_KERNEL 12 // max kernel: 12x4
#define L1_CACHE_EL (32*1024/EL_SIZE) // L1 data cache size: 32 kB
#define CACHE_LINE_EL (64/EL_SIZE) // data cache size: 64 bytes

#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE) | defined(TARGET_ARMV8A_ARM_CORTEX_A57)
#define M_KERNEL 8 // max kernel: 8x4
#define L1_CACHE_EL (32*1024/EL_SIZE) // L1 data cache size: 32 kB
#define CACHE_LINE_EL (64/EL_SIZE) // data cache size: 64 bytes

#else // assume generic target
#define M_KERNEL 4 // max kernel: 4x4
#define L1_CACHE_EL (32*1024/EL_SIZE) // L1 data cache size: 32 kB
#define CACHE_LINE_EL (64/EL_SIZE) // data cache size: 64 bytes // TODO 32-bytes for cortex A9
#endif



void blasfeo_hp_dtrmm_llnn(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{

#if defined(PRINT_NAME)
	printf("\nblasfeo_hp_dtrmm_llnn (cm) %d %d %f %p %d %d %p %d %d %p %d %d\n", m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
#endif

	if(m<=0 | n<=0)
		return;

	// extract pointer to column-major matrices from structures
	int lda = sA->m;
	int ldb = sB->m;
	int ldd = sD->m;
	double *A = sA->pA + ai + aj*lda;
	double *B = sB->pA + bi + bj*ldb;
	double *D = sD->pA + di + dj*ldb;

//	printf("\n%p %d %p %d %p %d\n", A, lda, B, ldb);

	int ii, jj;

	const int ps = 4; //D_PS;

#if defined(TARGET_GENERIC)
	double pd0[K_MAX_STACK];
#else
	ALIGNED( double pd0[K_MAX_STACK], 64 );
#endif

#if defined(TARGET_GENERIC)
	double pU0[M_KERNEL*K_MAX_STACK];
#else
	ALIGNED( double pU0[M_KERNEL*K_MAX_STACK], 64 );
#endif

	int k0;
	// TODO update if necessary !!!!!
//	if(*side=='l' | *side=='L')
		k0 = m;
//	else
//		k0 = n;

	int sdu0 = (k0+3)/4*4;
	sdu0 = sdu0<K_MAX_STACK ? sdu0 : K_MAX_STACK;

	struct blasfeo_pm_dmat tA, tB;
	int sda, sdb;
	int tA_size, tB_size;
	void *mem;
	char *mem_align;
	int m1, n1, k1;
	int pack_B;

	double *pU, *dA, *pB, *dB;
	int sdu;

	const int m_kernel = M_KERNEL;
	const int l1_cache_el = L1_CACHE_EL;
	const int reals_per_cache_line = CACHE_LINE_EL;

	const int m_cache = (m+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int n_cache = (n+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
//	const int k_cache = (k+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int m_kernel_cache = (m_kernel+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	int m_min = m_cache<m_kernel_cache ? m_cache : m_kernel_cache;
//	int n_min = n_cache<m_kernel_cache ? n_cache : m_kernel_cache;
	
	double d_0 = 0.0;


#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>=200 | n>=200 | n>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | n>=64 | n>K_MAX_STACK)
#else
	if(m>=12 | n>=12 | n>K_MAX_STACK)
#endif
		{
		goto llnn_1;
		}
	else
		{
		goto llnn_0;
		}

	// never to get here
	return;


llnn_0:
	pU = pU0;
	sdu = sdu0;

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_4x12_tran_lib4c4c(jj, &alpha, pU, sdu, A+jj, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_4x12_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto llnn_0_left_4;
			}
		else if(n-ii<=8)
			{
			goto llnn_0_left_8;
			}
		else
			{
			goto llnn_0_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_4x8_tran_lib4c4c(jj, &alpha, pU, sdu, A+jj, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_4x8_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto llnn_0_left_4;
			}
		else
			{
			goto llnn_0_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_4x4_tran_lib4c4c(jj, &alpha, pU, A+jj, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_4x4_tran_vs_lib4c4c(jj, &alpha, pU, A+jj, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto llnn_0_left_4;
		}
#endif
goto llnn_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
llnn_0_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_4x12_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
//		kernel_dtrmm_nt_rl_4x12_tran_lib4c4c(jj, &alpha, pU, sdu, A+jj, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
		}
goto llnn_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
llnn_0_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_4x8_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnn_0_return;
#endif

llnn_0_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_4x4_tran_vs_lib4c4c(jj, &alpha, pU, A+jj, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnn_0_return;

llnn_0_return:
	return;



llnn_1:
	m1 = (m+128-1)/128*128;
	tA_size = blasfeo_pm_memsize_dmat(ps, m_kernel, m1);
	tB_size = blasfeo_pm_memsize_dmat(ps, m1, m1);
	mem = malloc(tA_size+tB_size+64);
	blasfeo_align_64_byte(mem, (void **) &mem_align);
	blasfeo_pm_create_dmat(ps, m_kernel, m, &tA, (void *) mem_align);
	blasfeo_pm_create_dmat(ps, m, m, &tB, (void *) (mem_align+tA_size));

	pU = tA.pA;
	sdu = tA.cn;
	pB = tB.pA;
	sdb = tB.cn;

	// lower to lower
	for(ii=0; ii<m-3; ii+=4)
		{
		kernel_dpack_tt_4_lib4(m-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb, sdb);
		}
	if(ii<m)
		{
		kernel_dpack_tt_4_vs_lib4(m-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb, sdb, m-ii);
		}

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_4x12_tran_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_4x12_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto llnn_1_left_4;
			}
		else if(n-ii<=8)
			{
			goto llnn_1_left_8;
			}
		else
			{
			goto llnn_1_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_4x8_tran_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_4x8_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto llnn_1_left_4;
			}
		else
			{
			goto llnn_1_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_4x4_tran_lib444c(jj, &alpha, pU, pB+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_4x4_tran_vs_lib444c(jj, &alpha, pU, pB+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto llnn_1_left_4;
		}
#endif
goto llnn_1_return;

#if defined(TARGET_X64_INTEL_HASWELL)
llnn_1_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_4x12_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnn_1_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
llnn_1_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_4x8_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnn_1_return;
#endif

llnn_1_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_4x4_tran_vs_lib444c(jj, &alpha, pU, pB+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnn_1_return;

llnn_1_return:
	free(mem);
	return;


	// never to get here
	return;

	}



void blasfeo_hp_dtrmm_llnu(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{

#if defined(PRINT_NAME)
	printf("\nblasfeo_hp_dtrmm_llnu (cm) %d %d %f %p %d %d %p %d %d %p %d %d\n", m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
#endif

	if(m<=0 | n<=0)
		return;

	// extract pointer to column-major matrices from structures
	int lda = sA->m;
	int ldb = sB->m;
	int ldd = sD->m;
	double *A = sA->pA + ai + aj*lda;
	double *B = sB->pA + bi + bj*ldb;
	double *D = sD->pA + di + dj*ldb;

//	printf("\n%p %d %p %d %p %d\n", A, lda, B, ldb);

	int ii, jj;

	const int ps = 4; //D_PS;

#if defined(TARGET_GENERIC)
	double pd0[K_MAX_STACK];
#else
	ALIGNED( double pd0[K_MAX_STACK], 64 );
#endif

#if defined(TARGET_GENERIC)
	double pU0[M_KERNEL*K_MAX_STACK];
#else
	ALIGNED( double pU0[M_KERNEL*K_MAX_STACK], 64 );
#endif

	int k0;
	// TODO update if necessary !!!!!
//	if(*side=='l' | *side=='L')
		k0 = m;
//	else
//		k0 = n;

	int sdu0 = (k0+3)/4*4;
	sdu0 = sdu0<K_MAX_STACK ? sdu0 : K_MAX_STACK;

	struct blasfeo_pm_dmat tA, tB;
	int sda, sdb;
	int tA_size, tB_size;
	void *mem;
	char *mem_align;
	int m1, n1, k1;
	int pack_B;

	double *pU, *dA, *pB, *dB;
	int sdu;

	const int m_kernel = M_KERNEL;
	const int l1_cache_el = L1_CACHE_EL;
	const int reals_per_cache_line = CACHE_LINE_EL;

	const int m_cache = (m+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int n_cache = (n+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
//	const int k_cache = (k+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int m_kernel_cache = (m_kernel+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	int m_min = m_cache<m_kernel_cache ? m_cache : m_kernel_cache;
//	int n_min = n_cache<m_kernel_cache ? n_cache : m_kernel_cache;
	
	double d_0 = 0.0;



#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>=200 | n>=200 | n>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | n>=64 | n>K_MAX_STACK)
#else
	if(m>=12 | n>=12 | n>K_MAX_STACK)
#endif
		{
		goto llnu_1;
		}
	else
		{
		goto llnu_0;
		}

	// never to get here
	return;


llnu_0:
	pU = pU0;
	sdu = sdu0;

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_one_4x12_tran_lib4c4c(jj, &alpha, pU, sdu, A+jj, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_one_4x12_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto llnu_0_left_4;
			}
		if(n-ii<=8)
			{
			goto llnu_0_left_8;
			}
		else
			{
			goto llnu_0_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_one_4x8_tran_lib4c4c(jj, &alpha, pU, sdu, A+jj, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_one_4x8_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto llnu_0_left_4;
			}
		else
			{
			goto llnu_0_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_one_4x4_tran_lib4c4c(jj, &alpha, pU, A+jj, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_one_4x4_tran_vs_lib4c4c(jj, &alpha, pU, A+jj, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto llnu_0_left_4;
		}
#endif
goto llnu_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
llnu_0_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_one_4x12_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnu_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
llnu_0_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_one_4x8_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnu_0_return;
#endif

llnu_0_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_one_4x4_tran_vs_lib4c4c(jj, &alpha, pU, A+jj, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnu_0_return;

llnu_0_return:
	return;



llnu_1:
	m1 = (m+128-1)/128*128;
	tA_size = blasfeo_pm_memsize_dmat(ps, m_kernel, m1);
	tB_size = blasfeo_pm_memsize_dmat(ps, m1, m1);
	mem = malloc(tA_size+tB_size+64);
	blasfeo_align_64_byte(mem, (void **) &mem_align);
	blasfeo_pm_create_dmat(ps, m_kernel, m, &tA, (void *) mem_align);
	blasfeo_pm_create_dmat(ps, m, m, &tB, (void *) (mem_align+tA_size));

	pU = tA.pA;
	sdu = tA.cn;
	pB = tB.pA;
	sdb = tB.cn;

	// lower to lower
	for(ii=0; ii<m-3; ii+=4)
		{
		kernel_dpack_tt_4_lib4(m-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb, sdb);
		}
	if(ii<m)
		{
		kernel_dpack_tt_4_vs_lib4(m-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb, sdb, m-ii);
		}

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_one_4x12_tran_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_one_4x12_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto llnu_1_left_4;
			}
		else if(n-ii<=8)
			{
			goto llnu_1_left_8;
			}
		else
			{
			goto llnu_1_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_one_4x8_tran_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_one_4x8_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto llnu_1_left_4;
			}
		else
			{
			goto llnu_1_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_one_4x4_tran_lib444c(jj, &alpha, pU, pB+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_one_4x4_tran_vs_lib444c(jj, &alpha, pU, pB+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto llnu_1_left_4;
		}
#endif
goto llnu_1_return;

#if defined(TARGET_X64_INTEL_HASWELL)
llnu_1_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_one_4x12_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnu_1_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
llnu_1_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_one_4x8_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnu_1_return;
#endif

llnu_1_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_one_4x4_tran_vs_lib444c(jj, &alpha, pU, pB+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnu_1_return;

llnu_1_return:
	free(mem);
	return;


	// never to get here
	return;

	}



void blasfeo_hp_dtrmm_lltn(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{

#if defined(PRINT_NAME)
	printf("\nblasfeo_hp_dtrmm_lltn (cm) %d %d %f %p %d %d %p %d %d %p %d %d\n", m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
#endif

	if(m<=0 | n<=0)
		return;

	// extract pointer to column-major matrices from structures
	int lda = sA->m;
	int ldb = sB->m;
	int ldd = sD->m;
	double *A = sA->pA + ai + aj*lda;
	double *B = sB->pA + bi + bj*ldb;
	double *D = sD->pA + di + dj*ldb;

//	printf("\n%p %d %p %d %p %d\n", A, lda, B, ldb);

	int ii, jj;

	const int ps = 4; //D_PS;

#if defined(TARGET_GENERIC)
	double pd0[K_MAX_STACK];
#else
	ALIGNED( double pd0[K_MAX_STACK], 64 );
#endif

#if defined(TARGET_GENERIC)
	double pU0[M_KERNEL*K_MAX_STACK];
#else
	ALIGNED( double pU0[M_KERNEL*K_MAX_STACK], 64 );
#endif

	int k0;
	// TODO update if necessary !!!!!
//	if(*side=='l' | *side=='L')
		k0 = m;
//	else
//		k0 = n;

	int sdu0 = (k0+3)/4*4;
	sdu0 = sdu0<K_MAX_STACK ? sdu0 : K_MAX_STACK;

	struct blasfeo_pm_dmat tA, tB;
	int sda, sdb;
	int tA_size, tB_size;
	void *mem;
	char *mem_align;
	int m1, n1, k1;
	int pack_B;

	double *pU, *dA, *pB, *dB;
	int sdu;

	const int m_kernel = M_KERNEL;
	const int l1_cache_el = L1_CACHE_EL;
	const int reals_per_cache_line = CACHE_LINE_EL;

	const int m_cache = (m+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int n_cache = (n+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
//	const int k_cache = (k+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int m_kernel_cache = (m_kernel+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	int m_min = m_cache<m_kernel_cache ? m_cache : m_kernel_cache;
//	int n_min = n_cache<m_kernel_cache ? n_cache : m_kernel_cache;
	
	double d_0 = 0.0;


#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>=300 | n>=300 | n>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | n>=64 | n>K_MAX_STACK)
#else
	if(m>=12 | n>=12 | n>K_MAX_STACK)
#endif
		{
		goto lunn_1;
		}
	else
		{
		goto lltn_0;
		}

	// never to get here
	return;


lltn_0:
	pU = pU0;
	sdu = sdu0;

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nn_rl_4x12_tran_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nn_rl_4x12_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lltn_0_left_4;
			}
		if(n-ii<=8)
			{
			goto lltn_0_left_8;
			}
		else
			{
			goto lltn_0_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nn_rl_4x8_tran_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nn_rl_4x8_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lltn_0_left_4;
			}
		else
			{
			goto lltn_0_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nn_rl_4x4_tran_lib4c4c(m-jj, &alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nn_rl_4x4_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto lltn_0_left_4;
		}
#endif
	goto lltn_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
lltn_0_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nn_rl_4x12_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lltn_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
lltn_0_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nn_rl_4x8_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lltn_0_return;
#endif

lltn_0_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nn_rl_4x4_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lltn_0_return;

lltn_0_return:
	return;


lunn_1:
	m1 = (m+128-1)/128*128;
	tA_size = blasfeo_pm_memsize_dmat(ps, m_kernel, m1);
	tB_size = blasfeo_pm_memsize_dmat(ps, m1, m1);
	mem = malloc(tA_size+tB_size+64);
	blasfeo_align_64_byte(mem, (void **) &mem_align);
	blasfeo_pm_create_dmat(ps, m_kernel, m, &tA, (void *) mem_align);
	blasfeo_pm_create_dmat(ps, m, m, &tB, (void *) (mem_align+tA_size));

	pU = tA.pA;
	sdu = tA.cn;
	pB = tB.pA;
	sdb = tB.cn;

	// lower to upper
	for(ii=0; ii<m-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb);
		}
	if(ii<m)
		{
		kernel_dpack_tn_4_vs_lib4(m-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb, m-ii);
		}

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_4x12_tran_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_4x12_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lunn_1_left_4;
			}
		if(n-ii<=8)
			{
			goto lunn_1_left_8;
			}
		else
			{
			goto lunn_1_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_4x8_tran_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_4x8_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lunn_1_left_4;
			}
		else
			{
			goto lunn_1_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_4x4_tran_lib444c(m-jj, &alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_4x4_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto lunn_1_left_4;
		}
#endif
goto lunn_1_return;

#if defined(TARGET_X64_INTEL_HASWELL)
lunn_1_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_4x12_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunn_1_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
lunn_1_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_4x8_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunn_1_return;
#endif

lunn_1_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_4x4_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunn_1_return;

lunn_1_return:
	free(mem);
	return;


	// never to get here
	return;

	}



void blasfeo_hp_dtrmm_lltu(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{

#if defined(PRINT_NAME)
	printf("\nblasfeo_hp_dtrmm_lltu (cm) %d %d %f %p %d %d %p %d %d %p %d %d\n", m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
#endif

	if(m<=0 | n<=0)
		return;

	// extract pointer to column-major matrices from structures
	int lda = sA->m;
	int ldb = sB->m;
	int ldd = sD->m;
	double *A = sA->pA + ai + aj*lda;
	double *B = sB->pA + bi + bj*ldb;
	double *D = sD->pA + di + dj*ldb;

//	printf("\n%p %d %p %d %p %d\n", A, lda, B, ldb);

	int ii, jj;

	const int ps = 4; //D_PS;

#if defined(TARGET_GENERIC)
	double pd0[K_MAX_STACK];
#else
	ALIGNED( double pd0[K_MAX_STACK], 64 );
#endif

#if defined(TARGET_GENERIC)
	double pU0[M_KERNEL*K_MAX_STACK];
#else
	ALIGNED( double pU0[M_KERNEL*K_MAX_STACK], 64 );
#endif

	int k0;
	// TODO update if necessary !!!!!
//	if(*side=='l' | *side=='L')
		k0 = m;
//	else
//		k0 = n;

	int sdu0 = (k0+3)/4*4;
	sdu0 = sdu0<K_MAX_STACK ? sdu0 : K_MAX_STACK;

	struct blasfeo_pm_dmat tA, tB;
	int sda, sdb;
	int tA_size, tB_size;
	void *mem;
	char *mem_align;
	int m1, n1, k1;
	int pack_B;

	double *pU, *dA, *pB, *dB;
	int sdu;

	const int m_kernel = M_KERNEL;
	const int l1_cache_el = L1_CACHE_EL;
	const int reals_per_cache_line = CACHE_LINE_EL;

	const int m_cache = (m+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int n_cache = (n+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
//	const int k_cache = (k+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int m_kernel_cache = (m_kernel+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	int m_min = m_cache<m_kernel_cache ? m_cache : m_kernel_cache;
//	int n_min = n_cache<m_kernel_cache ? n_cache : m_kernel_cache;
	
	double d_0 = 0.0;


#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>=300 | n>=300 | n>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | n>=64 | n>K_MAX_STACK)
#else
	if(m>=12 | n>=12 | n>K_MAX_STACK)
#endif
		{
		goto lunu_1;
		}
	else
		{
		goto lltu_0;
		}

	// never to get here
	return;


lltu_0:
	pU = pU0;
	sdu = sdu0;

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nn_rl_one_4x12_tran_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nn_rl_one_4x12_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lltu_0_left_4;
			}
		if(n-ii<=8)
			{
			goto lltu_0_left_8;
			}
		else
			{
			goto lltu_0_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nn_rl_one_4x8_tran_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nn_rl_one_4x8_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lltu_0_left_4;
			}
		else
			{
			goto lltu_0_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nn_rl_one_4x4_tran_lib4c4c(m-jj, &alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nn_rl_one_4x4_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto lltu_0_left_4;
		}
#endif
	goto lltu_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
lltu_0_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nn_rl_one_4x12_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lltu_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
lltu_0_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nn_rl_one_4x8_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lltu_0_return;
#endif

lltu_0_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nn_rl_one_4x4_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lltu_0_return;

lltu_0_return:
	return;


lunu_1:
	m1 = (m+128-1)/128*128;
	tA_size = blasfeo_pm_memsize_dmat(ps, m_kernel, m1);
	tB_size = blasfeo_pm_memsize_dmat(ps, m1, m1);
	mem = malloc(tA_size+tB_size+64);
	blasfeo_align_64_byte(mem, (void **) &mem_align);
	blasfeo_pm_create_dmat(ps, m_kernel, m, &tA, (void *) mem_align);
	blasfeo_pm_create_dmat(ps, m, m, &tB, (void *) (mem_align+tA_size));

	pU = tA.pA;
	sdu = tA.cn;
	pB = tB.pA;
	sdb = tB.cn;

	// lower to upper
	for(ii=0; ii<m-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb);
		}
	if(ii<m)
		{
		kernel_dpack_tn_4_vs_lib4(m-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb, m-ii);
		}

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_4x12_tran_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_one_4x12_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lunu_1_left_4;
			}
		if(n-ii<=8)
			{
			goto lunu_1_left_8;
			}
		else
			{
			goto lunu_1_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_4x8_tran_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_one_4x8_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lunu_1_left_4;
			}
		else
			{
			goto lunu_1_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_4x4_tran_lib444c(m-jj, &alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_one_4x4_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto lunu_1_left_4;
		}
#endif
goto lunu_1_return;

#if defined(TARGET_X64_INTEL_HASWELL)
lunu_1_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_4x12_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunu_1_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
lunu_1_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_4x8_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunu_1_return;
#endif

lunu_1_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_4x4_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunu_1_return;

lunu_1_return:
	free(mem);
	return;


	// never to get here
	return;

	}



void blasfeo_hp_dtrmm_lunn(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{

#if defined(PRINT_NAME)
	printf("\nblasfeo_hp_dtrmm_lunn (cm) %d %d %f %p %d %d %p %d %d %p %d %d\n", m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
#endif

	if(m<=0 | n<=0)
		return;

	// extract pointer to column-major matrices from structures
	int lda = sA->m;
	int ldb = sB->m;
	int ldd = sD->m;
	double *A = sA->pA + ai + aj*lda;
	double *B = sB->pA + bi + bj*ldb;
	double *D = sD->pA + di + dj*ldb;

//	printf("\n%p %d %p %d %p %d\n", A, lda, B, ldb);

	int ii, jj;

	const int ps = 4; //D_PS;

#if defined(TARGET_GENERIC)
	double pd0[K_MAX_STACK];
#else
	ALIGNED( double pd0[K_MAX_STACK], 64 );
#endif

#if defined(TARGET_GENERIC)
	double pU0[M_KERNEL*K_MAX_STACK];
#else
	ALIGNED( double pU0[M_KERNEL*K_MAX_STACK], 64 );
#endif

	int k0;
	// TODO update if necessary !!!!!
//	if(*side=='l' | *side=='L')
		k0 = m;
//	else
//		k0 = n;

	int sdu0 = (k0+3)/4*4;
	sdu0 = sdu0<K_MAX_STACK ? sdu0 : K_MAX_STACK;

	struct blasfeo_pm_dmat tA, tB;
	int sda, sdb;
	int tA_size, tB_size;
	void *mem;
	char *mem_align;
	int m1, n1, k1;
	int pack_B;

	double *pU, *dA, *pB, *dB;
	int sdu;

	const int m_kernel = M_KERNEL;
	const int l1_cache_el = L1_CACHE_EL;
	const int reals_per_cache_line = CACHE_LINE_EL;

	const int m_cache = (m+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int n_cache = (n+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
//	const int k_cache = (k+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int m_kernel_cache = (m_kernel+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	int m_min = m_cache<m_kernel_cache ? m_cache : m_kernel_cache;
//	int n_min = n_cache<m_kernel_cache ? n_cache : m_kernel_cache;
	
	double d_0 = 0.0;


#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>=200 | n>=200 | n>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | n>=64 | n>K_MAX_STACK)
#else
	if(m>=12 | n>=12 | n>K_MAX_STACK)
#endif
		{
		goto lunn_1;
		}
	else
		{
		goto lunn_0;
		}

	// never to get here
	return;


lunn_0:
	pU = pU0;
	sdu = sdu0;

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_4x12_tran_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_4x12_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lunn_0_left_4;
			}
		if(n-ii<=8)
			{
			goto lunn_0_left_8;
			}
		else
			{
			goto lunn_0_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_4x8_tran_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_4x8_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lunn_0_left_4;
			}
		else
			{
			goto lunn_0_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_4x4_tran_lib4c4c(m-jj, &alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_4x4_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto lunn_0_left_4;
		}
#endif
goto lunn_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
lunn_0_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_4x12_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunn_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
lunn_0_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_4x8_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunn_0_return;
#endif

lunn_0_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_4x4_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunn_0_return;

lunn_0_return:
	return;



lunn_1:
	m1 = (m+128-1)/128*128;
	tA_size = blasfeo_pm_memsize_dmat(ps, m_kernel, m1);
	tB_size = blasfeo_pm_memsize_dmat(ps, m1, m1);
	mem = malloc(tA_size+tB_size+64);
	blasfeo_align_64_byte(mem, (void **) &mem_align);
	blasfeo_pm_create_dmat(ps, m_kernel, m, &tA, (void *) mem_align);
	blasfeo_pm_create_dmat(ps, m, m, &tB, (void *) (mem_align+tA_size));

	pU = tA.pA;
	sdu = tA.cn;
	pB = tB.pA;
	sdb = tB.cn;

	// upper to upper
	for(ii=0; ii<m-3; ii+=4)
		{
		kernel_dpack_tt_4_lib4(ii+4, A+ii*lda, lda, pB+ii*ps, sdb);
		}
	if(ii<m)
		{
		kernel_dpack_tt_4_vs_lib4(m, A+ii*lda, lda, pB+ii*ps, sdb, m-ii);
		}

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_4x12_tran_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_4x12_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lunn_1_left_4;
			}
		if(n-ii<=8)
			{
			goto lunn_1_left_8;
			}
		else
			{
			goto lunn_1_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_4x8_tran_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_4x8_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lunn_1_left_4;
			}
		else
			{
			goto lunn_1_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_4x4_tran_lib444c(m-jj, &alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_4x4_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto lunn_1_left_4;
		}
#endif
goto lunn_1_return;

#if defined(TARGET_X64_INTEL_HASWELL)
lunn_1_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_4x12_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunn_1_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
lunn_1_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_4x8_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunn_1_return;
#endif

lunn_1_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_4x4_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunn_1_return;

lunn_1_return:
	free(mem);
	return;


	// never to get here
	return;

	}



void blasfeo_hp_dtrmm_lunu(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{

#if defined(PRINT_NAME)
	printf("\nblasfeo_hp_dtrmm_lunu (cm) %d %d %f %p %d %d %p %d %d %p %d %d\n", m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
#endif

	if(m<=0 | n<=0)
		return;

	// extract pointer to column-major matrices from structures
	int lda = sA->m;
	int ldb = sB->m;
	int ldd = sD->m;
	double *A = sA->pA + ai + aj*lda;
	double *B = sB->pA + bi + bj*ldb;
	double *D = sD->pA + di + dj*ldb;

//	printf("\n%p %d %p %d %p %d\n", A, lda, B, ldb);

	int ii, jj;

	const int ps = 4; //D_PS;

#if defined(TARGET_GENERIC)
	double pd0[K_MAX_STACK];
#else
	ALIGNED( double pd0[K_MAX_STACK], 64 );
#endif

#if defined(TARGET_GENERIC)
	double pU0[M_KERNEL*K_MAX_STACK];
#else
	ALIGNED( double pU0[M_KERNEL*K_MAX_STACK], 64 );
#endif

	int k0;
	// TODO update if necessary !!!!!
//	if(*side=='l' | *side=='L')
		k0 = m;
//	else
//		k0 = n;

	int sdu0 = (k0+3)/4*4;
	sdu0 = sdu0<K_MAX_STACK ? sdu0 : K_MAX_STACK;

	struct blasfeo_pm_dmat tA, tB;
	int sda, sdb;
	int tA_size, tB_size;
	void *mem;
	char *mem_align;
	int m1, n1, k1;
	int pack_B;

	double *pU, *dA, *pB, *dB;
	int sdu;

	const int m_kernel = M_KERNEL;
	const int l1_cache_el = L1_CACHE_EL;
	const int reals_per_cache_line = CACHE_LINE_EL;

	const int m_cache = (m+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int n_cache = (n+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
//	const int k_cache = (k+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int m_kernel_cache = (m_kernel+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	int m_min = m_cache<m_kernel_cache ? m_cache : m_kernel_cache;
//	int n_min = n_cache<m_kernel_cache ? n_cache : m_kernel_cache;
	
	double d_0 = 0.0;


#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>=200 | n>=200 | n>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | n>=64 | n>K_MAX_STACK)
#else
	if(m>=12 | n>=12 | n>K_MAX_STACK)
#endif
		{
		goto lunu_1;
		}
	else
		{
		goto lunu_0;
		}

	// never to get here
	return;


lunu_0:
	pU = pU0;
	sdu = sdu0;

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_4x12_tran_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_one_4x12_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lunu_0_left_4;
			}
		if(n-ii<=8)
			{
			goto lunu_0_left_8;
			}
		else
			{
			goto lunu_0_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_4x8_tran_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_one_4x8_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lunu_0_left_4;
			}
		else
			{
			goto lunu_0_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_4x4_tran_lib4c4c(m-jj, &alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_one_4x4_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto lunu_0_left_4;
		}
#endif
goto lunu_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
lunu_0_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_4x12_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunu_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
lunu_0_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_4x8_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunu_0_return;
#endif

lunu_0_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_4x4_tran_vs_lib4c4c(m-jj, &alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunu_0_return;

lunu_0_return:
	return;



lunu_1:
	m1 = (m+128-1)/128*128;
	tA_size = blasfeo_pm_memsize_dmat(ps, m_kernel, m1);
	tB_size = blasfeo_pm_memsize_dmat(ps, m1, m1);
	mem = malloc(tA_size+tB_size+64);
	blasfeo_align_64_byte(mem, (void **) &mem_align);
	blasfeo_pm_create_dmat(ps, m_kernel, m, &tA, (void *) mem_align);
	blasfeo_pm_create_dmat(ps, m, m, &tB, (void *) (mem_align+tA_size));

	pU = tA.pA;
	sdu = tA.cn;
	pB = tB.pA;
	sdb = tB.cn;

	// upper to upper
	for(ii=0; ii<m-3; ii+=4)
		{
		kernel_dpack_tt_4_lib4(ii+4, A+ii*lda, lda, pB+ii*ps, sdb);
		}
	if(ii<m)
		{
		kernel_dpack_tt_4_vs_lib4(m, A+ii*lda, lda, pB+ii*ps, sdb, m-ii);
		}

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_4x12_tran_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_one_4x12_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lunu_1_left_4;
			}
		if(n-ii<=8)
			{
			goto lunu_1_left_8;
			}
		else
			{
			goto lunu_1_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_4x8_tran_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_one_4x8_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lunu_1_left_4;
			}
		else
			{
			goto lunu_1_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_4x4_tran_lib444c(m-jj, &alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_ru_one_4x4_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto lunu_1_left_4;
		}
#endif
goto lunu_1_return;

#if defined(TARGET_X64_INTEL_HASWELL)
lunu_1_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_4x12_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunu_1_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
lunu_1_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_4x8_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunu_1_return;
#endif

lunu_1_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_4x4_tran_vs_lib444c(m-jj, &alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lunu_1_return;

lunu_1_return:
	free(mem);
	return;


	// never to get here
	return;

	}



void blasfeo_hp_dtrmm_lutn(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{

#if defined(PRINT_NAME)
	printf("\nblasfeo_hp_dtrmm_lutn (cm) %d %d %f %p %d %d %p %d %d %p %d %d\n", m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
#endif

	if(m<=0 | n<=0)
		return;

	// extract pointer to column-major matrices from structures
	int lda = sA->m;
	int ldb = sB->m;
	int ldd = sD->m;
	double *A = sA->pA + ai + aj*lda;
	double *B = sB->pA + bi + bj*ldb;
	double *D = sD->pA + di + dj*ldb;

//	printf("\n%p %d %p %d %p %d\n", A, lda, B, ldb);

	int ii, jj;

	const int ps = 4; //D_PS;

#if defined(TARGET_GENERIC)
	double pd0[K_MAX_STACK];
#else
	ALIGNED( double pd0[K_MAX_STACK], 64 );
#endif

#if defined(TARGET_GENERIC)
	double pU0[M_KERNEL*K_MAX_STACK];
#else
	ALIGNED( double pU0[M_KERNEL*K_MAX_STACK], 64 );
#endif

	int k0;
	// TODO update if necessary !!!!!
//	if(*side=='l' | *side=='L')
		k0 = m;
//	else
//		k0 = n;

	int sdu0 = (k0+3)/4*4;
	sdu0 = sdu0<K_MAX_STACK ? sdu0 : K_MAX_STACK;

	struct blasfeo_pm_dmat tA, tB;
	int sda, sdb;
	int tA_size, tB_size;
	void *mem;
	char *mem_align;
	int m1, n1, k1;
	int pack_B;

	double *pU, *dA, *pB, *dB;
	int sdu;

	const int m_kernel = M_KERNEL;
	const int l1_cache_el = L1_CACHE_EL;
	const int reals_per_cache_line = CACHE_LINE_EL;

	const int m_cache = (m+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int n_cache = (n+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
//	const int k_cache = (k+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int m_kernel_cache = (m_kernel+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	int m_min = m_cache<m_kernel_cache ? m_cache : m_kernel_cache;
//	int n_min = n_cache<m_kernel_cache ? n_cache : m_kernel_cache;
	
	double d_0 = 0.0;


#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>=300 | n>=300 | n>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | n>=64 | n>K_MAX_STACK)
#else
	if(m>=12 | n>=12 | n>K_MAX_STACK)
#endif
		{
		goto llnn_1;
		}
	else
		{
		goto lutn_0;
		}

	// never to get here
	return;


lutn_0:
	pU = pU0;
	sdu = sdu0;

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nn_ru_4x12_tran_lib4c4c(jj, &alpha, pU, sdu, A+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nn_ru_4x12_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lutn_0_left_4;
			}
		if(n-ii<=8)
			{
			goto lutn_0_left_8;
			}
		else
			{
			goto lutn_0_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nn_ru_4x8_tran_lib4c4c(jj, &alpha, pU, sdu, A+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nn_ru_4x8_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lutn_0_left_4;
			}
		else
			{
			goto lutn_0_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nn_ru_4x4_tran_lib4c4c(jj, &alpha, pU, A+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nn_ru_4x4_tran_vs_lib4c4c(jj, &alpha, pU, A+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto lutn_0_left_4;
		}
#endif
goto lutn_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
lutn_0_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nn_ru_4x12_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lutn_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
lutn_0_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nn_ru_4x8_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lutn_0_return;
#endif

lutn_0_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nn_ru_4x4_tran_vs_lib4c4c(jj, &alpha, pU, A+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lutn_0_return;

lutn_0_return:
	return;


llnn_1:
	m1 = (m+128-1)/128*128;
	tA_size = blasfeo_pm_memsize_dmat(ps, m_kernel, m1);
	tB_size = blasfeo_pm_memsize_dmat(ps, m1, m1);
	mem = malloc(tA_size+tB_size+64);
	blasfeo_align_64_byte(mem, (void **) &mem_align);
	blasfeo_pm_create_dmat(ps, m_kernel, m, &tA, (void *) mem_align);
	blasfeo_pm_create_dmat(ps, m, m, &tB, (void *) (mem_align+tA_size));

	pU = tA.pA;
	sdu = tA.cn;
	pB = tB.pA;
	sdb = tB.cn;

	// upper to lower
	for(ii=0; ii<m-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(ii+4, A+ii*lda, lda, pB+ii*sdb);
		}
	if(ii<m)
		{
		kernel_dpack_tn_4_vs_lib4(m, A+ii*lda, lda, pB+ii*sdb, m-ii);
		}

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_4x12_tran_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_4x12_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto llnn_1_left_4;
			}
		else if(n-ii<=8)
			{
			goto llnn_1_left_8;
			}
		else
			{
			goto llnn_1_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_4x8_tran_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_4x8_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto llnn_1_left_4;
			}
		else
			{
			goto llnn_1_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_4x4_tran_lib444c(jj, &alpha, pU, pB+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_4x4_tran_vs_lib444c(jj, &alpha, pU, pB+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto llnn_1_left_4;
		}
#endif
goto llnn_1_return;

#if defined(TARGET_X64_INTEL_HASWELL)
llnn_1_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_4x12_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnn_1_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
llnn_1_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_4x8_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnn_1_return;
#endif

llnn_1_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_4x4_tran_vs_lib444c(jj, &alpha, pU, pB+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnn_1_return;

llnn_1_return:
	free(mem);
	return;


	// never to get here
	return;

	}



void blasfeo_hp_dtrmm_lutu(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{

#if defined(PRINT_NAME)
	printf("\nblasfeo_hp_dtrmm_lutu (cm) %d %d %f %p %d %d %p %d %d %p %d %d\n", m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
#endif

	if(m<=0 | n<=0)
		return;

	// extract pointer to column-major matrices from structures
	int lda = sA->m;
	int ldb = sB->m;
	int ldd = sD->m;
	double *A = sA->pA + ai + aj*lda;
	double *B = sB->pA + bi + bj*ldb;
	double *D = sD->pA + di + dj*ldb;

//	printf("\n%p %d %p %d %p %d\n", A, lda, B, ldb);

	int ii, jj;

	const int ps = 4; //D_PS;

#if defined(TARGET_GENERIC)
	double pd0[K_MAX_STACK];
#else
	ALIGNED( double pd0[K_MAX_STACK], 64 );
#endif

#if defined(TARGET_GENERIC)
	double pU0[M_KERNEL*K_MAX_STACK];
#else
	ALIGNED( double pU0[M_KERNEL*K_MAX_STACK], 64 );
#endif

	int k0;
	// TODO update if necessary !!!!!
//	if(*side=='l' | *side=='L')
		k0 = m;
//	else
//		k0 = n;

	int sdu0 = (k0+3)/4*4;
	sdu0 = sdu0<K_MAX_STACK ? sdu0 : K_MAX_STACK;

	struct blasfeo_pm_dmat tA, tB;
	int sda, sdb;
	int tA_size, tB_size;
	void *mem;
	char *mem_align;
	int m1, n1, k1;
	int pack_B;

	double *pU, *dA, *pB, *dB;
	int sdu;

	const int m_kernel = M_KERNEL;
	const int l1_cache_el = L1_CACHE_EL;
	const int reals_per_cache_line = CACHE_LINE_EL;

	const int m_cache = (m+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int n_cache = (n+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
//	const int k_cache = (k+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	const int m_kernel_cache = (m_kernel+reals_per_cache_line-1)/reals_per_cache_line*reals_per_cache_line;
	int m_min = m_cache<m_kernel_cache ? m_cache : m_kernel_cache;
//	int n_min = n_cache<m_kernel_cache ? n_cache : m_kernel_cache;
	
	double d_0 = 0.0;


#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>=300 | n>=300 | n>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | n>=64 | n>K_MAX_STACK)
#else
	if(m>=12 | n>=12 | n>K_MAX_STACK)
#endif
		{
		goto llnu_1;
		}
	else
		{
		goto lutu_0;
		}

	// never to get here
	return;


lutu_0:
	pU = pU0;
	sdu = sdu0;

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nn_ru_one_4x12_tran_lib4c4c(jj, &alpha, pU, sdu, A+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nn_ru_one_4x12_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lutu_0_left_4;
			}
		if(n-ii<=8)
			{
			goto lutu_0_left_8;
			}
		else
			{
			goto lutu_0_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nn_ru_one_4x8_tran_lib4c4c(jj, &alpha, pU, sdu, A+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nn_ru_one_4x8_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto lutu_0_left_4;
			}
		else
			{
			goto lutu_0_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nn_ru_one_4x4_tran_lib4c4c(jj, &alpha, pU, A+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nn_ru_one_4x4_tran_vs_lib4c4c(jj, &alpha, pU, A+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto lutu_0_left_4;
		}
#endif
goto lutu_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
lutu_0_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nn_ru_one_4x12_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lutu_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
lutu_0_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nn_ru_one_4x8_tran_vs_lib4c4c(jj, &alpha, pU, sdu, A+jj*lda, lda, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lutu_0_return;
#endif

lutu_0_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nn_ru_one_4x4_tran_vs_lib4c4c(jj, &alpha, pU, A+jj*lda, lda, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto lutu_0_return;

lutu_0_return:
	return;


llnu_1:
	m1 = (m+128-1)/128*128;
	tA_size = blasfeo_pm_memsize_dmat(ps, m_kernel, m1);
	tB_size = blasfeo_pm_memsize_dmat(ps, m1, m1);
	mem = malloc(tA_size+tB_size+64);
	blasfeo_align_64_byte(mem, (void **) &mem_align);
	blasfeo_pm_create_dmat(ps, m_kernel, m, &tA, (void *) mem_align);
	blasfeo_pm_create_dmat(ps, m, m, &tB, (void *) (mem_align+tA_size));

	pU = tA.pA;
	sdu = tA.cn;
	pB = tB.pA;
	sdb = tB.cn;

	// upper to lower
	for(ii=0; ii<m-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(ii+4, A+ii*lda, lda, pB+ii*sdb);
		}
	if(ii<m)
		{
		kernel_dpack_tn_4_vs_lib4(m, A+ii*lda, lda, pB+ii*sdb, m-ii);
		}

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<n-11; ii+=12)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu);
		kernel_dpack_tn_4_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_one_4x12_tran_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_one_4x12_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto llnu_1_left_4;
			}
		else if(n-ii<=8)
			{
			goto llnu_1_left_8;
			}
		else
			{
			goto llnu_1_left_12;
			}
		}
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<n-7; ii+=8)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		kernel_dpack_tn_4_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_one_4x8_tran_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_one_4x8_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		if(n-ii<=4)
			{
			goto llnu_1_left_4;
			}
		else
			{
			goto llnu_1_left_8;
			}
		}
#else
	for(; ii<n-3; ii+=4)
		{
		kernel_dpack_tn_4_lib4(m, B+ii*ldb, ldb, pU);
		for(jj=0; jj<m-3; jj+=4)
			{
			kernel_dtrmm_nt_rl_one_4x4_tran_lib444c(jj, &alpha, pU, pB+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd);
			}
		if(jj<m)
			{
			kernel_dtrmm_nt_rl_one_4x4_tran_vs_lib444c(jj, &alpha, pU, pB+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
			}
		}
	if(ii<n)
		{
		goto llnu_1_left_4;
		}
#endif
goto llnu_1_return;

#if defined(TARGET_X64_INTEL_HASWELL)
llnu_1_left_12:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+4*sdu, n-(ii+4));
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+8)*ldb, ldb, pU+8*sdu, n-(ii+8));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_one_4x12_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnu_1_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) //| defined(TARGET_X64_INTEL_SANDY_BRIDGE)
llnu_1_left_8:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	kernel_dpack_tn_4_vs_lib4(m, B+(ii+4)*ldb, ldb, pU+ps*sdu, n-(ii+4));
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_one_4x8_tran_vs_lib444c(jj, &alpha, pU, sdu, pB+jj*sdb, &d_0, pU+jj*ps, sdu, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnu_1_return;
#endif

llnu_1_left_4:
	kernel_dpack_tn_4_vs_lib4(m, B+ii*ldb, ldb, pU, n-ii);
	for(jj=0; jj<m; jj+=4)
		{
		kernel_dtrmm_nt_rl_one_4x4_tran_vs_lib444c(jj, &alpha, pU, pB+jj*sdb, &d_0, pU+jj*ps, D+jj+ii*ldd, ldd, m-jj, n-ii);
		}
goto llnu_1_return;

llnu_1_return:
	free(mem);
	return;

	// never to get here
	return;

	}



#if defined(LA_HIGH_PERFORMANCE)



void blasfeo_dtrmm_llnn(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{
	blasfeo_hp_dtrmm_llnn(m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
	return;
	}



void blasfeo_dtrmm_llnu(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{
	blasfeo_hp_dtrmm_llnu(m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
	return;
	}



void blasfeo_dtrmm_lltn(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{
	blasfeo_hp_dtrmm_lltn(m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
	return;
	}



void blasfeo_dtrmm_lltu(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{
	blasfeo_hp_dtrmm_lltu(m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
	return;
	}



void blasfeo_dtrmm_lunn(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{
	blasfeo_hp_dtrmm_lunn(m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
	return;
	}



void blasfeo_dtrmm_lunu(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{
	blasfeo_hp_dtrmm_lunu(m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
	return;
	}



void blasfeo_dtrmm_lutn(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{
	blasfeo_hp_dtrmm_lutn(m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
	return;
	}



void blasfeo_dtrmm_lutu(int m, int n, double alpha, struct blasfeo_dmat *sA, int ai, int aj, struct blasfeo_dmat *sB, int bi, int bj, struct blasfeo_dmat *sD, int di, int dj)
	{
	blasfeo_hp_dtrmm_lutu(m, n, alpha, sA, ai, aj, sB, bi, bj, sD, di, dj);
	return;
	}



#endif

