/*
 * Modified by: chenrc11 <1065313246@qq.com>
 * Date: 2024-05-29 15:29:55
 */
/*
 * Modified by: chenrc11 <1065313246@qq.com>
 * Date: 2024-05-29 15:29:55
 */
from m5.params import *
from m5.SimObject import SimObject

class RemappingTable(SimObject):
    type = 'RemappingTable'
    cxx_header = "mem/remappingtable.hh"
    cxx_class = "gem5::memory::RemappingTable"
    
    # nvm_size = Param.MemorySize('8GB',
