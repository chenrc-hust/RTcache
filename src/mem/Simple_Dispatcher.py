from m5.params import *
from m5.SimObject import SimObject

# A wrapper for DRAMSim3 multi-channel memory controller
class Simple_Dispatcher(SimObject):
    type = 'Simple_Dispatcher'
    cxx_header = "mem/simple_dispatcher.hh"
    cxx_class = "gem5::memory::Simple_Dispatcher"

    bus_side_port = ResponsePort("port for receiving requests from"
                        "the membus or other requestor")

    dram_side_port = RequestPort("port for sending requests to"
                        "dram physical memory")

    hbm_side_port = RequestPort("port for sending requests to"
                        "hbm physical memory")