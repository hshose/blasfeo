/**************************************************************************************************
*                                                                                                 *
* This file is part of BLASFEO.                                                                   *
*                                                                                                 *
* BLASFEO -- BLAS For Embedded Optimization.                                                      *
* Copyright (C) 2016-2018 by Gianluca Frison.                                                     *
* Developed at IMTEK (University of Freiburg) under the supervision of Moritz Diehl.              *
* All rights reserved.                                                                            *
*                                                                                                 *
* This program is free software: you can redistribute it and/or modify                            *
* it under the terms of the GNU General Public License as published by                            *
* the Free Software Foundation, either version 3 of the License, or                               *
* (at your option) any later version                                                              *.
*                                                                                                 *
* This program is distributed in the hope that it will be useful,                                 *
* but WITHOUT ANY WARRANTY; without even the implied warranty of                                  *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                                   *
* GNU General Public License for more details.                                                    *
*                                                                                                 *
* You should have received a copy of the GNU General Public License                               *
* along with this program.  If not, see <https://www.gnu.org/licenses/>.                          *
*                                                                                                 *
* The authors designate this particular file as subject to the "Classpath" exception              *
* as provided by the authors in the LICENSE file that accompained this code.                      *
*                                                                                                 *
* Author: Gianluca Frison, gianluca.frison (at) imtek.uni-freiburg.de                             *
*                                                                                                 *
**************************************************************************************************/

#include <stdlib.h>
#include <stdio.h>

#include "../include/blasfeo_target.h"
#include "../include/blasfeo_common.h"
#include "../include/blasfeo_d_aux.h"
#include "../include/blasfeo_d_kernel.h"



#if defined(FORTRAN_BLAS_API)
#define blasfeo_dtrmm dtrmm_
#endif



#if defined(FALLBACK_TO_EXT_BLAS)
void dtrmv_(char *uplo, char *transa, char *diag, int *m, double *A, int *lda, double *x, int *incx);
void dscal_(int *m, double *alpha, double *x, int *incx);
#endif



void blasfeo_dtrmm(char *side, char *uplo, char *transa, char *diag, int *pm, int *pn, double *alpha, double *A, int *plda, double *B, int *pldb)
	{

#if defined(PRINT_NAME)
	printf("\nblasfeo_dtrmm %c %c %c %c %d %d %f %p %d %p %d\n", *side, *uplo, *transa, *diag, *pm, *pn, *alpha, A, *plda, B, *pldb);
#endif

	int m = *pm;
	int n = *pn;
	int lda = *plda;
	int ldb = *pldb;

#if defined(DIM_CHECK)
	if( !(*side=='l' | *side=='L' | *side=='r' | *side=='R') )
		{
		printf("\nBLASFEO: dtrmm: wrong value for side\n");
		return;
		}
	if( !(*uplo=='l' | *uplo=='L' | *uplo=='u' | *uplo=='U') )
		{
		printf("\nBLASFEO: dtrmm: wrong value for uplo\n");
		return;
		}
	if( !(*transa=='c' | *transa=='C' | *transa=='n' | *transa=='N' | *transa=='t' | *transa=='T') )
		{
		printf("\nBLASFEO: dtrmm: wrong value for transa\n");
		return;
		}
	if( !(*diag=='n' | *diag=='n' | *diag=='u' | *diag=='U') )
		{
		printf("\nBLASFEO: dtrmm: wrong value for diag\n");
		return;
		}
#endif

	char c_n = 'n';
	char c_t = 't';
	int i_1 = 1;

#if defined(FALLBACK_TO_EXT_BLAS)
	// fallback to dtrmv if B is a vector
	if(n==1 & (*side=='l' | *side=='L'))
		{
		dtrmv_(uplo, transa, diag, pm, A, plda, B, &i_1);
		if(*alpha!=1.0)
			{
			dscal_(pm, alpha, B, &i_1);
			}
		return;
		}
	else if(m==1 & (*side=='r' | *side=='r'))
		{
		if(*transa=='n' | *transa=='N')
			{
			dtrmv_(uplo, &c_t, diag, pn, A, plda, B, pldb);
			}
		else
			{
			dtrmv_(uplo, &c_n, diag, pn, A, plda, B, pldb);
			}
		if(*alpha!=1.0)
			{
			dscal_(pn, alpha, B, pldb);
			}
		return;
		}
#endif

	double d_0 = 0.0;

	int ii, jj;

	int ps = 4;

	if(m<=0 | n<=0)
		return;

// TODO visual studio alignment
#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_ARMV8A_ARM_CORTEX_A53)
	double pU0[3*4*K_MAX_STACK] __attribute__ ((aligned (64)));
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE) | defined(TARGET_ARMV8A_ARM_CORTEX_A57)
	double pU0[2*4*K_MAX_STACK] __attribute__ ((aligned (64)));
