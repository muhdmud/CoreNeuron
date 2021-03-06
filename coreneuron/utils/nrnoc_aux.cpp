/*
Copyright (c) 2016, Blue Brain Project
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <cstdlib>
#include <cstring>

#include "coreneuron/sim/multicore.hpp"
#include "coreneuron/mpi/nrnmpi.h"
#include "coreneuron/coreneuron.hpp"
#include "coreneuron/utils/nrnoc_aux.hpp"

namespace coreneuron {
bool stoprun;
int v_structure_change;
int diam_changed;
#define MAXERRCOUNT 5
int hoc_errno_count;
const char* bbcore_write_version = "1.2";

char* pnt_name(Point_process* pnt) {
    return corenrn.get_memb_func(pnt->_type).sym;
}

void nrn_exit(int err) {
#if NRNMPI
    nrnmpi_finalize();
#endif
    exit(err);
}

void hoc_execerror(const char* s1, const char* s2) {
    printf("error: %s %s\n", s1, s2 ? s2 : "");
    abort();
}

void hoc_warning(const char* s1, const char* s2) {
    printf("warning: %s %s\n", s1, s2 ? s2 : "");
}

double* makevector(size_t size) {
    return (double*)ecalloc(size, sizeof(char));
}

void freevector(double* p) {
    if (p) {
        free(p);
    }
}

double** makematrix(size_t nrows, size_t ncols) {
    double** matrix = (double**)emalloc(nrows * sizeof(double*));
    *matrix = (double*)emalloc(nrows * ncols * sizeof(double));
    for (size_t i = 1; i < nrows; i++)
        matrix[i] = matrix[i - 1] + ncols;
    return (matrix);
}

void freematrix(double** matrix) {
    if (matrix != nullptr) {
        free(*matrix);
        free(matrix);
    }
}

void* emalloc(size_t size) {
    void* memptr = malloc(size);
    assert(memptr);
    return memptr;
}

/* some user mod files may use this in VERBATIM */
void* hoc_Emalloc(size_t size) {
    return emalloc(size);
}
void hoc_malchk(void) {
}

void* ecalloc(size_t n, size_t size) {
    if (n == 0) {
        return nullptr;
    }
    void* p = calloc(n, size);
    assert(p);
    return p;
}

void* erealloc(void* ptr, size_t size) {
    if (!ptr) {
        return emalloc(size);
    }
    void* p = realloc(ptr, size);
    assert(p);
    return p;
}

void* nrn_cacheline_alloc(void** memptr, size_t size) {
#if HAVE_MEMALIGN
    if (posix_memalign(memptr, 64, size) != 0) {
        fprintf(stderr, "posix_memalign not working\n");
        assert(0);
    }
#else
    *memptr = emalloc(size);
#endif
    return *memptr;
}

/* used by nmodl and other c, c++ code */
double hoc_Exp(double x) {
    if (x < -700.) {
        return 0.;
    } else if (x > 700) {
        errno = ERANGE;
        if (++hoc_errno_count < MAXERRCOUNT) {
            fprintf(stderr, "exp(%g) out of range, returning exp(700)\n", x);
        }
        if (hoc_errno_count == MAXERRCOUNT) {
            fprintf(stderr, "No more errno warnings during this execution\n");
        }
        return exp(700.);
    }
    return exp(x);
}

/* check for version bbcore_write version between NEURON and CoreNEURON
 * abort in case of missmatch
 */
void check_bbcore_write_version(const char* version) {
    if (strcmp(version, bbcore_write_version) != 0) {
        if (nrnmpi_myid == 0)
            fprintf(stderr,
                    "Error: Incompatible binary input dataset version (expected %s, input %s)\n",
                    bbcore_write_version, version);
        abort();
    }
}
}  // namespace coreneuron
