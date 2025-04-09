/*
 * Copyright (c) 2012-2013 ARM Limited
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

/**
 * @file
 * SimpleMemory declaration
 */

#ifndef __MEM_SIMPLE_MEMORY_HH__
#define __MEM_SIMPLE_MEMORY_HH__

#include <list>
#include "base/statistics.hh"

#include "mem/abstract_mem.hh"
#include "mem/port.hh"
#include "params/SimpleMemory.hh"
#include "learning_gem5/part2/RTcache/hit_test.hh"

class HitTest;

namespace gem5
{

namespace memory
{

/**
 * The simple memory is a basic single-ported memory controller with
 * a configurable throughput and latency.
 *
 * @sa  \ref gem5MemorySystem "gem5 Memory System"
 */
class SimpleMemory : public AbstractMemory
{

  private:

    /**
     * A deferred packet stores a packet along with its scheduled
     * transmission time
     */
    class DeferredPacket
    {

      public:

        const Tick tick;
        const PacketPtr pkt;

        DeferredPacket(PacketPtr _pkt, Tick _tick) : tick(_tick), pkt(_pkt)
        { }
    };

    class MemoryPort : public ResponsePort
    {
      private:
        SimpleMemory& mem;

      public:
        MemoryPort(const std::string& _name, SimpleMemory& _memory);

      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        Tick recvAtomicBackdoor(
                PacketPtr pkt, MemBackdoorPtr &_backdoor) override;
        void recvFunctional(PacketPtr pkt) override;
        void recvMemBackdoorReq(const MemBackdoorReq &req,
                MemBackdoorPtr &backdoor) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
        AddrRangeList getAddrRanges() const override;
    };

    MemoryPort port;

    /**
     * Latency from that a request is accepted until the response is
     * ready to be sent.
     */
    // const Tick latency;

    /**
     * Fudge factor added to the latency.
     */
    const Tick latency_var;

    /**
     * Internal (unbounded) storage to mimic the delay caused by the
     * actual memory access. Note that this is where the packet spends
     * the memory latency.
     */
    std::list<DeferredPacket> packetQueue;

    /**
     * Bandwidth in ticks per byte. The regulation affects the
     * acceptance rate of requests and the queueing takes place after
     * the regulation.
     */
    const double bandwidth;

    /**
     * Track the state of the memory as either idle or busy, no need
     * for an enum with only two states.
     */
    bool isBusy;

    /**
     * Remember if we have to retry an outstanding request that
     * arrived while we were busy.
     */
    bool retryReq;

    /**
     * Remember if we failed to send a response and are awaiting a
     * retry. This is only used as a check.
     */
    bool retryResp;

    /**
     * Release the memory after being busy and send a retry if a
     * request was rejected in the meanwhile.
     */
    void release();

    EventFunctionWrapper releaseEvent;

    /**
     * Dequeue a packet from our internal packet queue and move it to
     * the port where it will be sent as soon as possible.
     */
    void dequeue();

    EventFunctionWrapper dequeueEvent;
    
    const Tick latency;
    //读
    const Tick PCM_read_latency ;
    //写
    const Tick PCM_write_latency ;
    //
    const Tick eSRAM_read_latency ;
    
    const Tick eSRAM_write_latency ;
    //100 *1000
    //磨损均衡1 ： 256B , 4096 /256 = 16 
    // 默认要更新的两个页面都不在，  
    // 2*PCM_read_latency+ 32 *PCM_read_latency+32 *PCM_write_latency+2*PCM_write_latency
    const Tick WearLevel_1; 
    //磨损均衡1 ： 256B , 4096 /256 = 16 
    // 默认要更新的两个页面都不在，  
    // 3*PCM_read_latency+ 48 *PCM_read_latency+48 *PCM_write_latency+3*PCM_write_latency
    const Tick WearLevel_2;

    // 读 命中情况： eSRAM_read_latency+PCM_read_latency
    const Tick Read_hit;
    // 读 不命中 ： PCM_read_latency + eSRAM_write_latency + PCM_read_latency
    const Tick Read_miss;
    // 写 命中 ：eSRAM_read_latency + PCM_write_latency
    const Tick Write_hit;
    // 写 不命中： PCM_read_latency + eSRAM_write_latency + PCM_write_latency
    const Tick Write_miss;

    int hybird;//
    HitTest * Hitmodel;//缓存模型
    /**
     * Detemine the latency.
     *
     * @return the latency seen by the current packet
     */
    Tick getLatency() const;

    /**
     * Upstream caches need this packet until true is returned, so
     * hold it for deletion until a subsequent call
     */
    std::unique_ptr<Packet> pendingDelete;

  public:

    SimpleMemory(const SimpleMemoryParams &p);
    void LatencyAdderClean(){
      delete Hitmodel;
      std::cout<<"LatencyAdder destory"<<std::endl;
    }
    DrainState drain() override;

    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;
    void init() override;

  protected:
    Tick recvAtomic(PacketPtr pkt);
    Tick recvAtomicBackdoor(PacketPtr pkt, MemBackdoorPtr &_backdoor);
    void recvFunctional(PacketPtr pkt);
    void recvMemBackdoorReq(const MemBackdoorReq &req,
            MemBackdoorPtr &backdoor);
    bool recvTimingReq(PacketPtr pkt);
    void recvRespRetry();
    struct SimpleMemoryStats : public statistics::Group
    {
        SimpleMemoryStats(statistics::Group *parent);
        statistics::Scalar Write_hit;
        statistics::Scalar Read_hit;
        statistics::Scalar Write_miss;
        statistics::Scalar Read_miss;
        statistics::Scalar WearLevel_1;
        statistics::Scalar WearLevel_2;
        // statistics::Histogram missLatency;
        statistics::Formula hitRatio;
    } stats;
};

} // namespace memory
} // namespace gem5

#endif //__MEM_SIMPLE_MEMORY_HH__
