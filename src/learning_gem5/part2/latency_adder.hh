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

#ifndef __LEARNING_GEM5_LATENCY_ADDER_HH__
#define __LEARNING_GEM5_LATENCY_ADDER_HH__

#include <unordered_map>

#include "base/statistics.hh"
#include "mem/port.hh"
#include "params/LatencyAdder.hh"
#include "sim/clocked_object.hh"
#include "learning_gem5/part2/RTcache/hit_test.hh"

class HitTest;

namespace gem5
{

/**
 * A very simple cache object. Has a fully-associative data store with random
 * replacement.
 * This cache is fully blocking (not non-blocking). Only a single request can
 * be outstanding at a time.
 * This cache is a writeback cache.
 */
class LatencyAdder : public ClockedObject
{
  private:

    /**
     * Port on the CPU-side that receives requests.
     * Mostly just forwards requests to the cache (owner)
     */
    class CPUSidePort : public ResponsePort
    {
      private:
        /// Since this is a vector port, need to know what number this one is
        int id;

        /// The object that owns this object (LatencyAdder)
        LatencyAdder *owner;

        /// True if the port needs to send a retry req.
        bool needRetry;

        /// If we tried to send a packet and it was blocked, store it here
        // PacketPtr blockedPacket;
        bool blocked;

      public:
        /**
         * Constructor. Just calls the superclass constructor.
         */
        CPUSidePort(const std::string& name, LatencyAdder *owner) :
            ResponsePort(name), owner(owner), needRetry(false),
            blocked(false)
            // blockedPacket(nullptr)
        { }

        /**
         * Send a packet across this port. This is called by the owner and
         * all of the flow control is hanled in this function.
         * This is a convenience function for the LatencyAdder to send pkts.
         *
         * @param packet to send.
         */
        bool sendPacket(PacketPtr pkt);

        /**
         * Get a list of the non-overlapping address ranges the owner is
         * responsible for. All response ports must override this function
         * and return a populated list with at least one item.
         *
         * @return a list of ranges responded to
         */
        AddrRangeList getAddrRanges() const override;

        /**
         * Send a retry to the peer port only if it is needed. This is called
         * from the LatencyAdder whenever it is unblocked.
         */
        void trySendRetry();

      protected:
        /**
         * Receive an atomic request packet from the request port.
         * No need to implement in this simple cache.
         */
        Tick recvAtomic(PacketPtr pkt) override
        { return owner->memPort.sendAtomic(pkt);}

        /**
         * Receive a functional request packet from the request port.
         * Performs a "debug" access updating/reading the data in place.
         *
         * @param packet the requestor sent.
         */
        void recvFunctional(PacketPtr pkt) override;

        /**
         * Receive a timing request from the request port.
         *
         * @param the packet that the requestor sent
         * @return whether this object can consume to packet. If false, we
         *         will call sendRetry() when we can try to receive this
         *         request again.
         */
        bool recvTimingReq(PacketPtr pkt) override;

        /**
         * Called by the request port if sendTimingResp was called on this
         * response port (causing recvTimingResp to be called on the request
         * port) and was unsuccessful.
         */
        void recvRespRetry() override;
    };

    /**
     * Port on the memory-side that receives responses.
     * Mostly just forwards requests to the cache (owner)
     */
    class MemSidePort : public RequestPort
    {
      private:
        /// The object that owns this object (LatencyAdder)
        LatencyAdder *owner;

        /// If we tried to send a packet and it was blocked, store it here
        // PacketPtr blockedPacket;
        
        bool needRetry;

        bool blocked;
      public:
        /**
         * Constructor. Just calls the superclass constructor.
         */
        MemSidePort(const std::string& name, LatencyAdder *owner) :
            RequestPort(name), owner(owner), blocked(false)
        { }

        /**
         * Send a packet across this port. This is called by the owner and
         * all of the flow control is hanled in this function.
         * This is a convenience function for the LatencyAdder to send pkts.
         *
         * @param packet to send.
         */
        bool sendPacket(PacketPtr pkt);
        
        void trySendRetry();
        // bool blocked(){
        //   return blockedPacket!=nullptr;
        // }
      protected:
        /**
         * Receive a timing response from the response port.
         */
        bool recvTimingResp(PacketPtr pkt) override;

        /**
         * Called by the response port if sendTimingReq was called on this
         * request port (causing recvTimingReq to be called on the response
         * port) and was unsuccesful.
         */
        void recvReqRetry() override;

        /**
         * Called to receive an address range change from the peer response
         * port. The default implementation ignores the change and does
         * nothing. Override this function in a derived class if the owner
         * needs to be aware of the address ranges, e.g. in an
         * interconnect component like a bus.
         */
        void recvRangeChange() override;
    };

