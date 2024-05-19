from m5.params import *
from m5.SimObject import SimObject

#py
# A wrapper for DRAMSim3 multi-channel memory controller
class MigrationManager(SimObject):
    type = 'MigrationManager'
    cxx_header = "mem/migrationmanager.hh"

    # A single port for now
    ac_side_port = ResponsePort("port for receiving requests from"
                        "Access Counter")
    
    rt_response_port = ResponsePort("port for receiving requests from"
                        "Remappingt Table")

    rt_request_port = RequestPort("port for sending requests to"
                        "Remapping Table")

    dp_side_port = RequestPort("port for sending requests to"
                        "Dispatcher")