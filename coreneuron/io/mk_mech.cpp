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

#include <cstring>
#include <map>
#include <iostream>
#include <fstream>
#include <sstream>

#include "coreneuron/apps/corenrn_parameters.hpp"
#include "coreneuron/nrnconf.h"
#include "coreneuron/sim/multicore.hpp"
#include "coreneuron/membrane_definitions.h"
#include "coreneuron/mechanism/register_mech.hpp"
#include "coreneuron/nrniv/nrniv_decl.h"
#include "coreneuron/utils/nrn_assert.h"
#include "coreneuron/mechanism/mech/cfile/cabvars.h"
#include "coreneuron/io/nrn2core_direct.h"
#include "coreneuron/coreneuron.hpp"
#include "coreneuron/mechanism//eion.hpp"

static char banner[] = "Duke, Yale, and the BlueBrain Project -- Copyright 1984-2020";

namespace coreneuron {
int nrn_nobanner_;

extern corenrn_parameters corenrn_param;

// NB: this should go away
extern const char* nrn_version(int);

bool nrn_need_byteswap;
// following copied (except for nrn_need_byteswap line) from NEURON ivocvect.cpp
#define BYTEHEADER   \
    uint32_t _II__;  \
    char* _IN__;     \
    char _OUT__[16]; \
    bool BYTESWAP_FLAG = false;
#define BYTESWAP(_X__, _TYPE__)                                 \
    BYTESWAP_FLAG = nrn_need_byteswap;                          \
    if (BYTESWAP_FLAG) {                                        \
        _IN__ = (char*)&(_X__);                                 \
        for (_II__ = 0; _II__ < sizeof(_TYPE__); _II__++) {     \
            _OUT__[_II__] = _IN__[sizeof(_TYPE__) - _II__ - 1]; \
        }                                                       \
        (_X__) = *((_TYPE__*)&_OUT__);                          \
    }

std::map<std::string, int> mech2type;

extern "C" {
void (*nrn2core_mkmech_info_)(std::ostream&);
}
static void mk_mech();
static void mk_mech(std::istream&);

/// Read meta data about the mechanisms and allocate corresponding mechanism management data
/// structures
void mk_mech(const char* datpath) {
    if (corenrn_embedded) {
        // we are embedded in NEURON
        mk_mech();
        return;
    }
    {
        std::string fname = std::string(datpath) + "/bbcore_mech.dat";
        std::ifstream fs(fname);

        if (!fs.good()) {
            fprintf(stderr, "Error: couldn't find bbcore_mech.dat file in the dataset directory \n");
            fprintf(
                stderr,
                "       Make sure to pass full directory path of dataset using -d DIR or --datpath=DIR \n");
        }

        nrn_assert(fs.good());
        mk_mech(fs);
        fs.close();
    }

    {
        std::string fname = std::string(datpath) + "/byteswap1.dat";
        FILE* f = fopen(fname.c_str(), "r");
        if (!f) {
            fprintf(stderr, "Error: couldn't find byteswap1.dat file in the dataset directory \n");
        }
        nrn_assert(f);
        // file consists of int32_t binary 1 . After reading can decide if
        // binary info in files needs to be byteswapped.
        int32_t x;
        nrn_assert(fread(&x, sizeof(int32_t), 1, f) == 1);
        nrn_need_byteswap = false;
        if (x != 1) {
            BYTEHEADER;
            nrn_need_byteswap = true;
            BYTESWAP(x, int32_t);
            nrn_assert(x == 1);
        }
        fclose(f);
    }
}

// we are embedded in NEURON, get info as stringstream from nrnbbcore_write.cpp
static void mk_mech() {
    static bool already_called = false;
    if (already_called) {
        return;
    }
    nrn_need_byteswap = false;
    std::stringstream ss;
    nrn_assert(nrn2core_mkmech_info_);
    (*nrn2core_mkmech_info_)(ss);
    mk_mech(ss);
    already_called = true;
}

static void mk_mech(std::istream& s) {
    char version[256];
    s >> version;
    check_bbcore_write_version(version);

    //  printf("reading %s\n", fname);
    int n = 0;
    nrn_assert(s >> n);

    /// Allocate space for mechanism related data structures
    alloc_mech(n);

    /// Read all the mechanisms and their meta data
    for (int i = 2; i < n; ++i) {
        char mname[100];
        int type = 0, pnttype = 0, is_art = 0, is_ion = 0, dsize = 0, pdsize = 0;
        nrn_assert(s >> mname >> type >> pnttype >> is_art >> is_ion >> dsize >> pdsize);
        nrn_assert(i == type);
#ifdef DEBUG
        printf("%s %d %d %d %d %d %d\n", mname, type, pnttype, is_art, is_ion, dsize, pdsize);
#endif
        std::string str(mname);
        corenrn.get_memb_func(type).sym = (Symbol*)strdup(mname);
        mech2type[str] = type;
        corenrn.get_pnt_map()[type] = (char)pnttype;
        corenrn.get_prop_param_size()[type] = dsize;
        corenrn.get_prop_dparam_size()[type] = pdsize;
        corenrn.get_is_artificial()[type] = is_art;
        if (is_ion) {
            double charge = 0.;
            nrn_assert(s >> charge);
            // strip the _ion
            char iname[100];
            strcpy(iname, mname);
            iname[strlen(iname) - 4] = '\0';
            // printf("%s %s\n", mname, iname);
            ion_reg(iname, charge);
        }
        // printf("%s %d %d\n", mname, nrn_get_mechtype(mname), type);
    }

    if (nrnmpi_myid < 1 && nrn_nobanner_ == 0 && !corenrn_param.is_quiet()) {
        fprintf(stderr, " \n");
        fprintf(stderr, " %s\n", banner);
        fprintf(stderr, " %s\n", nrn_version(1));
        fprintf(stderr, " \n");
        fflush(stderr);
    }
    /* will have to put this back if any mod file refers to diam */
    //	register_mech(morph_mech, morph_alloc, (Pfri)0, (Pfri)0, (Pfri)0, (Pfri)0, -1, 0);

    /// Calling _reg functions for the default mechanisms from the file mech/cfile/cabvars.h
    for (int i = 0; mechanism[i]; i++) {
        (*mechanism[i])();
    }
}

/// Get mechanism type by the mechanism name
int nrn_get_mechtype(const char* name) {
    std::string str(name);
    std::map<std::string, int>::const_iterator mapit = mech2type.find(str);
    if (mapit == mech2type.end())
        return -1;  // Could not find the mechanism
    return mapit->second;
}

const char* nrn_get_mechname(int type) {
    for (std::map<std::string, int>::iterator i = mech2type.begin(); i != mech2type.end(); ++i) {
        if (type == i->second) {
            return i->first.c_str();
        }
    }
    return nullptr;
}
}  // namespace coreneuron
