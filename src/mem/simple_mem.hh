

/**
 * @file
 * SimpleMemory declaration
 */

#ifndef __MEM_SIMPLE_MEMORY_HH__
#define __MEM_SIMPLE_MEMORY_HH__


#include "base/statistics.hh"
#include "mem/abstract_mem.hh"
#include "mem/port.hh"
#include "params/SimpleMemory.hh"
#include "mem/climber.h"
#include "mem/twl.h"
#include "mem/mycache.hh"
#include "string.h"


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
    std::map<Addr, uint64_t> pageWriteCounts;
    //twlobj mytwlobj; // 假设 twlobj 有默认构造函数
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
    const Tick latency;
    //crc add 
    const Tick cache_latency;
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

    /**
     * Detemine the latency.
     *
     * @return the latency seen by the current packet
     */
    Tick getLatency() const;
    Tick getLatency_cache() const;

    /**
     * Upstream caches need this packet until true is returned, so
     * hold it for deletion until a subsequent call
     */
    std::unique_ptr<Packet> pendingDelete;

  public:
        // Stats
    //void collateStats();
 
    //void resetStats();
    //void print(std::ostream& out) const;

    void printAddrRanges() const; 

    SimpleMemory(const SimpleMemoryParams &p);
    //virtual ~SimpleMemory();
    
    DrainState drain() override;

    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;
    void init() override;

    const int CACHE1_SIZE_1 ;
    const int BUFFER_SIZE_1;
    const bool bool_cache ;
    const int what_twl ;
    const bool bool_fpag_mem ;
    const int swap_ ;
    const Tick FPGA_read_PCM;

    const Tick FPGA_write_PCM;
    
    climberobj myclimberobj;
    
    twlobj mytwlobj; // 假设 twlobj 有默认构造函数

    MemoryModel mm; 
    
    struct pagelife{//记录页寿命
      Addr addr_;
      double life;
      pagelife(Addr temp_,double temp_l):addr_(temp_),life(temp_l){}
    };
    vector<Addr>pageRemap;//自带的地址映射表，swap交换

    Addr queryfront(Addr addr_);
    //剩余部分代码
    int allwritecount=0;
  protected:
    Tick recvAtomic(PacketPtr pkt);
    Tick recvAtomicBackdoor(PacketPtr pkt, MemBackdoorPtr &_backdoor);
    void recvFunctional(PacketPtr pkt);
    void recvMemBackdoorReq(const MemBackdoorReq &req,
            MemBackdoorPtr &backdoor);
    bool recvTimingReq(PacketPtr pkt);
    void recvRespRetry();
    

    
    struct mycachehitStats : public statistics::Group
    {
        void regStats() override;
        mycachehitStats(statistics::Group *parent);
        statistics::Scalar hits;
        statistics::Scalar misses;
        statistics::Scalar total_num;
        statistics::Scalar total_num_read;
        statistics::Scalar total_num_swap1;
        statistics::Scalar total_num_swap2;
        statistics::Formula hitRatio;
        statistics::Vector pagelife;//页面寿命
        statistics::Scalar total_num_time;
    } stats;

};

} // namespace memory
} // namespace gem5

#endif //__MEM_SIMPLE_MEMORY_HH__