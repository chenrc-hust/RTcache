/*
 * Modified by: chenrc11 <1065313246@qq.com>
 * Date: 2024-05-29 15:24:39
 */
/*
 * Modified by: chenrc11 <1065313246@qq.com>
 * Date: 2024-05-29 15:24:39
 */

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

# Simple test script
#
# "m5 test.py"
import argparse
# import optparse
import sys
import os

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.params import NULL
from m5.util import addToPath, fatal, warn

# //by crc 240514
from gem5.isas import ISA
from gem5.runtime import get_runtime_isa
addToPath("../")

from ruby import Ruby

from common import Options
from common import Simulation
from common import CacheConfig
from common import CpuConfig
from common import ObjectList
from common import MemConfig
from common.FileSystemConfig import config_filesystem
from common.Caches import *
from common.cpu2000 import *

def get_processes(args):
    """Interprets provided args and returns a list of processes"""

    multiprocesses = []
    inputs = []
    outputs = []
    errouts = []
    pargs = []

    workloads = args.cmd.split(";")
    if args.input != "":
        inputs = args.input.split(";")
    if args.output != "":
        outputs = args.output.split(";")
    if args.errout != "":
        errouts = args.errout.split(";")
    if args.options != "":
        pargs = args.options.split(";")

    idx = 0
    for wrkld in workloads:
        process = Process(pid = 100 + idx)
        process.executable = wrkld
        process.cwd = os.getcwd()
        process.gid = os.getgid()

        if args.env:
            with open(args.env, "r") as f:
                process.env = [line.rstrip() for line in f]

        if len(pargs) > idx:
            process.cmd = [wrkld] + pargs[idx].split()
        else:
            process.cmd = [wrkld]

        if len(inputs) > idx:
            process.input = inputs[idx]
                                                                                                                                                                                                                                                      process.gid = os.getgid()

        if args.env:
            with open(args.env, "r") as f:
                process.env = [line.rstrip() for line in f]

        if len(pargs) > idx:
            process.cmd = [wrkld] + pargs[idx].split()
        else:
            process.cmd = [wrkld]

        if len(inputs) > idx:
            process.input = inputs[idx]
        if len(outputs) > idx:
            process.output = outputs[idx]
        if len(errouts) > idx:
            process.errout = errouts[idx]

        multiprocesses.append(process)
        idx += 1

    if args.smt:
        assert args.cpu_type == "DerivO3CPU"
        return multiprocesses, idx
    else:
        return multiprocesses, 1

warn(
    "The se.py script is deprecated. It will be removed in future releases of "
    " gem5."
)

parser = argparse.ArgumentParser()
Options.addCommonOptions(parser)
Options.addSEOptions(parser)

if "--ruby" in sys.argv:
    Ruby.define_options(parser)

args = parser.parse_args()


multiprocesses = []
numThreads = 1

if args.bench:
    apps = args.bench.split("-")
    if len(apps) != args.num_cpus:
        print("number of benchmarks not equal to set num_cpus!")
        sys.exit(1)

    for app in apps:
        try:
            if get_runtime_isa() == ISA.ARM:
                exec(
                    "workload = %s('arm_%s', 'linux', '%s')"
                    % (app, args.arm_iset, args.spec_input)
                )
            else:
                # TARGET_ISA has been removed, but this is missing a ], so it
                # has incorrect syntax and wasn't being used anyway.
                exec(
                    "workload = %s(buildEnv['TARGET_ISA', 'linux', '%s')"
                    % (app, args.spec_input)
                )
            multiprocesses.append(workload.makeProcess())
        except:
            print(
                f"Unable to find workload for {get_runtime_isa().name()}: {app}",
                file=sys.stderr,
            )
            sys.exit(1)
elif args.cmd:
    multiprocesses, numThreads = get_processes(args)
