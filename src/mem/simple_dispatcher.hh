/**
 * @file
 * Simple_Dispatcher
 */
#ifndef __MEM_SIMPLE_DISPATCHER_HH__
#define __MEM_SIMPLE_DISPATCHER_HH__

#include "mem/port.hh"
#include "params/Simple_Dispatcher.hh"
#include "sim/sim_object.hh"
#include "sim/stats.hh"
#include "sim/system.hh"
#include "base/statistics.hh"

namespace gem5 {

class System;

namespace memory
{

class Simple_Dispatcher : public SimObject 
{
  public:
  enum PortType{
    SimpleDispatcher,
    PhysicalDram,
    PhysicalHbm
  };
  enum SMBlockType{//
    SM2DRAM,
    SM2HBM,

    DRAM2SM,
    HBM2SM,

    SMBlockTypeSize
  };
  PARAMS(Simple_Dispatcher);
  
  Simple_Dispatcher(const Simple_DispatcherParams &params);
  Port &getPort(const std::string &if_name,
                PortID idx = InvalidPortID) override;
  // overhead
    System* system() const { return _system; }
    void system(System *sys) { _system = sys; }

    std::unordered_map<uint64_t,Tick> PacketsLatency;//统计每一个从bus下来的包的时延 
  protected:

    System *_system;
    struct MemStats : public statistics::Group {
      MemStats(Simple_Dispatcher &_simple_dp);

      void regStats() override;

      const Simple_Dispatcher &simple_dp;

      /** Number of dram hit*/
      statistics::Vector numofdramHit;
      /** Number of hbm hit*/
      statistics::Vector numofhbmHit;
      /** Sum of bus packets latency*/
      statistics::Vector sumofpacketslatency;
      /** Average dispatch number per second */
      //Stats::Formula avDisps;
    } stats;
    // overhead 
  private:

    class BusSidePort : public ResponsePort {
      private:
        Simple_Dispatcher& simple_dispatcher;
        PortType porttype;
        // bool needRetry;
        bool needRetry;
        // PacketPtr blockedPacket;
        bool hbm_blocked;
        bool dram_blocked;
        void setReqPort(PacketPtr pkt);
      public:
        BusSidePort(const std::string& _name, Simple_Dispatcher& _simple_dispatch,Simple_Dispatcher::PortType _type);
        AddrRangeList getAddrRanges() const override;
        bool sendPacket(PacketPtr pkt);
        void trySendRetry();
      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
    };

    class MemSidePort : public RequestPort {
      private:
        Simple_Dispatcher& simple_dispatcher;
        PortType porttype;
        // bool hbm_needRetry;
        // bool dram_needRetry;
        bool needRetry;
        // PacketPtr blockedPacket;
        bool blocked;
        void setRespPort(PacketPtr pkt);
      public:
        MemSidePort(const std::string& _name, Simple_Dispatcher& _simple_dispatch,Simple_Dispatcher::PortType _type);
        bool sendPacket(PacketPtr pkt);
        void trySendRetry();
      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    BusSidePort bus_side_port;//响应membus的请求
    
    MemSidePort dram_side_port;//向dram请求数据
    MemSidePort hbm_side_port;//向hbm请求映射表
    EventFunctionWrapper event; // packets simple_dispatch event
    bool blocked[SMBlockType::SMBlockTypeSize];

    //bool bus_side_blocked
    //hjy modify 6.7
    bool busdram_side_blocked;
    bool bushbm_side_blocked;
    bool mem_side_blocked;
    uint64_t maxdramsize;


    AddrRangeList getAddrRanges() const;
    //AddrRangeList getHbmAddrRanges() const;
    void sendRangeChange();
    Tick handleAtomic(PacketPtr pkt);
    void handleFunctional(PacketPtr pkt);
    bool handleRequest(PacketPtr pkt);
    bool handleResponse(PacketPtr pkt);
    void handleReqRetry();
    void handleRespRetry();


    bool IsDramOrHbm(PacketPtr pkt);//false代表hbm，true代表dram.
    void processEvent();

    

};

} // namespace memory
} // namespace gem5

#endif//_SIMPLE_DISPATCHER_HH__