    /**
     * Handle the request from the CPU side. Called from the CPU port
     * on a timing request.
     *
     * @param requesting packet
     * @param id of the port to send the response
     * @return true if we can handle the request this cycle, false if the
     *         requestor needs to retry later
     */
    bool handleRequest(PacketPtr pkt);

    /**
     * Handle the respone from the memory side. Called from the memory port
     * on a timing response.
     *
     * @param responding packet
     * @return true if we can handle the response this cycle, false if the
     *         responder needs to retry later
     */
    bool handleResponse(PacketPtr pkt);

    /**
     * Send the packet to the CPU side.
     * This function assumes the pkt is already a response packet and forwards
     * it to the correct port. This function also unblocks this object and
     * cleans up the whole request.
     *
     * @param the packet to send to the cpu side
     */
    // void sendResponse(PacketPtr pkt);
    void release();
    /**
     * Handle a packet functionally. Update the data on a write and get the
     * data on a read. Called from CPU port on a recv functional.
     *
     * @param packet to functionally handle
     */
    void handleFunctional(PacketPtr pkt);

    /**
     * Access the cache for a timing access. This is called after the cache
     * access latency has already elapsed.
     */
    void accessTiming(PacketPtr pkt);

    /**
     * This is where we actually update / read from the cache. This function
     * is executed on both timing and functional accesses.
     *
     * @return true if a hit, false otherwise
     */
    bool accessFunctional(PacketPtr pkt);


    /**
     * Return the address ranges this cache is responsible for. Just use the
     * same as the next upper level of the hierarchy.
     *
     * @return the address ranges this cache is responsible for
     */
    AddrRangeList getAddrRanges() const;

    /**
     * Tell the CPU side to ask for our memory ranges.
     */
    void sendRangeChange() const;

    /// Latency to check the cache. Number of cycles for both hit and miss
    const Cycles latency;
    //读
    const Cycles PCM_read_latency ;
    //写
    const Cycles PCM_write_latency ;
    //
    const Cycles eSRAM_read_latency ;
    
    const Cycles eSRAM_write_latency ;
    //100 *1000
    //磨损均衡1 ： 256B , 4096 /256 = 16 
    // 默认要更新的两个页面都不在，  
    // 2*PCM_read_latency+ 32 *PCM_read_latency+32 *PCM_write_latency+2*PCM_write_latency
    const Cycles WearLevel_1; 
    //磨损均衡1 ： 256B , 4096 /256 = 16 
    // 默认要更新的两个页面都不在，  
    // 3*PCM_read_latency+ 48 *PCM_read_latency+48 *PCM_write_latency+3*PCM_write_latency
    const Cycles WearLevel_2;

    // 读 命中情况： eSRAM_read_latency+PCM_read_latency
    const Cycles Read_hit;
    // 读 不命中 ： PCM_read_latency + eSRAM_write_latency + PCM_read_latency
    const Cycles Read_miss;
    // 写 命中 ：eSRAM_read_latency + PCM_write_latency
    const Cycles Write_hit;
    // 写 不命中： PCM_read_latency + eSRAM_write_latency + PCM_write_latency
    const Cycles Write_miss;
    //
    
 
    /// The block size for the cache
    // const Addr blockSize;

    /// Number of blocks in the cache (size of cache / block size)
    const unsigned capacity;

    /// Instantiation of the CPU-side port
    CPUSidePort cpuPort;

    /// Instantiation of the memory-side port
    MemSidePort memPort;

    /// True if this cache is currently blocked waiting for a response.
    bool blocked_membus;//停止接受resp 

    bool blocked_memctrl;//阻塞停止接受req

    bool retryReq;

    EventFunctionWrapper releaseEvent;
    
    HitTest * Hitmodel;//缓存模型


    /// Cache statistics
  protected:
    struct LatencyAdderStats : public statistics::Group
    {
        LatencyAdderStats(statistics::Group *parent);
        statistics::Scalar Write_hit;
        statistics::Scalar Read_hit;
        statistics::Scalar Write_miss;
        statistics::Scalar Read_miss;
        statistics::Scalar WearLevel_1;
        statistics::Scalar WearLevel_2;
        // statistics::Histogram missLatency;
        statistics::Formula hitRatio;
    } stats;

  public:

    /** constructor
     */
    LatencyAdder(const LatencyAdderParams &params);
    void LatencyAdderClean(){
      delete Hitmodel;
      std::cout<<"LatencyAdder destory"<<std::endl;
    }
    /**
     * Get a port with a given name and index. This is used at
     * binding time and returns a reference to a protocol-agnostic
     * port.
     *
     * @param if_name Port name
     * @param idx Index in the case of a VectorPort
     *
     * @return A reference to the given port
     */
    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;

};

} // namespace gem5

#endif // __LEARNING_GEM5_LATENCY_ADDER_HH__
