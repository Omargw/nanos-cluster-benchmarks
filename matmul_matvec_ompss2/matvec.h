/*
 * Copyright (C) 2019  Jimmy Aguilar Mena
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MATVEC_H
#define MATVEC_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NANOS6
#include <nanos6/debug.h>
#endif

#include "cmacros/macros.h"

	double *alloc_init(const size_t rows, size_t cols, size_t TS)
	{
		myassert(rows >= TS);
		modcheck(rows, TS);

		const size_t size = cols * rows;

		double *ret =
			(double *) nanos6_dmalloc(size * sizeof(double),
			                          nanos6_equpart_distribution, 0, NULL);
		myassert (ret);

		const size_t piece = cols * TS;

		for (size_t i = 0; i < rows; i += TS) {
			double *first = &ret[i * cols];

			#pragma oss task out(first[0; piece]) label("initalize_vector")
			{
				struct drand48_data drand_buf;
				srand48_r(i, &drand_buf);
				double x;

				for (size_t j = 0; j < piece; ++j) {
					drand48_r(&drand_buf, &x);
					first[j] = x;
				}
			}
		}

		return ret;
	}

	void free_matrix(double *mat, size_t size)
	{
		nanos6_dfree(mat, size * sizeof(double));
	}


	void matmul_base(const double *A, const double *B, double * const C,
	                 size_t lrowsA, size_t dim, size_t colsBC)
	{
		for (size_t i = 0; i < lrowsA; ++i) {
			for (size_t k = 0; k < colsBC; ++k)
				C[i * colsBC + k] = 0.0;

			for (size_t j = 0; j < dim; ++j) {
				const double temp = A[i * dim + j];

				for (size_t k = 0; k < colsBC; ++k) {
					C[i * colsBC + k] += (temp * B[j * colsBC + k]);
				}
			}
		}
	}

	void __print_task(const double * const mat,
	                  const size_t rows, const size_t cols,
	                  const char prefix[64], const char name[64])
	{
		#pragma oss task in(mat[0; rows * cols]) label("matrix_print")
		{
			__print(mat, rows, cols, prefix, name);
		}
	}

#define printmatrix_task(mat, rows, cols, prefix) \
	__print_task(mat, rows, cols, prefix, #mat)


	bool validate(const double *A, const double *B, double *C,
	              size_t dim, size_t colsBC)
	{
		bool success = true;

		#pragma oss task in(A[0; dim * dim])			\
			in(B[0; dim * colsBC])						\
			in(C[0; dim * colsBC])						\
			inout(success) label("validate")
		{
			success = true;

			double *expected = (double *) malloc(dim * colsBC * sizeof(double));
			matmul_base(A, B, expected, dim, dim, colsBC);

			for (size_t i = 0; i < dim; ++i) {
				for (size_t j = 0; j < colsBC; ++j) {

					if (abs(C[i * colsBC + j] - expected[i * colsBC + j]) > 1e-12) {
						success = false;
						break;
					}
				}
			}

			free(expected);
		}
		#pragma oss taskwait
		return success;
	}

#ifdef __cplusplus
}
#endif

#endif