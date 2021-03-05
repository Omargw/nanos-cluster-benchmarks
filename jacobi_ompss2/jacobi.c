/*
 * Copyright (C) 2019  Jimmy Aguilar Mena
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>

#include "ArgParserC/argparser.h"

#include "jacobi.h"


#if FETCHTASK == 0
#define THECOND 0
#elif FETCHTASK == 1
#define THECOND 1
#elif FETCHTASK == 2
#define THECOND it < 1
#else  // FETCHTASK
#error FETCHTASK value not valid.
#endif // FETCHTASK

void jacobi_tasks(const double *A, const double *B, double *xin, double *xout,
                  size_t ts, size_t dim, size_t it
) {

	printf("# matvec_tasks_node FETCHTASK %d\n", FETCHTASK);
	const size_t numNodes = nanos6_get_num_cluster_nodes();
	myassert(ts <= dim);
	modcheck(dim, ts);

	const size_t rowsPerNode = dim / numNodes;
	myassert(ts <= rowsPerNode);
	modcheck(rowsPerNode, ts);

	for (size_t i = 0; i < dim; i += rowsPerNode) {

		int nodeid = i / rowsPerNode;

		#pragma oss task weakin(A[i * dim; rowsPerNode * dim])			\
			weakin(xin[0; dim])											\
			weakin(B[i; rowsPerNode])									\
			weakout(xout[i; rowsPerNode])								\
			node(nodeid) wait label("weakmatvec")
		{
			if (THECOND) {
				#pragma oss task in(A[i * dim; rowsPerNode * dim])		\
					in(xin[0; dim])										\
					in(B[i; rowsPerNode])								\
					out(xout[i; rowsPerNode])							\
					node(nanos6_cluster_no_offload) label("fetchtask")
				{
				}
			}

			for (size_t j = i; j < i + rowsPerNode; j += ts) {
				#pragma oss task in(A[j * dim; ts * dim])				\
					in(xin[0; dim])										\
					in(B[j; ts])										\
					out(xout[j; ts])									\
					node(nanos6_cluster_no_offload) label("strongmatvec")
				{
					matmul_base(&A[j * dim], xin, &xout[j], ts, dim);
					for (size_t l = j; l < j + ts; ++l) {
						xout[l] = B[l] + xout[l];
					}
				}
			}
		}
	}
}

int main(int argc, char* argv[])
{
	init_args(argc, argv);

	const int ROWS = create_cl_int ("Rows");
	const int TS = create_cl_int ("Tasksize");
	const int ITS = create_optional_cl_int ("Iterations", 1);
	const int PRINT = create_optional_cl_int ("Print", 0);

	printf("# Initializing data\n");
	timer ttimer = create_timer("Total time");

	double *A = nanos6_dmalloc(ROWS * ROWS * sizeof(double),
	                           nanos6_equpart_distribution, 0, NULL);

	double *B = nanos6_dmalloc(ROWS * sizeof(double),
	                           nanos6_equpart_distribution, 0, NULL);

	double *x1 = nanos6_dmalloc(ROWS * sizeof(double),
	                            nanos6_equpart_distribution, 0, NULL);

	double *x2 = nanos6_dmalloc(ROWS * sizeof(double),
	                            nanos6_equpart_distribution, 0, NULL);

	init_AB(A, B, ROWS, TS);
	jacobi_modify(A, B, ROWS, TS);

	init_x(x1, ROWS, TS, 0.0);
	init_x(x2, ROWS, TS, 0.0);

	#pragma oss taskwait

	printf("# Starting algorithm\n");
	timer atimer = create_timer("Algorithm time");

	for (int i = 0; i < ITS; ++i) {
		jacobi_tasks(A, B, x1, x2, TS, ROWS, 2 * i);
		jacobi_tasks(A, B, x2, x1, TS, ROWS, 2 * i + 1);
	}
	#pragma oss taskwait

	stop_timer(&atimer);

	printf("# Finished algorithm...\n");
	stop_timer(&ttimer);

	if (PRINT) {
		printmatrix_task(A, ROWS, ROWS, "jacobi");
		printmatrix_task(B, ROWS, 1, "jacobi");
		printmatrix_task(x1, ROWS, 1, "jacobi");
		#pragma oss taskwait
	}

	free_matrix(A, ROWS * ROWS);
	free_matrix(B, ROWS);
	free_matrix(x1, ROWS);
	free_matrix(x2, ROWS);

	create_reportable_int("worldsize", nanos6_get_num_cluster_nodes());
	create_reportable_int("cpu_count", nanos6_get_num_cpus());
	create_reportable_int("namespace_enabled", nanos6_get_namespace_is_enabled());

	const double performance =
		2 * ITS * ROWS * ROWS * 2000.0 / getNS_timer(&atimer);

	create_reportable_double("performance", performance);

	report_args();
	free_args();

	return 0;
}