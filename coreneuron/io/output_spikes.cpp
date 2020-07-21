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

#include <iostream>
#include <sstream>
#include <cstring>
#include <stdexcept>  // std::lenght_error
#include <vector>
#include <algorithm>
#include <numeric>

#include "coreneuron/nrnconf.h"
#include "coreneuron/nrniv/nrniv_decl.h"
#include "coreneuron/io/output_spikes.hpp"
#include "coreneuron/mpi/nrnmpi.h"
#include "coreneuron/utils/nrnmutdec.h"
#include "coreneuron/mpi/nrnmpi_impl.h"
#include "coreneuron/mpi/nrnmpidec.h"
#include "coreneuron/utils/string_utils.h"
#ifdef ENABLE_SONATA_REPORTS
#include "bbp/sonata/reports.h"
#endif  // ENABLE_SONATA_REPORTS

namespace coreneuron {

/// --> Coreneuron as SpikeBuffer class
std::vector<double> spikevec_time;
std::vector<int> spikevec_gid;

#ifdef _OPENMP
static MUTDEC
#endif

    void
    mk_spikevec_buffer(int sz) {
    try {
        spikevec_time.reserve(sz);
        spikevec_gid.reserve(sz);
    } catch (const std::length_error& le) {
        std::cerr << "Lenght error" << le.what() << std::endl;
    }
    if (!MUTCONSTRUCTED) {
        MUTCONSTRUCT(1);
    }
}

void spikevec_lock() {
    MUTLOCK
}

void spikevec_unlock() {
    MUTUNLOCK
}

void local_spikevec_sort(std::vector<double>& isvect,
                         std::vector<int>& isvecg,
                         std::vector<double>& osvect,
                         std::vector<int>& osvecg) {
    osvect.resize(isvect.size());
    osvecg.resize(isvecg.size());
    // first build a permutation vector
    std::vector<std::size_t> perm(isvect.size());
    std::iota(perm.begin(), perm.end(), 0);
    // sort by gid (second predicate first)
    std::stable_sort(perm.begin(), perm.end(),
                     [&](std::size_t i, std::size_t j) { return isvecg[i] < isvecg[j]; });
    // then sort by time
    std::stable_sort(perm.begin(), perm.end(),
                     [&](std::size_t i, std::size_t j) { return isvect[i] < isvect[j]; });
    // now apply permutation to time and gid output vectors
    std::transform(perm.begin(), perm.end(), osvect.begin(),
                   [&](std::size_t i) { return isvect[i]; });
    std::transform(perm.begin(), perm.end(), osvecg.begin(),
                   [&](std::size_t i) { return isvecg[i]; });
}

#if NRNMPI

void sort_spikes(std::vector<double>& spikevec_time, std::vector<int>& spikevec_gid) {
    double lmin_time = std::numeric_limits<double>::max();
    double lmax_time = std::numeric_limits<double>::min();
    if (!spikevec_time.empty()) {
        lmin_time = *(std::min_element(spikevec_time.begin(), spikevec_time.end()));
        lmax_time = *(std::max_element(spikevec_time.begin(), spikevec_time.end()));
    }
    double min_time = nrnmpi_dbl_allmin(lmin_time);
    double max_time = nrnmpi_dbl_allmax(lmax_time);

    // allocate send and receive counts and displacements for MPI_Alltoallv
    std::vector<int> snd_cnts(nrnmpi_numprocs);
    std::vector<int> rcv_cnts(nrnmpi_numprocs);
    std::vector<int> snd_dsps(nrnmpi_numprocs);
    std::vector<int> rcv_dsps(nrnmpi_numprocs);

    double bin_t = (max_time - min_time) / nrnmpi_numprocs;
    bin_t = bin_t ? bin_t : 1;
    // first find number of spikes in each time window
    for (const auto& st : spikevec_time) {
        int idx = (int)(st - min_time) / bin_t;
        snd_cnts[idx]++;
    }
    for (int i = 1; i < nrnmpi_numprocs; i++) {
        snd_dsps[i] = snd_dsps[i - 1] + snd_cnts[i - 1];
    }

    // now let each rank know how many spikes they will receive
    // and get in turn all the buffer sizes to receive
    nrnmpi_int_alltoall(&snd_cnts[0], &rcv_cnts[0], 1);
    for (int i = 1; i < nrnmpi_numprocs; i++) {
        rcv_dsps[i] = rcv_dsps[i - 1] + rcv_cnts[i - 1];
    }
    std::size_t new_sz = 0;
    for (const auto& r : rcv_cnts) {
        new_sz += r;
    }
    // prepare new sorted vectors
    std::vector<double> svt_buf(new_sz, 0.0);
    std::vector<int> svg_buf(new_sz, 0);

    // now exchange data
    nrnmpi_dbl_alltoallv(spikevec_time.data(), &snd_cnts[0], &snd_dsps[0], svt_buf.data(),
                         &rcv_cnts[0], &rcv_dsps[0]);
    nrnmpi_int_alltoallv(spikevec_gid.data(), &snd_cnts[0], &snd_dsps[0], svg_buf.data(),
                         &rcv_cnts[0], &rcv_dsps[0]);

    local_spikevec_sort(svt_buf, svg_buf, spikevec_time, spikevec_gid);
}

/** Write generated spikes to out.dat using mpi parallel i/o.
 *  \todo : MPI related code should be factored into nrnmpi.c
 *          Check spike record length which is set to 64 chars
 */
void output_spikes_parallel(const char* outpath, const std::string& population_name) {
    std::stringstream ss;
    ss << outpath << "/out.dat";
    std::string fname = ss.str();

    // remove if file already exist
    if (nrnmpi_myid == 0) {
        remove(fname.c_str());
    }
#ifdef ENABLE_SONATA_REPORTS
    sonata_write_spikes(population_name.data(), spikevec_time.data(), spikevec_time.size(), spikevec_gid.data(),
                        spikevec_gid.size(), outpath);
#endif  // ENABLE_SONATA_REPORTS
    sort_spikes(spikevec_time, spikevec_gid);
    nrnmpi_barrier();

    // each spike record in the file is time + gid (64 chars sufficient)
    const int SPIKE_RECORD_LEN = 64;
    unsigned num_spikes = spikevec_gid.size();
    unsigned num_bytes = (sizeof(char) * num_spikes * SPIKE_RECORD_LEN);
    char* spike_data = (char*)malloc(num_bytes);

    if (spike_data == nullptr) {
        printf("Error while writing spikes due to memory allocation\n");
        return;
    }

    // empty if no spikes
    strcpy(spike_data, "");

    // populate buffer with all spike entries
    char spike_entry[SPIKE_RECORD_LEN];
    unsigned spike_data_offset = 0;
    for (unsigned i = 0; i < num_spikes; i++) {
        int spike_entry_chars = snprintf(spike_entry, 64, "%.8g\t%d\n", spikevec_time[i], spikevec_gid[i]);
        spike_data_offset = strcat_at_pos(spike_data, spike_data_offset, spike_entry, spike_entry_chars);
    }

    // calculate offset into global file. note that we don't write
    // all num_bytes but only "populated" buffer
    unsigned long num_chars = strlen(spike_data);
    unsigned long offset = 0;

    // global offset into file
    MPI_Exscan(&num_chars, &offset, 1, MPI_UNSIGNED_LONG, MPI_SUM, MPI_COMM_WORLD);

    // write to file using parallel mpi i/o
    MPI_File fh;
    MPI_Status status;

    // ibm mpi (bg-q) expects char* instead of const char* (even though it's standard)
    int op_status = MPI_File_open(MPI_COMM_WORLD, (char*)fname.c_str(),
                                  MPI_MODE_CREATE | MPI_MODE_WRONLY, MPI_INFO_NULL, &fh);
    if (op_status != MPI_SUCCESS && nrnmpi_myid == 0) {
        std::cerr << "Error while opening spike output file " << fname << std::endl;
        abort();
    }

    op_status = MPI_File_write_at_all(fh, offset, spike_data, num_chars, MPI_BYTE, &status);
    if (op_status != MPI_SUCCESS && nrnmpi_myid == 0) {
        std::cerr << "Error while writing spike output " << std::endl;
        abort();
    }

    MPI_File_close(&fh);
    free(spike_data);
}

#endif

void output_spikes_serial(const char* outpath) {
    std::stringstream ss;
    ss << outpath << "/out.dat";
    std::string fname = ss.str();

    // reserve some space for sorted spikevec buffers
    std::vector<double> sorted_spikevec_time(spikevec_time.size());
    std::vector<int> sorted_spikevec_gid(spikevec_gid.size());
    local_spikevec_sort(spikevec_time, spikevec_gid, sorted_spikevec_time, sorted_spikevec_gid);

    // remove if file already exist
    remove(fname.c_str());

    FILE* f = fopen(fname.c_str(), "w");
    if (!f && nrnmpi_myid == 0) {
        std::cout << "WARNING: Could not open file for writing spikes." << std::endl;
        return;
    }

    for (std::size_t i = 0; i < sorted_spikevec_gid.size(); ++i)
        if (sorted_spikevec_gid[i] > -1)
            fprintf(f, "%.8g\t%d\n", sorted_spikevec_time[i], sorted_spikevec_gid[i]);

    fclose(f);
}

void output_spikes(const char* outpath, const std::string& population_name) {
#if NRNMPI
    if (nrnmpi_initialized()) {
        output_spikes_parallel(outpath, population_name);
    } else {
        output_spikes_serial(outpath);
    }
#else
    output_spikes_serial(outpath);
#endif
}

void clear_spike_vectors() {
    auto spikevec_time_capacity = spikevec_time.capacity();
    auto spikevec_gid_capacity = spikevec_gid.capacity();
    spikevec_time.clear();
    spikevec_gid.clear();
    spikevec_time.reserve(spikevec_time_capacity);
    spikevec_gid.reserve(spikevec_gid_capacity);
}

void validation(std::vector<std::pair<double, int> >& res) {
    for (unsigned i = 0; i < spikevec_gid.size(); ++i)
        if (spikevec_gid[i] > -1)
            res.push_back(std::make_pair(spikevec_time[i], spikevec_gid[i]));
}
}  // namespace coreneuron
