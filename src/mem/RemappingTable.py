from m5.params import *
from m5.SimObject import SimObject

class RemappingTable(SimObject):
    type = 'RemappingTable'
    cxx_header = "mem/remappingtable.hh"
    cxx_class = "gem5::memory::RemappingTable"
    
    # nvm_size = Param.MemorySize('8GB',
    #                                "Size of NVM")
    dram_size = Param.MemorySize('2GB',
                                   "Size of DRAM")                               
    hbm_size = Param.MemorySize('256MB',
                                   "Size of HBM")


    bus_side_port = ResponsePort("port for receiving requests from"
                        "the membus or other requestor")
    
    # mm_side_port = ResponsePort("port for receiving requests from"
    #                     "the migration manager")

    dp_side_port = RequestPort("port for sending requests to"
                        "dispatcher")
                        
    # mm_request_port = RequestPort("port for sending requests to"
    #                     "migrationmanager")
