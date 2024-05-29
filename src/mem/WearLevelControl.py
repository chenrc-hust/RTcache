from m5.params import *
from m5.SimObject import SimObject

class WearLevelControl(SimObject):
    type = 'WearLevelControl'
    cxx_header = "mem/wearlevelcontrol.hh"
    cxx_class = "gem5::memory::WearLevelControl"

    # nvm_size = Param.MemorySize('8GB',
    #                                "Size of NVM")
    dram_size = Param.MemorySize('2048MB',
                                   "Size of DRAM")                               
    hbm_size = Param.MemorySize('256MB',
                                   "Size of HBM")

    dp_side_port = ResponsePort("port for receiving requests from"
                        "the dispatcher")
    
    # mm_side_port = RequestPort("port for sending migration requests to"
    #                     "migration manager")

    ps_side_port = RequestPort("port sendint requests to"
                        "pages waper")
    
