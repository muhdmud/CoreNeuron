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
#include "coreneuron/nrniv/nrn_checkpoint.h"
#include "coreneuron/nrnoc/multicore.h"
#include "coreneuron/nrniv/nrniv_decl.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cassert>

static int   maxgid;     // no gid in any file can be greater than maxgid
static const char* output_dir; // output directory to write simple checkpoint 
static void write_phase1( NrnThread& nt);
static void write_phase2( NrnThread& nt);
static void write_phase3( NrnThread& nt);

void write_checkpoint (NrnThread* nt, int nb_threads, const char* dir){
  output_dir = dir;
  int i;
/*
#if defined(_OPENMP)
  #pragma omp parallel for private(i) shared(nt, nb_threads) schedule(runtime)
#endif
*/
for (i = 0 ; i < nb_threads; i ++) {
    write_phase1 (nt[i]);
    write_phase2 (nt[i]);
    write_phase3 (nt[i]);
  }
}



static void write_phase1( NrnThread& nt){
  // serialize
  int* output_gids      = (int*) malloc (nt.n_presyn*sizeof(int));
  int* netcon_srcgid    = (int*) malloc (nt.n_netcon*sizeof(int));
  // fill array of output_gids with:
  // nt_presyns[i]->gid_ - (maxgid * nrn_setup_multiple);
  for (int i = 0; i < nt.n_presyn; i++) {
    output_gids[i] = nt.presyns[i].gid_ - (maxgid * nrn_setup_multiple);
  }
  for (int i = 0; i < nt.n_netcon; i++) {
    netcon_srcgid[i] = nt.src_gids[i] - (maxgid * nrn_setup_multiple);
  }
  
  // open file for writing
  std::ostringstream filename;
  filename << output_dir << "/" << nt.file_id << "_1.dat";
  std::ofstream file_handle (filename.str().c_str(), std::ios::binary);
  assert (! file_handle);
  
  // write dimensions:  nt.n_presyn and nt.netcon - nrn_setup_extracon (nrn_setup:390)
  file_handle << nt.n_presyn;
  file_handle << nt.n_netcon - nrn_setup_extracon;
  
  file_handle.write ((const char*) output_gids,   nt.n_presyn*(sizeof(int)));
  file_handle.write ((const char*) netcon_srcgid, nt.n_netcon*(sizeof(int)));
  
  // close file
  file_handle.close();
  free (output_gids);
  free (netcon_srcgid);
}

static void write_phase2( NrnThread& nt){
// open file for writing
// n_outputgid is not stored in read_pahse2
// write nt.ncell
// write nt.end
// write 0 if nt._actual_diam == NULL otherwise write nt.end again
// write number of element in linked list tml (in read file it is nmech)
// for each element in tml:
//        write tml->index
//        write tml->ml->nodecount
// write nt._ndata
// write nt._nidata
// write nt._nvdata
// write nt.n_weight
// write nt._v_parent_index [0 -> nt.end -1]
// write nt._actual_a    [0 -> nt.end -1]
// write nt._actual_b    [0 -> nt.end -1]
// write nt._actual_area [0 -> nt.end -1]
// write nt._actual_v    [0 -> nt.end -1]
// if (nt._actual_diam)
//        write nt._actual_diam [0 -> nt.end]
//
// for each element of tml:
//        if ! nrn_is_artificial [tml->index]
//              write tml->ml->nodeindices [0 -> tml->ml->nodecount -1]
//        for i in [0..ml->nodecount]:
//                                                                  (read care of data layout, here we use 2D loop to always write correct elements)
//              write ml->data[i][0..nrn_prop_param_size[type]]                  (read on line 1129)
//              write ml->pdata[i][0..nrn_dprop_param_size_[type]]               (read on line 1131)
//

// close file
}

static void write_phase3( NrnThread& nt){

}

