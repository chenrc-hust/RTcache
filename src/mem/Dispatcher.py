/*
 * Modified by: chenrc11 <1065313246@qq.com>
 * Date: 2024-05-29 15:25:23
 */
/*
 * Modified by: chenrc11 <1065313246@qq.com>
 * Date: 2024-05-29 15:25:23
 */
/*
 * Modified by: chenrc11 <1065313246@qq.com>
 * Date: 2024-05-29 15:25:23
 */
/*
 * Modified by: chenrc11 <1065313246@qq.com>
 * Date: 2024-05-29 15:25:23
 */
/*
m m5.params import *
from m5.SimObject import SimObject

# A wrapper for DRAMSim3 multi-channel memory controller
class Dispatcher(SimObject):
    type = 'Dispatcher'
    cxx_header = "mem/dispatcher.hh"
    cxx_class = "gem5::memory::Dispatcher"
    
    rt_side_port = ResponsePort("port for receiving requests from"
                        "remapping table")

    # mm_side_port = ResponsePort("port for receiving requests from"
    #                     "migration manager")
    
    wc_side_port = RequestPort("port for sending requests to"
                        "wearlevelcontrol ")

    hbm_side_port = RequestPort("port for sending requests to"
                        "physical hbm memory")
    
    dram_side_port = RequestPort("port for sending requests to"
                        "physical dram memory")

    # nvm_side_port = RequestPort("port for sending requests to"
    #                     "physical nvm memory")

    ps_side_port = ResponsePort("port for recv requests from"
                        "pageswaper ")