#elif defined(TARGET_GENERIC)
	double pU0[1*4*K_MAX_STACK];
#else
	double pU0[1*4*K_MAX_STACK] __attribute__ ((aligned (64)));
#endif

	int k0;
	// TODO update if necessary !!!!!
	if(*side=='l' | *side=='L')
		k0 = m;
	else
		k0 = n;

	int sdu0 = (k0+3)/4*4;
	sdu0 = sdu0<K_MAX_STACK ? sdu0 : K_MAX_STACK;

	struct blasfeo_dmat sA, sB;
	double *pU, *pB, *dA, *dB;
	int sda, sdb, sdu;
	int sA_size, sB_size;
	void *mem, *mem_align;
	int m1, n1;
	int pack_tran = 0;

	if(*side=='l' | *side=='L') // _l
		{
		if(*uplo=='l' | *uplo=='L') // _ll
			{
			if(*transa=='n' | *transa=='N') // _lln
				{
				if(*diag=='n' | *diag=='N') // _llnn
					{
					printf("\nBLASFEO: dtrmm_llnn: not implemented yet\n");
					return;
					}
				else //if(*diag=='u' | *diag=='U') // _llnu
					{
					printf("\nBLASFEO: dtrmm_llnu: not implemented yet\n");
					return;
					}
				}
			else //if(*transa=='t' | *transa=='T' | *transa=='c' | *transa=='C') // _llt
				{
				if(*diag=='n' | *diag=='N') // _lltn
					{
					printf("\nBLASFEO: dtrmm_lltn: not implemented yet\n");
					return;
					}
				else //if(*diag=='u' | *diag=='U') // _lltu
					{
					printf("\nBLASFEO: dtrmm_lltu: not implemented yet\n");
					return;
					}
				}
			}
		else if(*uplo=='u' | *uplo=='U') // _lu
			{
			if(*transa=='n' | *transa=='N') // _lun
				{
				if(*diag=='n' | *diag=='N') // _lunn
					{
					printf("\nBLASFEO: dtrmm_lunn: not implemented yet\n");
					return;
					}
				else //if(*diag=='u' | *diag=='U') // _lunu
					{
					printf("\nBLASFEO: dtrmm_lunu: not implemented yet\n");
					return;
					}
				}
			else //if(*transa=='t' | *transa=='T' | *transa=='c' | *transa=='C') // _lut
				{
				if(*diag=='n' | *diag=='N') // _lutn
					{
					printf("\nBLASFEO: dtrmm_lutn: not implemented yet\n");
					return;
					}
				else //if(*diag=='u' | *diag=='U') // _lutu
					{
					printf("\nBLASFEO: dtrmm_lutu: not implemented yet\n");
					return;
					}
				}
			}
		}
	else if(*side=='r' | *side=='R') // _r
		{
		if(*uplo=='l' | *uplo=='L') // _rl
			{
			if(*transa=='n' | *transa=='N') // _rln
				{
				if(*diag=='n' | *diag=='N') // _rlnn
					{
					goto rlnn;
					}
				else //if(*diag=='u' | *diag=='U') // _rlnu
					{
					goto rlnu;
					}
				}
			else //if(*transa=='t' | *transa=='T' | *transa=='c' | *transa=='C') // _rlt
				{
				if(*diag=='n' | *diag=='N') // _rltn
					{
					printf("\nBLASFEO: dtrmm_rltn: not implemented yet\n");
					return;
					}
				else //if(*diag=='u' | *diag=='U') // _rltu
					{
					printf("\nBLASFEO: dtrmm_rltu: not implemented yet\n");
					return;
					}
				}
			}
		else if(*uplo=='u' | *uplo=='U') // _ru
			{
			if(*transa=='n' | *transa=='N') // _run
				{
				if(*diag=='n' | *diag=='N') // _runn
					{
					goto runn;
//					printf("\nBLASFEO: dtrmm_runn: not implemented yet\n");
//					return;
					}
				else //if(*diag=='u' | *diag=='U') // _runu
					{
					goto runu;
//					printf("\nBLASFEO: dtrmm_runu: not implemented yet\n");
//					return;
					}
				}
			else //if(*transa=='t' | *transa=='T' | *transa=='c' | *transa=='C') // _rut
				{
				if(*diag=='n' | *diag=='N') // _rutn
					{
					goto rutn;
					}
				else //if(*diag=='u' | *diag=='U') // _rutu
					{
					goto rutu;
					}
				}
			}
		}



