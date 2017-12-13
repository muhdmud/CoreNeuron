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
#include "coreneuron/nrnoc/nrnoc_decl.h"
#include "coreneuron/nrniv/nrn_filehandler.h"
#include "coreneuron/nrniv/netcvode.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cassert>

#ifndef LAYOUT
#define LAYOUT 1
#endif
/*
 * LAYOUT = 0 => SoA, LAYOUT = 1 => AoS
 *
 * */
static const char* output_dir;  // output directory to write simple checkpoint
static bool swap_bytes;

// todo : only keep phase2 as rest (phase1, gan and 3) are constant
static void write_phase1(NrnThread& nt, FileHandler& file_handle);
static void write_phase2(NrnThread& nt, FileHandler& file_handle);
static void write_phase3(NrnThread& nt, FileHandler& file_handle);
static void write_tqueue(NrnThread& nt, FileHandler& file_handle);

void write_checkpoint(NrnThread* nt, int nb_threads, const char* dir, bool swap_bytes_order) {
    if (!strlen(dir))
        return;  // empty directory means the option is not enabled
    output_dir = dir;
    int i;
    swap_bytes = swap_bytes_order;
    /*
    #if defined(_OPENMP)
      #pragma omp parallel for private(i) shared(nt, nb_threads) schedule(runtime)
    #endif
    */
    FileHandler f;
    for (i = 0; i < nb_threads; i++) {
      if (nt[i].ncell) {
        //write_phase1(nt[i], f);
        write_phase2(nt[i], f);
        //write_phase3(nt[i], f);
      }
    }
}

static void write_phase1(NrnThread& nt, FileHandler& file_handle) {
    // open file for writing
    std::ostringstream filename;
    std::cout << "nt.file_id" << nt.file_id << " " << nt.n_presyn << " npresyn\n";
    filename << output_dir << "/" << nt.file_id << "_1.dat";
    file_handle.open(filename.str().c_str(), swap_bytes, std::ios::out);
    file_handle.checkpoint(0);
    // write dimensions:  nt.n_presyn and nt.netcon - nrn_setup_extracon (nrn_setup:390)
    file_handle << nt.n_presyn << " npresyn\n";
    file_handle << nt.n_netcon - nrn_setup_extracon << " nnetcon\n";

    file_handle.write_array<int>(nt.output_gids, nt.n_presyn);
    file_handle.write_array<int>(nt.src_gids, nt.n_netcon - nrn_setup_extracon);

    // close file
    file_handle.close();
    //  free (netcon_srcgid);
}

