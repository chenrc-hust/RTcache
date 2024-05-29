/**
 * @file
 * dispatcher.hh
 * Last modify Time:2021.3.15
 * Author:hjy
 */
#ifndef __MEM_DISPATCHER_HH__
#define __MEM_DISPATCHER_HH__

#include <vector>
#include "mem/port.hh"
#include "params/Dispatcher.hh"
#include "sim/sim_object.hh"
#include "sim/stats.hh"
#include "sim/system.hh"
#include "mem/wearlevelcontrol.hh" //by crc 240514
#include "base/statistics.hh"
namespace gem5 {

class System;

namespace memory
{

//class AccessCounter;

class WearLevelControl;

class Dispatcher : public SimObject {
  public:
    enum PortType {//枚举端口的类型
      RemappingTable,
      MigrationManager,
      PageSwaper,
      Accesscounter,
      PhysicalDram,
      PhysicalHbm,
      PhysicalNvm,
      WearLevelcontrol//不能完全一样，
    };

    enum BlockType {//枚举阻塞的类型
      Rt2Wc,    // Request packet from Remmaping Table   blocks Acess counter ，rt导致ac阻塞
      Rt2Hbm,   // Request packet from Remmaping Table   blocks Physical Hbm
      Ps2Hbm,   // Request packet from Migration Manager blocks Physical Hbm
      Rt2Dram,  // Request packet from Remmaping Table   blocks Physical Dram
      Ps2Dram,  // Request packet from Migration Manager blocks Physical Dram

      Dram2Rt,  // Response packet from Physical Dram    blocks Remmaping Table
      Hbm2Rt,   // Response packet from Physical Hbm     blocks Remmaping Table
      Dram2Ps,  // Response packet from Physical Dram    blocks Migration Manager
      Hbm2Ps,   // Response packet from Physical Hbm     blocks Migration Manager
      BlockTypeSize
    };

    PARAMS(Dispatcher);

    Dispatcher(const DispatcherParams &params);
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    // overhead
    System* system() const { return _system; }
    void system(System *sys) { _system = sys; }
    
    WearLevelControl* wearlevelcontrol()const {return _wc;}
    void wearlevelcontrol(WearLevelControl *wc){_wc=wc;}
    void startup();
  private:  
    class CpuSidePort : public ResponsePort {//名为cpuside连接到rt
      private:
        Dispatcher& disp;
        PortType porttype;
        bool needRetry;
        PacketPtr blockedPacket;    
        void setReqPort(PacketPtr pkt);
      public:
        CpuSidePort(const std::string& _name, Dispatcher& _disp, Dispatcher::PortType _type);
        AddrRangeList getAddrRanges() const override;
        bool blocked() { return (blockedPacket != nullptr); }
        void sendPacket(PacketPtr pkt);
        void trySendRetry();
      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
    };

    class MemSidePort : public RequestPort {
      private:
        Dispatcher& disp;
        PortType porttype;
        bool needRetry;
        PacketPtr blockedPacket; 
        void setRespPort(PacketPtr pkt);
      public:
        MemSidePort(const std::string& _name, Dispatcher& _disp, Dispatcher::PortType _type);
        bool blocked() { return (blockedPacket != nullptr); }
        void sendPacket(PacketPtr pkt);
       
      
      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    CpuSidePort rt_side_port;   // connect to remapping table
    CpuSidePort ps_side_port;   // connect to pageswaper

    MemSidePort wc_side_port;   // connect to wearlevel counter 
    MemSidePort hbm_side_port;  // connect to physical hbm
    MemSidePort dram_side_port; // connect to physical dram
    //MemSidePort nvm_side_port; // connect to physical nvm

    bool blocked[BlockType::BlockTypeSize];

    AddrRangeList getAddrRanges() const;
    void sendRangeChange();
    Tick handleAtomic(PacketPtr pkt);
    void handleFunctional(PacketPtr pkt);
    bool handleRequest(PacketPtr pkt);
    bool handleResponse(PacketPtr pkt);
    void handleReqRetry();
    void handleRespRetry();//
    EventFunctionWrapper event;
    void processEvent();
    Tick adjust_latency;//迁移阈值调整时延

    // bool isNvmOrcache(PacketPtr pkt);
    
  protected:

    System *_system;
    WearLevelControl * _wc;

    struct MemStats : public statistics::Group {
      MemStats(Dispatcher &_disp);

      void regStats() override;

      const Dispatcher &disp;

      /** Number of bus dram packets  */
      statistics::Vector numBusDram;
      /** Number of bus hbm packets */
      statistics::Vector numBusHbm;
      /** Number of bus nvm packets */
      statistics::Vector numBusNvm;
      /** Number of access mm packets */
      statistics::Vector numPS;
      /** Number of packets dispatch */
      statistics::Vector numDisps;
      /** Number of packets bus dram read */
      statistics::Vector numBusDramRead;
      /** Number of packets bus hbm read */
      statistics::Vector numBusHbmRead;
      /** Number of packets bus nvm read */
      statistics::Vector numBusNvmRead;
      /** Average from bus dram number per second */
      statistics::Formula avNumBusDram;
      /** Average from bus hbm number per second */
      statistics::Formula avNumBusHbm;
      /** Average from bus nvm number per second */
      statistics::Formula avNumBusNvm;
      /** Average dispatch number per second */
      statistics::Formula avDisps;
    } stats;
    // overhead 

  
};

} // namespace memory
} // namespace gem5

#endif//_DISPATCHER_HH__