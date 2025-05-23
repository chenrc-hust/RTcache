# Copyright (c) 2012-2013, 2021 Arm Limited
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Copyright (c) 2005-2008 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from m5.params import *
from m5.objects.AbstractMemory import *

#  
# 
class SimpleMemory(AbstractMemory):
    type = "SimpleMemory"
    cxx_header = "mem/simple_mem.hh"
    cxx_class = "gem5::memory::SimpleMemory"

    port = ResponsePort("This port sends responses and receives requests")
    latency = Param.Latency("1ns", "Request to response latency")# base 
    latency_var = Param.Latency("0ns", "Request to response latency variance")
    # The memory bandwidth limit default is set to 12.8GiB/s which is
    # representative of a x64 DDR3-1600 channel.
    bandwidth = Param.MemoryBandwidth(
        "12.8GiB/s", "Combined read and write bandwidth"
    )

    hybird = Param.Int(0,"0 is default not open hybird")

    size = Param.MemorySize("16kB", "The size of the cache")
    
    cache_type = Param.Int(1,"select the model")
    
    wl_type = Param.Int(0,"select the wearlevel")

    swap_time = Param.Int(2048,"set the swap time")
    
    hbm_size = Param.MemorySize("16kB", "The size of the hbm_cache")

    rt_group = Param.Int(8,"rt group size")

    pre = Param.Int(0,"pre ")

    rt_dt = Param.Int(0,"rt dongtai")
    def controller(self):
        # Simple memory doesn't use a MemCtrl
        return self