else:
    print("No workload specified. Exiting!
", file=sys.stderr)
    sys.exit(1)



(CPUClass, test_mem_mode, FutureClass) = Simulation.setCPUClass(args)
CPUClass.numThreads = numThreads

# Check -- do not allow SMT with multiple CPUs
if args.smt and args.num_cpus > 1:
    fatal("You cannot use SMT with multiple CPUs!")

np = args.num_cpus
mp0_path = multiprocesses[0].executable
system = System(
    cpu=[CPUClass(cpu_id=i) for i in range(np)],
    mem_mode=test_mem_mode,
    mem_ranges=[AddrRange(args.mem_size)],
    cache_line_size=args.cacheline_size,
)

if numThreads > 1:
    system.multi_thread = True

# Create a top-level voltage domain
system.voltage_domain = VoltageDomain(voltage = args.sys_voltage)

# Create a source clock for the system and set the clock period
system.clk_domain = SrcClockDomain(clock =  args.sys_clock,
                                   voltage_domain = system.voltage_domain)

# Create a CPU voltage domain
system.cpu_voltage_domain = VoltageDomain()

# Create a separate clock domain for the CPUs
system.cpu_clk_domain = SrcClockDomain(clock = args.cpu_clock,
                                       voltage_domain = system.cpu_voltage_domain)

# If elastic tracing is enabled, then configure the cpu and attach the elastic
# trace probe
if args.elastic_trace_en:
    CpuConfig.config_etrace(CPUClass, system.cpu, args)

# All cpus belong to a common cpu_clk_domain, therefore running at a common
# frequency.
for cpu in system.cpu:
    cpu.clk_domain = system.cpu_clk_domain

if ObjectList.is_kvm_cpu(CPUClass) or ObjectList.is_kvm_cpu(FutureClass):
    if buildEnv["USE_X86_ISA"]:
        system.kvm_vm = KvmVM()
        system.m5ops_base = 0xFFFF0000
        for process in multiprocesses:
            process.useArchPT = True
            process.kvmInSE = True
    else:
        fatal("KvmCPU can only be used in SE mode with x86")

# Sanity check
if args.simpoint_profile:
    if not ObjectList.is_noncaching_cpu(CPUClass):
        fatal("SimPoint/BPProbe should be done with an atomic cpu")
    if np > 1:
        fatal("SimPoint generation not supported with more than one CPUs")

for i in range(np):
    if args.smt:
        system.cpu[i].workload = multiprocesses
    elif len(multiprocesses) == 1:
        system.cpu[i].workload = multiprocesses[0]
    else:
        system.cpu[i].workload = multiprocesses[i]

    if args.simpoint_profile:
        system.cpu[i].addSimPointProbe(args.simpoint_interval)

    if args.checker:
        system.cpu[i].addCheckerCpu()

    if args.bp_type:
        bpClass = ObjectList.bp_list.get(args.bp_type)
        system.cpu[i].branchPred = bpClass()

    if args.indirect_bp_type:
        indirectBPClass = ObjectList.indirect_bp_list.get(
            args.indirect_bp_type
        )
        system.cpu[i].branchPred.indirectBranchPred = indirectBPClass()

    system.cpu[i].createThreads()

if args.ruby:
    Ruby.create_system(args, False, system)
    assert(args.num_cpus == len(system.ruby._cpu_ports))

    system.ruby.clk_domain = SrcClockDomain(clock = args.ruby_clock,
                                        voltage_domain = system.voltage_domain)
    for i in range(np):
        ruby_port = system.ruby._cpu_ports[i]

        # Create the interrupt controller and connect its ports to Ruby
        # Note that the interrupt controller is always present but only
        # in x86 does it have message ports that need to be connected
        system.cpu[i].createInterruptController()

        # Connect the cpu's cache ports to Ruby
        ruby_port.connectCpuPorts(system.cpu[i])
else:
    MemClass = Simulation.setMemClass(args)
    system.membus = SystemXBar()
    system.system_port = system.membus.cpu_side_ports
    CacheConfig.config_cache(args, system)
    #MemConfig.config_mem(args, system)
    # if args.flat_mem == "true":
    #     system.physmem = DRAMsim3()
    #     system.physmem.range = AddrRange(args.mem_size)
    #     system.simple_dispatcher = FlatMemory()
    #     if args.dramsim3_ini is None:
    #         system.physmem.configFile = "ext/dramsim3/DRAMsim3/config/DDR4_4Gb_x4_2400.ini"
    #         #system.physmem.configFile = "ext/dramsim3/DRAMsim3/configs/HBM_4Gb_x128.ini"
    #     else:
    #         system.physmem.configFile = args.dramsim3_ini
    #     system.physmem.port = system.simple_dispatcher.mem_side_port
    #     system.simple_dispatcher.bus_side_port = system.membus.mem_side_ports
    # MemConfig.config_mem(args, system)
    if args.flat_mem == "true":
        print("Start dispatcher_rt test
")
        system.hbmphysmem = DRAMsim3()
        system.dramphysmem = DRAMsim3()

        # HBM in high address DRAM in low address HBM--256MB,DRAM--2048MB)
        print(f"DRAM Size: {args.dramSize_size}")

        print(f"HBM Size: {args.hbmSize_size}")
        system.dramphysmem.range = AddrRange(args.dramSize_size)
        hbm_size = (args.hbmSize_size)
        print(f"HBM Size: {hbm_size}")
        system.hbmphysmem.range = AddrRange(Addr(args.dramSize_size),size=hbm_size)
        print(f"HBM Size: {system.hbmphysmem.range}")
        # HBM in low address DRAM in high address
         

        system.dispatcher = Dispatcher() 
        system.wearlevelcontrol = WearLevelControl()
        system.pageswaper = PageSwaper()
        # system.accesscounter = AccessCounter()
        system.remappingtable = RemappingTable()

        if args.HBMdramsim3_ini is None:
            system.hbmphysmem.configFile = "ext/dramsim3/DRAMsim3/configs/HBM_4Gb_x128.ini"
        else:
            system.hbmphysmem.configFile = args.HBMdramsim3_ini
        if args.DRAMdramsim3_ini is None:
            system.dramphysmem.configFile = "ext/dramsim3/DRAMsim3/configs/DDR4_4Gb_x4_2400.ini"
        else:
            system.dramphysmem.configFile = args.DRAMdramsim3_ini

        # port setting
        system.remappingtable.bus_side_port = system.membus.mem_side_ports
        system.remappingtable.dp_side_port = system.dispatcher.rt_side_port
        system.remappingtable.ps_side_port = system.pageswaper.rt_request_port

        system.dispatcher.dram_side_port = system.dramphysmem.port
        system.dispatcher.hbm_side_port = system.hbmphysmem.port


        system.wearlevelcontrol.dp_side_port = system.dispatcher.wc_side_port
        system.wearlevelcontrol.ps_side_port = system.pageswaper.wc_side_port
        
        system.pageswaper.dp_side_port = system.dispatcher.ps_side_port

        #size setting
        system.remappingtable.dram_size = args.dramSize_size
        system.remappingtable.hbm_size = args.hbmSize_size

        system.wearlevelcontrol.dram_size = args.dramSize_size
        system.wearlevelcontrol.hbm_size = args.hbmSize_size

    elif args.mem_type == "DRAMsim3":
        system.physmem = DRAMsim3()
        # 4Gb
        # system.physmem.range = AddrRange("0","4294967296")
        # 8Gb 
        system.physmem.range = AddrRange("0","8589934592")
        # # hbm+dram = 256+2048M
        # system.physmem.range = AddrRange("0","2415919104")
        # hbm+dram = 16+512M
        # system.physmem.range = AddrRange("0","553648128")
        if args.dramsim3_ini is None:
            system.physmem.configFile = "ext/dramsim3/DRAMsim3/configs/DDR4_4Gb_x4_2400.ini"
            # system.physmem.configFile = "ext/dramsim3/DRAMsim3/configs/HBM_4Gb_x128.ini"
        else:
            system.physmem.configFile = args.dramsim3_ini
        system.physmem.port = system.membus.mem_side_ports
    else:
        MemConfig.config_mem(args, system)

    
    config_filesystem(system, args)

system.workload = SEWorkload.init_compatible(mp0_path)

if args.wait_gdb:
    for cpu in system.cpu:
        cpu.wait_for_remote_gdb = True
    system.workload.wait_for_remote_gdb = True

root = Root(full_system = False, system = system)
Simulation.run(args, root, system, FutureClass)
