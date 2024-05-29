/*
 * Modified by: chenrc11 <1065313246@qq.com>
 * Date: 2024-05-29 15:27:22
 */
/*
 * Modified by: chenrc11 <1065313246@qq.com>
 * Date: 2024-05-29 15:27:22
 */
/*
 * Modified by: chenrc11 <1065313246@qq.com>
 * Date: 2024-05-29 15:27:22
 */
from m5.params import *
from m5.SimObject import SimObject

#py
# A wrapper for DRAMSim3 multi-channel memory controller
# pageswaper
class PageSwaper(SimObject):
    type = 'PageSwaper'
    cxx_header = "mem/pageswaper.hh"
    cxx_class = "gem5::memory::PageSwaper"

    # A single port for now
    # ac_side_port = ResponsePort("port for receiving requests from"
    #                     "Access Counter")
    wc_side_port = ResponsePort("port for receiving requests from"
                                "WearLevel Control")

    # rt_response_port = ResponsePort("port for receiving requests from"
    #                     "Remappingt Table")

    rt_request_port = RequestPort("port for sending requests to"
                        "Remapping Table")

    dp_side_port = RequestPort("port for sending requests to"
                        "Dispatcher")
