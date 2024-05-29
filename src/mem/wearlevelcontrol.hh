/**
 * @file
 * wearlevelcontrol.hh
 * Last modify Time:2022.3.17
 * Author:zpy
 */

#ifndef __WEARLEVEL_CONTROL_HH__
#define __WEARLEVEL_CONTROL_HH__

#include "mem/port.hh"
#include "params/WearLevelControl.hh"
#include "sim/sim_object.hh"
#include "sim/stats.hh"
#include "sim/system.hh"
#include "mem/dispatcher.hh"
#include "mem/remappingtable.hh"
#include <queue>
#include <deque>
#include <utility>
#include "base/statistics.hh"

//AccessCounter
//热度监测器是一个进口，一个出口
//改为wearlevel控制器
namespace gem5 {

class System;

namespace memory
{

class Dispatcher;
class RemappingTable;

class WearLevelControl : public SimObject {
  private:

//类可能要改
    class DpSidePort : public ResponsePort {
      private:
        WearLevelControl& wearlevelcontrol;
        bool needRetry;
        bool blocked;
      public:
        DpSidePort(const std::string& _name, WearLevelControl& _wearlevelcontrol);
        AddrRangeList getAddrRanges() const override;
        bool sendPacket(PacketPtr pkt);//发送resp
        void trySendRetry();
      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
    };

     class PsSidePort : public RequestPort {
      private:
        WearLevelControl& wearlevelcontrol;
    
        PacketPtr blockedPacket;
      public:
        PsSidePort(const std::string& _name, WearLevelControl& _wearlevelcontrol);
        bool blocked() { return (blockedPacket != nullptr); }
        void sendPacket(PacketPtr pkt);

      protected:
        bool recvTimingResp(PacketPtr pkt) override;//接受从Ps发送的响应
        void recvReqRetry() override;
        void recvRangeChange() override;
    };
    //
    DpSidePort dp_side_port;
    PsSidePort ps_side_port;
    // EventFunctionWrapper event; // accesscounter event
    // void processEvent();

    bool dp_side_blocked;
    bool ps_side_blocked;//控制能否进行磨损均衡

    AddrRangeList getAddrRanges() const;
    void sendRangeChange();
    Tick handleAtomic(PacketPtr pkt);
    void handleFunctional(PacketPtr pkt);
    bool handleDpRequest(PacketPtr pkt); 

    bool handlePsResponse(PacketPtr pkt);//
    void handleDpReqRetry();//要求dp重发 响应
    void handlePsRespRetry();//要求ps重发 响应
    void sendPsReq(PacketPtr pkt);//正常迁移req
    // void updatehctable(uint64_t page);
    EventFunctionWrapper event;
    void processEvent();

  public:

    int wearlevel_threshold;//页面热度的迁移阈值
    int write_count;
    //modify 8.24
    uint64_t MaxDrampage;//dram最大页号
    uint64_t Coldpage;//写入最少的页
    uint64_t Hotpage;//写入最多的页
    
    Tick adjust_latency;//迁移阈值调整时延
    void startup(); //
    //每次找到写入次数最多和最少的交换即可
    std::vector<uint64_t> CounterMap;//访问计数 记录dram页面以及对应的热度
    int PageSize;

    WearLevelControl(const WearLevelControlParams &params);
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    // overhead
    System* system() const { return _system; }
    void system(System *sys) { _system = sys; }

    Dispatcher* dispatcher() const { return _dp; }
    void dispatcher(Dispatcher *dp) { _dp = dp; }

    RemappingTable* remappingtable() const { return _rt; }
    void remappingtable(RemappingTable *rt) { _rt = rt; }

  protected:

    System *_system;
    Dispatcher* _dp;
    RemappingTable* _rt;
    // MigrationManager* _mm;
    PageSwaper* _ps;
    struct MemStats : public statistics::Group {
      MemStats(WearLevelControl &_wc);

      void regStats() override;

      const WearLevelControl &wc;

      /** Number of second remap*/
      // statistics::Vector numofSecondRemap;
      /** Number of ac packets*/
      // statistics::Vector numofAcPackets;

      statistics::Vector pagelife;//页面寿命
    } stats;
    // overhead 
};


} // namespace memory
} // namespace gem5

#endif