/************************************************
* rlnn
************************************************/
rlnn:
#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>300 | n>300 | n>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | n>=64 | n>K_MAX_STACK)
#else
	if(m>=12 | n>=12 | n>K_MAX_STACK)
#endif
		{
		pack_tran = 1;
		goto rutn_1;
		}
	else
		{
		goto rlnn_0;
		}

rlnn_0:
	pU = pU0;
	sdu = sdu0;

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<m-11; ii+=12)
		{
		kernel_dpack_nn_12_lib4(n, B+ii, ldb, pU, sdu);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nn_rl_12x4_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nn_rl_12x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto rlnn_0_left_4;
			}
		if(m-ii<=8)
			{
			goto rlnn_0_left_8;
			}
		else
			{
			goto rlnn_0_left_12;
			}
		}
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<m-7; ii+=8)
		{
		kernel_dpack_nn_8_lib4(n, B+ii, ldb, pU, sdu);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nn_rl_8x4_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nn_rl_8x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto rlnn_0_left_4;
			}
		else
			{
			goto rlnn_0_left_8;
			}
		}
#else
	for(; ii<m-3; ii+=4)
		{
		kernel_dpack_nn_4_lib4(n, B+ii, ldb, pU);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nn_rl_4x4_lib4cc(n-jj, alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nn_rl_4x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		goto rlnn_0_left_4;
		}
#endif
	goto rlnn_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
rlnn_0_left_12:
	kernel_dpack_nn_12_lib4(n, B+ii, ldb, pU, sdu);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nn_rl_12x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rlnn_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_X64_INTEL_SANDY_BRIDGE)
rlnn_0_left_8:
	kernel_dpack_nn_8_vs_lib4(n, B+ii, ldb, pU, sdu, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nn_rl_8x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rlnn_0_return;
#endif

rlnn_0_left_4:
	kernel_dpack_nn_4_vs_lib4(n, B+ii, ldb, pU, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nn_rl_4x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rlnn_0_return;

rlnn_0_return:
	return;



/************************************************
* rlnu
************************************************/
rlnu:
#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>300 | n>300 | n>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | n>=64 | n>K_MAX_STACK)
#else
	if(m>=12 | n>=12 | n>K_MAX_STACK)
#endif
		{
		pack_tran = 1;
		goto rutu_1;
		}
	else
		{
		goto rlnu_0;
		}

rlnu_0:
	pU = pU0;
	sdu = sdu0;

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<m-11; ii+=12)
		{
		kernel_dpack_nn_12_lib4(n, B+ii, ldb, pU, sdu);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nn_rl_one_12x4_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nn_rl_one_12x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto rlnu_0_left_4;
			}
		if(m-ii<=8)
			{
			goto rlnu_0_left_8;
			}
		else
			{
			goto rlnu_0_left_12;
			}
		}
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<m-7; ii+=8)
		{
		kernel_dpack_nn_8_lib4(n, B+ii, ldb, pU, sdu);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nn_rl_one_8x4_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nn_rl_one_8x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto rlnu_0_left_4;
			}
		else
			{
			goto rlnu_0_left_8;
			}
		}