static void write_phase2(NrnThread& nt, FileHandler& file_handle) {
    std::ostringstream filename;
    filename << output_dir << "/" << nt.file_id << "_2.dat";
    file_handle.open(filename.str().c_str(), swap_bytes, std::ios::out);
    file_handle.checkpoint(2);
    file_handle << nt.n_outputgids << " ngid\n";
    file_handle << nt.ncell << " n_real_gid\n";
    file_handle << nt.end << " nnode\n";
    file_handle << ((nt._actual_diam == NULL) ? 0 : nt.end) << " ndiam\n";
    file_handle << nt.nmech << " nmech\n";
    NrnThreadMembList* current_tml = nt.tml;

    while (current_tml) {
        file_handle << current_tml->index << "\n";
        file_handle << current_tml->ml->nodecount << "\n";
        current_tml = current_tml->next;
    }

    file_handle << nt.ndata_unpadded << " ndata\n";
    file_handle << nt._nidata << " nidata\n";
    file_handle << nt._nvdata << " nvdata\n";
    file_handle << nt.n_weight << " nweight\n";
    file_handle.write_array<int>(nt._v_parent_index, nt.end);
    file_handle.write_array<double>(nt._actual_a, nt.end);
    file_handle.write_array<double>(nt._actual_b, nt.end);
    file_handle.write_array<double>(nt._actual_area, nt.end);
    file_handle.write_array<double>(nt._actual_v, nt.end);

    if (nt._actual_diam)
        file_handle.write_array<double>(nt._actual_diam, nt.end);

    current_tml = nt.tml;

    while (current_tml) {
        int type = current_tml->index;
        int nb_nodes = current_tml->ml->nodecount;
        int size_of_line_data = nrn_soa_padded_size(nb_nodes, nrn_mech_data_layout_[type]);

        if (!nrn_is_artificial_[type]) {
            file_handle.write_array<int>(current_tml->ml->nodeindices, nb_nodes);
        }

        // if LAYOUT is SoA: we need to transpose to the structure to write in file format order
        file_handle.write_array<double>(current_tml->ml->data, nb_nodes, size_of_line_data,
                                        nrn_prop_param_size_[type], !LAYOUT);

        if (nrn_prop_dparam_size_[type]) {
            // if LAYOUT is SoA: we need to transpose to the structure to write in file format order
            file_handle.write_array<int>(current_tml->ml->pdata_not_permuted, nb_nodes,
                                         size_of_line_data, nrn_prop_dparam_size_[type], !LAYOUT);
        }

        current_tml = current_tml->next;
    }

    int nnetcon = nt.n_netcon - nrn_setup_extracon;
    file_handle.write_array<int>(nt.output_vindex, nt.n_presyn);
    file_handle.write_array<double>(nt.output_threshold, nt.ncell);
    file_handle.write_array<int>(nt.pnttype, nnetcon);
    file_handle.write_array<int>(nt.pntindex, nnetcon);
    file_handle.write_array<double>(nt.weights, nt.n_weight);
    file_handle.write_array<double>(nt.delay, nnetcon);
    file_handle << nt.npnt << " bbcorepointer\n";

    int* iArray = NULL;
    double* dArray = NULL;

    for (int i = 0; i < nt.npnt; i++) {
        file_handle << nt.type[i] << "\n";
        file_handle << nt.icnt[i] << "\n";
        file_handle << nt.dcnt[i] << "\n";

        iArray = new int[nt.icnt[i]];
        dArray = new double[nt.dcnt[i]];
        Memb_list* ml = nt.mlmap[nt.type[i]];
        int dsz = nrn_prop_param_size_[nt.type[i]];
        int pdsz = nrn_prop_dparam_size_[nt.type[i]];

        if (nrn_bbcore_write_[nt.type[i]] && (nt.icnt[i] || nt.dcnt[i])) {
            int d_offset = 0;
            int i_offset = 0;
            double* d = ml->data;
            Datum* pd = ml->pdata;
            int layout = nrn_mech_data_layout_[nt.type[i]];

            for (int j = 0; j < ml->nodecount; j++) {
                int jp = j;

                if (ml->_permute) {
                    jp = ml->_permute[j];
                }

                d = ml->data + nrn_i_layout(jp, ml->nodecount, 0, dsz, layout);
                pd = ml->pdata + nrn_i_layout(jp, ml->nodecount, 0, pdsz, layout);
                int aln_cntml = nrn_soa_padded_size(ml->nodecount, layout);

                // extra parameters after i_offset dont seems to be used
                (*nrn_bbcore_write_[nt.type[i]])(dArray, iArray, &d_offset, &i_offset, 0, aln_cntml,
                                                 d, pd, ml->_thread, &nt, 0.0);
            }
        } else {
            std::cerr << " ERROR no bbcore_write registered for this type" << std::endl;
        }

        if (nt.icnt[i]) {
            file_handle.write_array<int>(iArray, nt.icnt[i]);
        }

        if (nt.dcnt[i]) {
            file_handle.write_array<double>(dArray, nt.dcnt[i]);
        }
        delete[] iArray;
        delete[] dArray;
    }

    file_handle << nt.n_vecplay << " VecPlay instances\n";
    for (int i = 0; i < nt.n_vecplay; i++) {
        file_handle << nt.vtype[i] << "\n";
        file_handle << nt.mtype[i] << "\n";
        file_handle << nt.vecplay_ix[i] << "\n";
        file_handle << nt.vecplay_sz[i] << "\n";
        file_handle.write_array<double>(nt.vecplay_yvec[i], nt.vecplay_sz[i]);
        file_handle.write_array<double>(nt.vecplay_tvec[i], nt.vecplay_sz[i]);
    }
    write_tqueue(nt, file_handle);
    file_handle.close();
}

static void write_phase3(NrnThread&, FileHandler&) {
}

