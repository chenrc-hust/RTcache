/*
 * Copyright (c) 2010-2013, 2015 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2001-2005 The Regents of The University of Michigan
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

#include "mem/simple_mem.hh"

#include "base/random.hh"
#include "base/trace.hh"
#include "debug/Drain.hh"

namespace gem5
{

namespace memory
{

SimpleMemory::SimpleMemory(const SimpleMemoryParams &p) :
    AbstractMemory(p),
    port(name() + ".port", *this), 
    latency_var(p.latency_var), bandwidth(p.bandwidth), isBusy(false),
    retryReq(false), retryResp(false),
    releaseEvent([this]{ release(); }, name()),
    dequeueEvent([this]{ dequeue(); }, name()),
    latency(p.latency),
    PCM_read_latency(305*latency),
    PCM_write_latency(94*latency),
    eSRAM_read_latency(16*latency),
    eSRAM_write_latency(2*latency),
    WearLevel_1(2*PCM_read_latency+ 32*PCM_read_latency+32*PCM_write_latency+2*PCM_write_latency),
    WearLevel_2(3*PCM_read_latency+ 48 *PCM_read_latency+48 *PCM_write_latency+3*PCM_write_latency),
    Read_hit(eSRAM_read_latency+PCM_read_latency),
    Read_miss(PCM_read_latency + eSRAM_write_latency + PCM_read_latency),
    Write_hit(eSRAM_read_latency + PCM_write_latency),
    Write_miss(PCM_read_latency + eSRAM_write_latency + PCM_write_latency),hybird(p.hybird),
    stats(this)
{
    if(hybird){
        Hitmodel = new HitTest(p.cache_type,p.wl_type,p.swap_time,p.hbm_size
            ,p.rt_group,p.pre,p.rt_dt
        );
    }
    // std::cout<<capacity<<std::endl;
    std::cout<<latency<<" Write_hit "<<Write_hit<<" WearLevel_2 "<<WearLevel_2<<std::endl;
}

void
SimpleMemory::init()
{
    AbstractMemory::init();

    // allow unconnected memories as this is used in several ruby
    // systems at the moment
    if (port.isConnected()) {
        port.sendRangeChange();
    }
}

Tick
SimpleMemory::recvAtomic(PacketPtr pkt)
{
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    access(pkt);
    return getLatency();
}

Tick
SimpleMemory::recvAtomicBackdoor(PacketPtr pkt, MemBackdoorPtr &_backdoor)
{
    Tick latency = recvAtomic(pkt);
    getBackdoor(_backdoor);
    return latency;
}

void
SimpleMemory::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());

    functionalAccess(pkt);

    bool done = false;
    auto p = packetQueue.begin();
    // potentially update the packets in our packet queue as well
    while (!done && p != packetQueue.end()) {
        done = pkt->trySatisfyFunctional(p->pkt);
        ++p;
    }

    pkt->popLabel();
}

void
SimpleMemory::recvMemBackdoorReq(const MemBackdoorReq &req,
        MemBackdoorPtr &_backdoor)
{
    getBackdoor(_backdoor);
}

bool
SimpleMemory::recvTimingReq(PacketPtr pkt)
{
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see read and writes at memory controller, "
             "saw %s to %#llx\n", pkt->cmdString(), pkt->getAddr());

    // we should not get a new request after committing to retry the
    // current one, but unfortunately the CPU violates this rule, so
    // simply ignore it for now
    if (retryReq)
        return false;

    // if we are busy with a read or write, remember that we have to
    // retry
    if (isBusy) {
        retryReq = true;
        return false;
    }

    // technically the packet only reaches us after the header delay,
    // and since this is a memory controller we also need to
    // deserialise the payload before performing any write operation
    Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;
    // DPRINTF(Drain, "receive_delay %d\n",receive_delay);
    receive_delay = 0; 
    // update the release time according to the bandwidth limit, and
    // do so with respect to the time it takes to finish this request
    // rather than long term as it is the short term data rate that is
    // limited for any real memory
    
    // calculate an appropriate tick to release to not exceed
    // the bandwidth limit
    Tick duration = pkt->getSize() * bandwidth;
    // 首先读写都有延迟
    if(hybird){
        std::pair<bool, int> result = Hitmodel->access(pkt->isWrite(),pkt->getAddr());
        
        // Tick This_time;
        if(result.first){
            duration = pkt->isWrite()?Write_hit:Read_hit;
            if(pkt->isWrite())stats.Write_hit++;
            else stats.Read_hit++;
        }else{
            duration = pkt->isWrite()?Write_miss:Read_miss;
            if(pkt->isWrite())stats.Write_miss++;
            else stats.Read_miss++;
        }
        if(result.second==1){
            duration+=WearLevel_1;
            stats.WearLevel_1++;
        }else if(result.second==2){
            duration+=WearLevel_2;
            stats.WearLevel_2++;
        }
    }else{
        duration = pkt->isWrite()?PCM_write_latency:PCM_read_latency;
    }
    
    // only consider ourselves busy if there is any need to wait
    // to avoid extra events being scheduled for (infinitely) fast
    // memories
    if (duration != 0) {
        schedule(releaseEvent, curTick() + duration);
        isBusy = true;
    }

    // go ahead and deal with the packet and put the response in the
    // queue if there is one
    bool needsResponse = pkt->needsResponse();
    recvAtomic(pkt);
    // turn packet around to go back to requestor if response expected
    // 只有读需要响应延迟，
    if (needsResponse) {
        // recvAtomic() should already have turned packet into
        // atomic response
        assert(pkt->isResponse());

        Tick when_to_send = curTick() + receive_delay + getLatency();

        // when_to_send = 0;// 取消其他延迟。
        // typically this should be added at the end, so start the
        // insertion sort with the last element, also make sure not to
        // re-order in front of some existing packet with the same
        // address, the latter is important as this memory effectively
        // hands out exclusive copies (shared is not asserted)
        auto i = packetQueue.end();
        --i;
        while (i != packetQueue.begin() && when_to_send < i->tick &&
               !i->pkt->matchAddr(pkt))
            --i;

        // emplace inserts the element before the position pointed to by
        // the iterator, so advance it one step
        packetQueue.emplace(++i, pkt, when_to_send);

        if (!retryResp && !dequeueEvent.scheduled())
            schedule(dequeueEvent, packetQueue.back().tick);
    } else {
        pendingDelete.reset(pkt);
    }

    return true;
}

void
SimpleMemory::release()
{
    assert(isBusy);
    isBusy = false;
    if (retryReq) {
        retryReq = false;
        port.sendRetryReq();
    }
}

void
SimpleMemory::dequeue()
{
    assert(!packetQueue.empty());
    DeferredPacket deferred_pkt = packetQueue.front();

    retryResp = !port.sendTimingResp(deferred_pkt.pkt);

    if (!retryResp) {
        packetQueue.pop_front();

        // if the queue is not empty, schedule the next dequeue event,
        // otherwise signal that we are drained if we were asked to do so
        if (!packetQueue.empty()) {
            // if there were packets that got in-between then we
            // already have an event scheduled, so use re-schedule
            reschedule(dequeueEvent,
                       std::max(packetQueue.front().tick, curTick()), true);
        } else if (drainState() == DrainState::Draining) {
            DPRINTF(Drain, "Draining of SimpleMemory complete\n");
            signalDrainDone();
        }
    }
}

Tick
SimpleMemory::getLatency() const
{
    return latency +
        (latency_var ? random_mt.random<Tick>(0, latency_var) : 0);
}

void
SimpleMemory::recvRespRetry()
{
    assert(retryResp);

    dequeue();
}

Port &
SimpleMemory::getPort(const std::string &if_name, PortID idx)
{
    if (if_name != "port") {
        return AbstractMemory::getPort(if_name, idx);
    } else {
        return port;
    }
}

DrainState
SimpleMemory::drain()
{
    if (!packetQueue.empty()) {
        DPRINTF(Drain, "SimpleMemory Queue has requests, waiting to drain\n");
        return DrainState::Draining;
    } else {
        return DrainState::Drained;
    }
}

SimpleMemory::MemoryPort::MemoryPort(const std::string& _name,
                                     SimpleMemory& _memory)
    : ResponsePort(_name), mem(_memory)
{ }

AddrRangeList
SimpleMemory::MemoryPort::getAddrRanges() const
{
    AddrRangeList ranges;
    ranges.push_back(mem.getAddrRange());
    return ranges;
}

Tick
SimpleMemory::MemoryPort::recvAtomic(PacketPtr pkt)
{
    return mem.recvAtomic(pkt);
}

Tick
SimpleMemory::MemoryPort::recvAtomicBackdoor(
        PacketPtr pkt, MemBackdoorPtr &_backdoor)
{
    return mem.recvAtomicBackdoor(pkt, _backdoor);
}

void
SimpleMemory::MemoryPort::recvFunctional(PacketPtr pkt)
{
    mem.recvFunctional(pkt);
}

void
SimpleMemory::MemoryPort::recvMemBackdoorReq(const MemBackdoorReq &req,
        MemBackdoorPtr &backdoor)
{
    mem.recvMemBackdoorReq(req, backdoor);
}

bool
SimpleMemory::MemoryPort::recvTimingReq(PacketPtr pkt)
{
    return mem.recvTimingReq(pkt);
}

void
SimpleMemory::MemoryPort::recvRespRetry()
{
    mem.recvRespRetry();
}

SimpleMemory::SimpleMemoryStats::SimpleMemoryStats(statistics::Group *parent)
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

} // namespace memory

} // namespace gem5