#else
	for(; ii<m-3; ii+=4)
		{
		kernel_dpack_nn_4_lib4(n, B+ii, ldb, pU);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nn_rl_one_4x4_lib4cc(n-jj, alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nn_rl_one_4x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		goto rlnu_0_left_4;
		}
#endif
	goto rlnu_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
rlnu_0_left_12:
	kernel_dpack_nn_12_lib4(n, B+ii, ldb, pU, sdu);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nn_rl_one_12x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rlnu_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_X64_INTEL_SANDY_BRIDGE)
rlnu_0_left_8:
	kernel_dpack_nn_8_vs_lib4(n, B+ii, ldb, pU, sdu, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nn_rl_one_8x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rlnu_0_return;
#endif

rlnu_0_left_4:
	kernel_dpack_nn_4_vs_lib4(n, B+ii, ldb, pU, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nn_rl_one_4x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rlnu_0_return;

rlnu_0_return:
	return;



/************************************************
* runn
************************************************/
runn:
#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>=256 | n>=256 | n>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | n>=64 | n>K_MAX_STACK)
#else
	if(m>=12 | n>=12 | n>K_MAX_STACK)
#endif
		{
// TODO
//		pack_tran = 0;
//		goto rltn_1;
		}
	else
		{
		goto runn_0;
		}

runn_0:
	pU = pU0;
	sdu = sdu0;

	ii = 0;
#if 0//defined(TARGET_X64_INTEL_HASWELL)
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
#else
	for(; ii<m-3; ii+=4)
		{
		kernel_dpack_nn_4_lib4(n, B+ii, ldb, pU);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nn_ru_4x4_lib4cc(jj, alpha, pU, A+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nn_ru_4x4_vs_lib4cc(jj, alpha, pU, A+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		goto runn_0_left_4;
		}
#endif
goto runn_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
runn_0_left_12:
goto runn_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_X64_INTEL_SANDY_BRIDGE)
runn_0_left_8:
goto runn_0_return;
#endif

runn_0_left_4:
	kernel_dpack_nn_4_vs_lib4(n, B+ii, ldb, pU, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nn_ru_4x4_vs_lib4cc(jj, alpha, pU, A+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto runn_0_return;

runn_0_return:
	return;



/************************************************
* runu
************************************************/
runu:
#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>=256 | n>=256 | n>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | n>=64 | n>K_MAX_STACK)
#else
	if(m>=12 | n>=12 | n>K_MAX_STACK)
#endif
		{
// TODO
//		pack_tran = 0;
//		goto rltu_1;
		}
	else
		{
		goto runu_0;
		}

runu_0:
	pU = pU0;
	sdu = sdu0;

	ii = 0;
#if 0//defined(TARGET_X64_INTEL_HASWELL)
#elif 0//defined(TARGET_X64_INTEL_SANDY_BRIDGE)
#else
	for(; ii<m-3; ii+=4)
		{
		kernel_dpack_nn_4_lib4(n, B+ii, ldb, pU);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nn_ru_one_4x4_lib4cc(jj, alpha, pU, A+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nn_ru_one_4x4_vs_lib4cc(jj, alpha, pU, A+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		goto runu_0_left_4;
		}
#endif
goto runu_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
runu_0_left_12:
goto runu_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_X64_INTEL_SANDY_BRIDGE)
runu_0_left_8:
goto runu_0_return;
#endif

runu_0_left_4:
	kernel_dpack_nn_4_vs_lib4(n, B+ii, ldb, pU, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nn_ru_one_4x4_vs_lib4cc(jj, alpha, pU, A+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto runu_0_return;

runu_0_return:
	return;



/************************************************
* rutn
************************************************/
rutn:
#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>=128 | n>=128 | n>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | n>=64 | n>K_MAX_STACK)
#else
	if(m>=12 | n>=12 | n>K_MAX_STACK)
#endif
		{
		pack_tran = 0;
		goto rutn_1;
		}
	else
		{
		goto rutn_0;
		}

rutn_0:
	pU = pU0;
	sdu = sdu0;

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<m-11; ii+=12)
		{
		kernel_dpack_nn_12_lib4(n, B+ii, ldb, pU, sdu);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_12x4_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nt_ru_12x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto rutn_0_left_4;
			}
		if(m-ii<=8)
			{
			goto rutn_0_left_8;
			}
		else
			{
			goto rutn_0_left_12;
			}
		}
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<m-7; ii+=8)
		{
		kernel_dpack_nn_8_lib4(n, B+ii, ldb, pU, sdu);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_8x4_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nt_ru_8x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto rutn_0_left_4;
			}
		else
			{
			goto rutn_0_left_8;
			}
		}
#else
	for(; ii<m-3; ii+=4)
		{
		kernel_dpack_nn_4_lib4(n, B+ii, ldb, pU);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_4x4_lib4cc(n-jj, alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nt_ru_4x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		goto rutn_0_left_4;
		}
#endif
goto rutn_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
rutn_0_left_12:
	kernel_dpack_nn_12_vs_lib4(n, B+ii, ldb, pU, sdu, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nt_ru_12x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rutn_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_X64_INTEL_SANDY_BRIDGE)
rutn_0_left_8:
	kernel_dpack_nn_8_vs_lib4(n, B+ii, ldb, pU, sdu, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nt_ru_8x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rutn_0_return;
#endif

rutn_0_left_4:
	kernel_dpack_nn_4_vs_lib4(n, B+ii, ldb, pU, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nt_ru_4x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rutn_0_return;

rutn_0_return:
	return;



rutn_1:
	n1 = (n+128-1)/128*128;
	sA_size = blasfeo_memsize_dmat(12, n1);
	sB_size = blasfeo_memsize_dmat(n1, n1);
	mem = malloc(sA_size+sB_size+64);
	blasfeo_align_64_byte(mem, &mem_align);
	blasfeo_create_dmat(12, n, &sA, mem_align);
	blasfeo_create_dmat(n, n, &sB, mem_align+sA_size);

	pU = sA.pA;
	sdu = sA.cn;
	pB = sB.pA;
	sdb = sB.cn;

	if(pack_tran) // lower to upper
		{
		for(ii=0; ii<n-3; ii+=4)
			{
			kernel_dpack_tn_4_lib4(n-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb);
			}
		if(ii<n)
			{
			kernel_dpack_tn_4_vs_lib4(n-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb, n-ii);
			}
		}
	else // upper to upper
		{
		for(ii=0; ii<n-3; ii+=4)
			{
			kernel_dpack_nn_4_lib4(m-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb);
			}
		if(ii<n)
			{
			kernel_dpack_nn_4_vs_lib4(m-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb, n-ii);
			}
		}

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<m-11; ii+=12)
		{
		kernel_dpack_nn_12_lib4(n, B+ii, ldb, pU, sdu);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_12x4_lib44c(n-jj, alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nt_ru_12x4_vs_lib44c(n-jj, alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto rutn_1_left_4;
			}
		if(m-ii<=8)
			{
			goto rutn_1_left_8;
			}
		else
			{
			goto rutn_1_left_12;
			}
		}
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<m-7; ii+=8)
		{
		kernel_dpack_nn_8_lib4(n, B+ii, ldb, pU, sdu);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_8x4_lib44c(n-jj, alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nt_ru_8x4_vs_lib44c(n-jj, alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto rutn_1_left_4;
			}
		else
			{
			goto rutn_1_left_8;
			}
		}
#else
	for(; ii<m-3; ii+=4)
		{
		kernel_dpack_nn_4_lib4(n, B+ii, ldb, pU);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_4x4_lib44c(n-jj, alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nt_ru_4x4_vs_lib44c(n-jj, alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		goto rutn_1_left_4;
		}
#endif
goto rutn_1_return;

#if defined(TARGET_X64_INTEL_HASWELL)
rutn_1_left_12:
	kernel_dpack_nn_12_vs_lib4(n, B+ii, ldb, pU, sdu, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nt_ru_12x4_vs_lib44c(n-jj, alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rutn_1_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_X64_INTEL_SANDY_BRIDGE)
rutn_1_left_8:
	kernel_dpack_nn_8_vs_lib4(n, B+ii, ldb, pU, sdu, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nt_ru_8x4_vs_lib44c(n-jj, alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rutn_1_return;
#endif

rutn_1_left_4:
	kernel_dpack_nn_4_vs_lib4(n, B+ii, ldb, pU, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nt_ru_4x4_vs_lib44c(n-jj, alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rutn_1_return;

rutn_1_return:
	free(mem);
	return;



/************************************************
* rutu
************************************************/
rutu:
#if defined(TARGET_X64_INTEL_HASWELL)
	if(m>=128 | n>=128 | n>K_MAX_STACK)
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	if(m>=64 | n>=64 | n>K_MAX_STACK)
#else
	if(m>=12 | n>=12 | n>K_MAX_STACK)
#endif
		{
		pack_tran = 0;
		goto rutu_1;
		}
	else
		{
		goto rutu_0;
		}

rutu_0:
	pU = pU0;
	sdu = sdu0;

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<m-11; ii+=12)
		{
		kernel_dpack_nn_12_lib4(n, B+ii, ldb, pU, sdu);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_12x4_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nt_ru_one_12x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto rutu_0_left_4;
			}
		if(m-ii<=8)
			{
			goto rutu_0_left_8;
			}
		else
			{
			goto rutu_0_left_12;
			}
		}
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<m-7; ii+=8)
		{
		kernel_dpack_nn_8_lib4(n, B+ii, ldb, pU, sdu);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_8x4_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nt_ru_one_8x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto rutu_0_left_4;
			}
		else
			{
			goto rutu_0_left_8;
			}
		}
#else
	for(; ii<m-3; ii+=4)
		{
		kernel_dpack_nn_4_lib4(n, B+ii, ldb, pU);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_4x4_lib4cc(n-jj, alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nt_ru_one_4x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		goto rutu_0_left_4;
		}
#endif
goto rutu_0_return;

#if defined(TARGET_X64_INTEL_HASWELL)
rutu_0_left_12:
	kernel_dpack_nn_12_vs_lib4(n, B+ii, ldb, pU, sdu, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_12x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rutu_0_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_X64_INTEL_SANDY_BRIDGE)
rutu_0_left_8:
	kernel_dpack_nn_8_vs_lib4(n, B+ii, ldb, pU, sdu, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_8x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, sdu, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rutu_0_return;
#endif

rutu_0_left_4:
	kernel_dpack_nn_4_vs_lib4(n, B+ii, ldb, pU, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_4x4_vs_lib4cc(n-jj, alpha, pU+jj*ps, A+jj+jj*lda, lda, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rutu_0_return;

rutu_0_return:
	return;



rutu_1:
	n1 = (n+128-1)/128*128;
	sA_size = blasfeo_memsize_dmat(12, n1);
	sB_size = blasfeo_memsize_dmat(n1, n1);
	mem = malloc(sA_size+sB_size+64);
	blasfeo_align_64_byte(mem, &mem_align);
	blasfeo_create_dmat(12, n, &sA, mem_align);
	blasfeo_create_dmat(n, n, &sB, mem_align+sA_size);

	pU = sA.pA;
	sdu = sA.cn;
	pB = sB.pA;
	sdb = sB.cn;

	if(pack_tran) // lower to upper
		{
		for(ii=0; ii<n-3; ii+=4)
			{
			kernel_dpack_tn_4_lib4(n-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb);
			}
		if(ii<n)
			{
			kernel_dpack_tn_4_vs_lib4(n-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb, n-ii);
			}
		}
	else // upper to upper
		{
		for(ii=0; ii<n-3; ii+=4)
			{
			kernel_dpack_nn_4_lib4(m-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb);
			}
		if(ii<n)
			{
			kernel_dpack_nn_4_vs_lib4(m-ii, A+ii+ii*lda, lda, pB+ii*ps+ii*sdb, n-ii);
			}
		}

	ii = 0;
#if defined(TARGET_X64_INTEL_HASWELL)
	for(; ii<m-11; ii+=12)
		{
		kernel_dpack_nn_12_lib4(n, B+ii, ldb, pU, sdu);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_12x4_lib44c(n-jj, alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nt_ru_one_12x4_vs_lib44c(n-jj, alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto rutu_1_left_4;
			}
		if(m-ii<=8)
			{
			goto rutu_1_left_8;
			}
		else
			{
			goto rutu_1_left_12;
			}
		}
#elif defined(TARGET_X64_INTEL_SANDY_BRIDGE)
	for(; ii<m-7; ii+=8)
		{
		kernel_dpack_nn_8_lib4(n, B+ii, ldb, pU, sdu);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_8x4_lib44c(n-jj, alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nt_ru_one_8x4_vs_lib44c(n-jj, alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		if(m-ii<=4)
			{
			goto rutu_1_left_4;
			}
		else
			{
			goto rutu_1_left_8;
			}
		}
#else
	for(; ii<m-3; ii+=4)
		{
		kernel_dpack_nn_4_lib4(n, B+ii, ldb, pU);
		for(jj=0; jj<n-3; jj+=4)
			{
			kernel_dtrmm_nt_ru_one_4x4_lib44c(n-jj, alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb);
			}
		if(jj<n)
			{
			kernel_dtrmm_nt_ru_one_4x4_vs_lib44c(n-jj, alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
			}
		}
	if(ii<m)
		{
		goto rutu_1_left_4;
		}
#endif
goto rutu_1_return;

#if defined(TARGET_X64_INTEL_HASWELL)
rutu_1_left_12:
	kernel_dpack_nn_12_vs_lib4(n, B+ii, ldb, pU, sdu, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_12x4_vs_lib44c(n-jj, alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rutu_1_return;
#endif

#if defined(TARGET_X64_INTEL_HASWELL) | defined(TARGET_X64_INTEL_SANDY_BRIDGE)
rutu_1_left_8:
	kernel_dpack_nn_8_vs_lib4(n, B+ii, ldb, pU, sdu, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_8x4_vs_lib44c(n-jj, alpha, pU+jj*ps, sdu, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rutu_1_return;
#endif

rutu_1_left_4:
	kernel_dpack_nn_4_vs_lib4(n, B+ii, ldb, pU, m-ii);
	for(jj=0; jj<n; jj+=4)
		{
		kernel_dtrmm_nt_ru_one_4x4_vs_lib44c(n-jj, alpha, pU+jj*ps, pB+jj*ps+jj*sdb, &d_0, B+ii+jj*ldb, ldb, B+ii+jj*ldb, ldb, m-ii, n-jj);
		}
goto rutu_1_return;

rutu_1_return:
	free(mem);
	return;

	}



