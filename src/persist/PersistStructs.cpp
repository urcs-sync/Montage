#include "PersistStructs.hpp"
#include "EpochSys.hpp"
#include "Recoverable.hpp"

namespace pds{

PBlk::PBlk(Recoverable* ds){
    ds->register_alloc_pblk(this);
}

void sc_desc_t::try_complete(EpochSys* esys, uint64_t addr){
    nbptr_t _d = nbptr.load();
    int ret = 0;
    if(_d.val!=addr) return;
    if(in_progress(_d)){
        if(esys->check_epoch(cas_epoch)){
            ret = 2;
            ret |= commit(_d);
        } else {
            ret = 4;
            ret |= abort(_d);
        }
    }
    cleanup(_d);
}

}