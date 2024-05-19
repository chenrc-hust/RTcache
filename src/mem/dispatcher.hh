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
// #include "mem/accesscounter.hh" //by crc 240514
#include "base/statistics.hh"
namespace gem5 {

class System;

namespace memory
{

// class AccessCounter;

class Dispatcher : public SimObject {
  public:
    enum PortType {//枚举端口的类型
      RemappingTable,
      MigrationManager,
      Accesscounter,
      PhysicalDram,
      PhysicalHbm,
      PhysicalNvm
    };

    enum BlockType {//枚举阻塞的类型
      Rt2Ac,    // Request packet from Remmaping Table   blocks Acess counter ，rt导致ac阻塞
      // Mm2Ac,    // Request packet from Migration Manager blocks Acess counter
      Rt2Hbm,   // Request packet from Remmaping Table   blocks Physical Hbm
      Mm2Hbm,   // Request packet from Migration Manager blocks Physical Hbm
      Rt2Dram,  // Request packet from Remmaping Table   blocks Physical Dram
      Mm2Dram,  // Request packet from Migration Manager blocks Physical Dram
      Rt2Nvm,   // Request packet from Remmaping Table   blocks Physical Nvm
      Mm2Nvm,   // Request packet from Migration Manager blocks Physical Nvm

      Dram2Rt,  // Response packet from Physical Dram    blocks Remmaping Table
      Hbm2Rt,   // Response packet from Physical Hbm     blocks Remmaping Table
      Nvm2Rt,   // Response packet from Physical Nvm     blocks Remmaping Table
      Dram2Mm,  // Response packet from Physical Dram    blocks Migration Manager
      Hbm2Mm,   // Response packet from Physical Hbm     blocks Migration Manager
      Nvm2Mm,   // Response packet from Physical Nvm     blocks Remmaping Table
      BlockTypeSize
    };

    
    PARAMS(Dispatcher);

    Dispatcher(const DispatcherParams &params);
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    // overhead
    System* system() const { return _system; }
    void system(System *sys) { _system = sys; }
    // AccessCounter* accesscounter() const { return _ac; }//获取计数器
    // void accesscounter(AccessCounter *ac) { _ac = ac; }
    uint64_t bandthofdram;//？带宽？
    uint64_t bandthofhbm;

  private:  
    class CpuSidePort : public ResponsePort {//名为cpuside连接到rt
      private:
        Dispatcher& disp;
        PortType porttype;
        bool needRetry;
        // bool blocked;
        //adds
        bool hbm_blocked;
        bool dram_blocked;
        // bool nvm_blocked;
        void setReqPort(PacketPtr pkt);
      public:
        CpuSidePort(const std::string& _name, Dispatcher& _disp, Dispatcher::PortType _type);
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
        Dispatcher& disp;
        PortType porttype;
        bool needRetry;
        bool blocked;//
        void setRespPort(PacketPtr pkt);
      public:
        MemSidePort(const std::string& _name, Dispatcher& _disp, Dispatcher::PortType _type);
        bool sendPacket(PacketPtr pkt);
        void trySendRetry();
      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    CpuSidePort rt_side_port;   // connect to remapping table
    //CpuSidePort mm_side_port;   // connect to migration manager

    // MemSidePort ac_side_port;   // connect to access counter
    MemSidePort hbm_side_port;  // connect to physical hbm
    MemSidePort dram_side_port; // connect to physical dram
    //MemSidePort nvm_side_port; // connect to physical nvm

    bool blocked[BlockType::BlockTypeSize];
    // const uint64_t maxhbmsize;
    // const uint64_t maxdramsize;
    // const uint64_t maxnvmsize;
    AddrRangeList getAddrRanges() const;
    void sendRangeChange();
    Tick handleAtomic(PacketPtr pkt);
    void handleFunctional(PacketPtr pkt);
    bool handleRequest(PacketPtr pkt);
    bool handleResponse(PacketPtr pkt);
    void handleReqRetry();
    void handleRespRetry();//
    bool isDramOrHbm(PacketPtr pkt);
    // bool isNvmOrcache(PacketPtr pkt);
    
  protected:

    System *_system;
    // AccessCounter* _ac;

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
      statistics::Vector numMM;
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