static void write_tqueue(TQItem* q, NrnThread& nt, FileHandler& fh) {
    DiscreteEvent* d = (DiscreteEvent*)q->data_;
    std::cout << "  p  " << q->t_ << " " << d->type() << "\n";

    d->pr("", q->t_, net_cvode_instance);

    fh << d->type() << "\n";
    fh.write_array(&q->t_, 1);

    switch (d->type()) {
      case NetConType: {
        NetCon* nc = (NetCon*)d;
        assert (nc >= nt.netcons && (nc < (nt.netcons + nt.n_netcon)));
        fh << (nc - nt.netcons) << "\n";
        break;
      }
      case SelfEventType: {
        SelfEvent* se = (SelfEvent*)d;
        fh << int(se->target_->_type) << "\n";
        fh << se->target_->_i_instance << "\n";
        fh.write_array(&se->flag_, 1);
        fh << (se->movable_ - nt._vdata) << "\n"; // DANGEROUS?
        fh << se->weight_index_ << "\n";
        break;
      }
      case PreSynType: {
        PreSyn* ps = (PreSyn*)d;
        assert(ps >= nt.presyns && (ps < (nt.presyns + nt.n_presyn)));
        fh << (ps - nt.presyns) << "\n";
        break;
      }
      case NetParEventType: {
        // nothing extra to write
        break;
      }
      default: {
        // In particular, InputPreSyn does not appear in tqueue as it
        // immediately fans out to NetCon.
        assert(0);
        break;
      }
    }    
}

static void checkpoint_restore_tqitem(int type, NrnThread& nt, FileHandler& fh) {
    double te;
    fh.read_array(&te, 1);
    std::cout << "restore tqitem type=" << type << " te=" << te << "\n";

    switch (type) {
      case NetConType: {
        int ncindex = fh.read_int();
        std::cout << "  NetCon " << ncindex << "\n";
        NetCon* nc = nt.netcons + ncindex;
        nc->send(te, net_cvode_instance, &nt);
        break;
      }
      case SelfEventType: {
        int target_type = fh.read_int(); // not really needed (except for assert below)
        int target_instance = fh.read_int();
        double flag;
        fh.read_array(&flag, 1);
        int movable = fh.read_int();
        int weight_index = fh.read_int();
        Point_process* pnt = nt.pntprocs + target_instance;
        assert(target_instance == pnt->_i_instance);
        assert(target_type == pnt->_type);
        std::cout << "  SelfEvent " << target_type << " " << target_instance << " " << flag << " " << movable << "\n";
        net_send(nt._vdata + movable, weight_index, pnt, te, flag);
        break;
      }
      case PreSynType: {
        int psindex = fh.read_int();
        std::cout << "  PreSyn " << psindex << "\n";
        PreSyn* ps = nt.presyns + psindex;
        int gid = ps->output_index_;
        ps->output_index_ = -1;
        ps->send(te, net_cvode_instance, &nt);
        ps->output_index_ = gid;
        break;
      }
      case NetParEventType: {
        // nothing extra to read
        std::cout << "  NetParEvent\n";
        break;
      }
      default: {
        assert(0);
        break;
      }
    }    
}

static void write_tqueue(NrnThread& nt, FileHandler& file_handle) {
    NetCvodeThreadData& ntd = net_cvode_instance->p[nt.id];
    std::cout << "write_tqueue " << nt.id << " " << ntd.tqe_ << "\n";
    TQueue<QTYPE>* tqe = ntd.tqe_;
    TQItem* q;

    file_handle << -1 << " TQItems from atomic_dq\n";
    while((q = tqe->atomic_dq(1e20)) != NULL) {
      write_tqueue(q, nt, file_handle);
    }
    file_handle << 0 << "\n";

    file_handle << -1 << " TQItemsfrom binq_\n";
    for (q = tqe->binq_->first(); q; q = tqe->binq_->next(q)) {
      write_tqueue(q, nt, file_handle);
    }
    file_handle << 0 << "\n";
}

static bool checkpoint_restored_ = false;

void checkpoint_restore_tqueue(NrnThread& nt, FileHandler& fh) {
    int type;
    checkpoint_restored_ = true;

    assert(fh.read_int() == -1); // -1 TQItems from atomic_dq
    while ((type = fh.read_int()) != 0) {
      checkpoint_restore_tqitem(type, nt, fh);
    }

    assert(fh.read_int() == -1); // -1 TQItems from binq_
    while ((type - fh.read_int()) != 0) {
      checkpoint_restore_tqitem(type, nt, fh);
    }
}

// A call to finitialize must be avoided after restoring the checkpoint
// as that would change all states to a voltage clamp initialization.
// Nevertheless t and some spike exchange and other computer state needs to
// be initialized.
// Much of what is here is a copy of finitialize.c
bool checkpoint_initialize() {

    dt2thread(-1.);
    nrn_thread_table_check();
    nrn_spike_exchange_init();
    
    return checkpoint_restored_;
}
