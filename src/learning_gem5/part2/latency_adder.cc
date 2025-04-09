/*
 * Copyright (c) 2017 Jason Lowe-Power
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "learning_gem5/part2/latency_adder.hh"

#include "base/compiler.hh"
#include "base/random.hh"
#include "debug/LatencyAdder.hh"
// #include "sim/system.hh"

namespace gem5
{

LatencyAdder::LatencyAdder(const LatencyAdderParams &params) :
    ClockedObject(params),
    latency(params.latency),
    PCM_read_latency(305*latency),
    PCM_write_latency(94*latency),
    eSRAM_read_latency(16*latency),
    eSRAM_write_latency(2*latency),
    WearLevel_1(2*PCM_read_latency+ 32*PCM_read_latency+32*PCM_write_latency+2*PCM_write_latency),
    WearLevel_2(3*PCM_read_latency+ 48 *PCM_read_latency+48 *PCM_write_latency+3*PCM_write_latency),
    Read_hit(eSRAM_read_latency+PCM_read_latency),
    Read_miss(PCM_read_latency + eSRAM_write_latency + PCM_read_latency),
    Write_hit(eSRAM_read_latency + PCM_write_latency),
    Write_miss(PCM_read_latency + eSRAM_write_latency + PCM_write_latency),
    capacity(params.size),
    cpuPort(params.name + ".cpu_side", this),
    memPort(params.name + ".mem_side", this),
    blocked_membus(false),blocked_memctrl(false),retryReq(false),
    releaseEvent([this]{ release(); }, name()),stats(this)
{
    
    Hitmodel = new HitTest(params.cache_type,params.wl_type,params.swap_time,params.hbm_size
                ,params.rt_group,params.pre,params.rt_dt
            );
    std::cout<<capacity<<std::endl;
    std::cout<<latency<<" Write_hit "<<Write_hit<<" WearLevel_2 "<<WearLevel_2<<std::endl;
}

Port &
LatencyAdder::getPort(const std::string &if_name, PortID idx)
{
    // This is the name from the Python SimObject declaration in LatencyAdder.py
    if (if_name == "mem_side") {
        panic_if(idx != InvalidPortID,
                 "Mem side of simple cache not a vector port");
        return memPort;
    } else if (if_name == "cpu_side" ) {
        // We should have already created all of the ports in the constructor
        return cpuPort;
    } else {
        // pass it along to our super class
        return ClockedObject::getPort(if_name, idx);
    }
}

bool 
LatencyAdder::CPUSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the cache is blocking.

    panic_if(blocked, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    DPRINTF(LatencyAdder, "Sending %s to CPU\n", pkt->print());
    if (!sendTimingResp(pkt)) {
        DPRINTF(LatencyAdder, "failed!\n");
        // blockedPacket = pkt;
        blocked  = true;
        return false;
    }else {
        trySendRetry();
        return true;
    }
}

AddrRangeList
LatencyAdder::CPUSidePort::getAddrRanges() const
{
    DPRINTF(LatencyAdder, "getAddrRanges\n");
    return owner->getAddrRanges();
}

void
LatencyAdder::CPUSidePort::trySendRetry()
{
    DPRINTF(LatencyAdder, "CPUSidePort trySendRetry. needRetry %d  blocked %d blocked_memctrl %d\n",needRetry,blocked ,owner->blocked_memctrl);
    if (needRetry && !blocked) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        owner->blocked_memctrl = false;
        DPRINTF(LatencyAdder, "Sending retry req.\n");
        sendRetryReq();
    }
}

void
LatencyAdder::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    DPRINTF(LatencyAdder, "recvFunctional %s to CPU\n", pkt->print());
    // Just forward to the cache.
    owner->handleFunctional(pkt);
}

bool
LatencyAdder::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(LatencyAdder, "Got request %s\n", pkt->print());
    
    if (blocked||needRetry) {
        // The cache may not be able to send a reply if this is blocked
        DPRINTF(LatencyAdder, "Request blocked\n");
        needRetry = true;
        // owner->memPort.sendPacket(pkt);
            // port.trySendRetry();
        return false;
    }
    // Just forward to the cache.
    if (!owner->handleRequest(pkt)) {
        DPRINTF(LatencyAdder, "Request failed\n");
        // stalling
        needRetry = true;
        return false;
    } else {
        DPRINTF(LatencyAdder, "Request succeeded\n");
        return true;
    }
}

void
LatencyAdder::CPUSidePort::recvRespRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blocked);

    // Grab the blocked packet.
    // PacketPtr pkt = blockedPacket;
    // blockedPacket = nullptr;
    blocked = false;
    DPRINTF(LatencyAdder, "recvRespRetry \n");
    // Try to resend it. It's possible that it fails again.
    // sendPacket(pkt);
    owner->blocked_membus = false;
    // We may now be able to accept new packets
    owner->memPort.trySendRetry();
}

bool
LatencyAdder::MemSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the cache is blocking.

    panic_if(blocked, "Should never try to send if blocked!");
    DPRINTF(LatencyAdder, "sendPacket pkt %s\n", pkt->print());
    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        // blockedPacket = pkt;
        DPRINTF(LatencyAdder, "MemSidePort sendPacket pkt %s fail\n", pkt->print());
        blocked = true;
        return false;
    }else{
        DPRINTF(LatencyAdder, "MemSidePort sendPacket success \n");
        owner->cpuPort.trySendRetry();
        return true;
    }
}

bool
LatencyAdder::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    // Just forward to the cache.
    DPRINTF(LatencyAdder, "recvTimingResp pkt %s\n", pkt->print());
    if(!owner->handleResponse(pkt)){
        needRetry = true;
        return false;
    }else{
        return true;
    }
}

void
LatencyAdder::MemSidePort::recvReqRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blocked);

    DPRINTF(LatencyAdder, "recvReqRetry\n");
    // Grab the blocked packet.
    
    blocked = false;
    // for (auto& port : owner->cpuPort) {
         
    // }
    owner->cpuPort.trySendRetry();
}

void
LatencyAdder::MemSidePort::recvRangeChange()
{
    DPRINTF(LatencyAdder, "recvRangeChange \n");
    owner->sendRangeChange();
}

void
LatencyAdder::MemSidePort::trySendRetry()
{
    if (needRetry && !blocked) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(LatencyAdder, "Sending retry req.\n");
        sendRetryResp();
    }
}

bool
LatencyAdder::handleRequest(PacketPtr pkt)
{
    if (retryReq){
         DPRINTF(LatencyAdder, "have a retryReq \n");
        return false;
    }
    if (blocked_memctrl) {
        // There is currently an outstanding request so we can't respond. Stall
        retryReq = true;
        DPRINTF(LatencyAdder, "Got request for addr %#x ,but handleRequest blocked \n", pkt->getAddr());
        return false;
    }

    DPRINTF(LatencyAdder, "Got request for addr %#x\n", pkt->getAddr());

    // This cache is now blocked waiting for the response to this packet.
    // blocked = true;
    blocked_memctrl = true;
    // uint64_t pkt_address = pkt->getAddr();

    std::pair<bool, int> result = Hitmodel->access(pkt->isWrite(),pkt->getAddr());
    
    Cycles This_time;
    if(result.first){
        This_time = pkt->isWrite()?Write_hit:Read_hit;
        if(pkt->isWrite())stats.Write_hit++;
        else stats.Read_hit++;
    }else{
        This_time = pkt->isWrite()?Write_miss:Read_miss;
        if(pkt->isWrite())stats.Write_miss++;
        else stats.Read_miss++;
    }
    if(result.second==1){
        This_time+=WearLevel_1;
        stats.WearLevel_1++;
    }else if(result.second==2){
        This_time+=WearLevel_2;
        stats.WearLevel_2++;
    }
    // Tick now = curTick();
    // Tick clock_edge = clockEdge(This_time);
    // std::cout<<clock_edge-now<<std::endl;
    schedule(releaseEvent,
        clockEdge(This_time));
    // }
    // memPort.sendPacket(pkt);
    // if(!memPort.sendPacket(pkt)){
    //     blocked_memctrl = true;
    //     return false;
    // }
    accessTiming(pkt);
    return true;
}

bool
LatencyAdder::handleResponse(PacketPtr pkt)
{
    // assert(blocked);
    DPRINTF(LatencyAdder, "Got response for addr %#x\n", pkt->getAddr());
    if(blocked_membus){
         DPRINTF(LatencyAdder, "but blocked_membus true");
        return  false;
    }

    if(!cpuPort.sendPacket(pkt)){
        blocked_membus = true;
        return false;
    }

    return true;
}

void
LatencyAdder::release()
{
    assert(blocked_memctrl);
    blocked_memctrl = false;
    if (retryReq) {
        retryReq = false;
        cpuPort.sendRetryReq();
    }
}
void
LatencyAdder::handleFunctional(PacketPtr pkt)
{
    DPRINTF(LatencyAdder, "handleFunctional %#x\n", pkt->getAddr());
    // if (accessFunctional(pkt)) {
    //     pkt->makeResponse();
    // } else {
        memPort.sendFunctional(pkt);
    // }
}

void
LatencyAdder::accessTiming(PacketPtr pkt)
{

    // stats.misses++; // update stats
    // if (addr == block_addr && size == blockSize) {
    //     // Aligned and block size. We can just forward.
    DPRINTF(LatencyAdder, "forwarding packet blocked_membus %d blocked_memctrl %d \n",blocked_membus,blocked_memctrl);
    if(!memPort.sendPacket(pkt)){
        DPRINTF(LatencyAdder, "Failed to send packet after delay\n");
        blocked_memctrl = true;
    } else {
        blocked_memctrl = false;
    }
}

AddrRangeList
LatencyAdder::getAddrRanges() const
{
    DPRINTF(LatencyAdder, "Sending new ranges\n");
    // Just use the same ranges as whatever is on the memory side.
    return memPort.getAddrRanges();
}

void
LatencyAdder::sendRangeChange() const
{
     DPRINTF(LatencyAdder, "sendRangeChange \n");
    // for (auto& port : cpuPort) {
        cpuPort.sendRangeChange();
    // }
}

LatencyAdder::LatencyAdderStats::LatencyAdderStats(statistics::Group *parent)
      : statistics::Group(parent),
      ADD_STAT(Write_hit, statistics::units::Count::get(), "Number of Write_hit"),
      ADD_STAT(Read_hit, statistics::units::Count::get(), "Number of Read_hit"),
      ADD_STAT(Write_miss, statistics::units::Count::get(), "Number of Write_miss"),
      ADD_STAT(Read_miss, statistics::units::Count::get(), "Number of Read_miss"),
      ADD_STAT(WearLevel_1, statistics::units::Count::get(), "Number of WearLevel_1"),
      ADD_STAT(WearLevel_2, statistics::units::Count::get(), "Number of WearLevel_2"),
    //   ADD_STAT(missLatency, statistics::units::Tick::get(),
    //            "Ticks for misses to the cache"),
      ADD_STAT(hitRatio, statistics::units::Ratio::get(),
               "The ratio of hits to the total accesses to the cache",
               (Write_hit+Read_hit) / (Write_hit+Read_hit+Write_miss+Read_miss))
{
    // missLatency.init(16); // number of buckets
}

} // namespace gem5
