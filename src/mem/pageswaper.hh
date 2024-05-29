/**
 * @file
 * pageswaper.hh
//by crc 240520
 */
#ifndef __MEM_PAGESWAPER_HH__
#define __MEM_PAGESWAPER_HH__

#include "mem/port.hh"
#include "params/PageSwaper.hh"
#include "sim/sim_object.hh"
#include "sim/stats.hh"
#include "sim/system.hh"
#include <queue>
#include "base/statistics.hh"
namespace gem5 {

class System;

namespace memory
{

class WearLevelControl;//


class PageSwaper : public SimObject {
    private:
        // class define : port 1, 接收wc发送的包，触发页面交换的迁移
        class WcSidePort : public ResponsePort {   // 
            private:
                PageSwaper& pageswaper;
                // bool blocked;
                bool needRetry;     // if need retry
                PacketPtr blockedPacket;
            public:
                WcSidePort(const std::string& _name, PageSwaper& _pageswaper);   // constructor
                bool blocked() { return (blockedPacket != nullptr); }
                AddrRangeList getAddrRanges() const override;
                void sendPacket(PacketPtr pkt);
                void trySendRetry();

            protected:
                Tick recvAtomic(PacketPtr pkt) override;
                void recvFunctional(PacketPtr pkt) override;
                bool recvTimingReq(PacketPtr pkt) override;
                void recvRespRetry() override; 
        };
        // // port 2, 接收RT发送的包，模拟访存时的映射表开销
        // // 修改后能触发迁移的只有  wl模块了 //by crc 240516
        // class RtResponsePort : public ResponsePort {   // receive the migration request from Access-counter
        //     private:
        //         PageSwaper& pageswaper;
        //         bool blocked;
        //         bool needRetry;     // if need retry
        //         PacketPtr blockedPacket;
           
        //     public:
        //         RtResponsePort(const std::string& _name, PageSwaper& _pageswaper);   // constructor
        //         AddrRangeList getAddrRanges() const override;
        //         void sendPacket(PacketPtr pkt);
        //         void trySendRetry();

        //     protected:
        //         Tick recvAtomic(PacketPtr pkt) override;
        //         void recvFunctional(PacketPtr pkt) override;
        //         bool recvTimingReq(PacketPtr pkt) override;
        //         void recvRespRetry() override; 
        // };
        //by crc 240520 需要的，防止迁移过程中某个页面被访问
        // Prot 3,向RT发送Request包，告知其某一次迁移已完成
        class RtRequestPort : public RequestPort {   // send message to remapping table
            private:
                PageSwaper& pageswaper;

                bool needRetry;     // 响应端口才有needretry，响应端口发送完响应后，尝试让request重发请求
                PacketPtr blockedPacket;
            public:
                RtRequestPort(const std::string& _name, PageSwaper& _pageswaper);   // constructor
                void sendPacket(PacketPtr pkt);
                bool blocked() { return (blockedPacket != nullptr); }
                
            protected:
                bool recvTimingResp(PacketPtr pkt) override;
                void recvReqRetry() override;
                void recvRangeChange() override;
        };
        //by crc 240520
        //port 4, 向DP发送页面交换需要的读写包
        class DispatcherSidePort : public RequestPort {     // send migratio request to dispatcher
              private:
                PageSwaper& pageswaper;
                PacketPtr blockedPacket;//


            public:
                DispatcherSidePort(const std::string& _name, PageSwaper& _pageswaper);
                // constructor
                bool blocked() { return (blockedPacket != nullptr); }
                void sendPacket(PacketPtr pkt);
                
            protected:
                bool recvTimingResp(PacketPtr pkt) override;
                void recvReqRetry() override;
                void recvRangeChange() override;
        };

        // declartion of the ports
        WcSidePort wc_side_port;//触发
        // RtResponsePort rt_response_port;//模拟页面访问
        RtRequestPort rt_request_port;//更新表
        DispatcherSidePort dp_side_port;//发包

        Tick handleAtomic(PacketPtr pkt);
        void handleFunctional(PacketPtr pkt);
        bool IsDispatcher(PacketPtr pkt); // 判断是哪个发来的请求（disp还是remapping table），响应
        // deal functions
        bool handleWCRequest(PacketPtr pkt);        // deal the request from wearlevel control
        // bool handleRTRequest(PacketPtr pkt); //模拟访问映射表延迟
        bool handleRTResponse(PacketPtr pkt);
        bool handleDispResponse(PacketPtr pkt);
        void swapPage();
        
        bool dramtodram(PacketPtr pkt);
        //不同情况下进行迁移
        // bool flatnvmtodram_hot(PacketPtr pkt);
        // bool flatnvmtodram_first(PacketPtr pkt); 
        // bool flatnvmtodram_second(PacketPtr pkt);
        // bool cachenvmtodram_free(PacketPtr pkt);
        // bool cachenvmtodram_clean(PacketPtr pkt);
        // bool cachenvmtodram_dirty(PacketPtr pkt);
        // extra func
        AddrRangeList getAddrRanges() const;
        void sendRangeChange();
        //by crc 240520 暂存待迁移包
        std::queue<PacketPtr> swap_pkts_qe;//不需要存包把？
        // std::queue<std::pair<int,int> > swap_pkts_qe;
        //by crc 240520 
        std::queue<PacketPtr> send_to_dram;
        PacketDataPtr page_1_buffer, page_2_buffer/* , nvm_buffer */;       // save the data that read from hbm, bhm;
        int max_queue_len;         // the max length of the queue
        int PageSize;
        int PSnumofcycles; // 发包循环数(阻塞时保护现场)
        bool blockpacket1;//dram read
        bool blockpacket2;//dram write
        bool blockpacket3;//nvm read
        bool blockpacket4;//nvm write
        // bool blockpacket5;//dram_cache read
        // bool blockpacket6;//dram_cache write
        bool PSblock;
        EventFunctionWrapper event;
        void processEvent();
        Tick adjust_latency;//迁移阈值调整时延
        int swap_count ; //交换访问计数


  public:
        PageSwaper(const PageSwaperParams &params);
        Port &getPort(const std::string &if_name,
                    PortID idx = InvalidPortID) override;
        // overhead
        System* system() const { return _system; }
        void system(System *sys) { _system = sys; }

        WearLevelControl* wearlevelcontrol() const { return _wc; }
        void wearlevelcontrol(WearLevelControl *wc) { _wc = wc; }

        RemappingTable* remappingtable() const {return _rt;}
        void remappingtable(RemappingTable *rt) {_rt = rt;}
        void startup();

  protected:

    System *_system;
    WearLevelControl* _wc;
    RemappingTable* _rt;
    
    struct MemStats : public statistics::Group {
      MemStats(PageSwaper &_ps);

      void regStats() override;

      const PageSwaper &ps;

     
      statistics::Vector SwapFromWc;
      
      statistics::Vector ReqFromWc;

    //   statistics::Vector Migraflathot;
      /** MM Number of packets of flat_first*/
    //   statistics::Vector Migraflatfir;
      /** MM Number of packets of flat_second*/
    //   statistics::Vector Migraflatsec;
      /** MM Number of packets of cache_free*/
    //   statistics::Vector Migracachefree;
      /** MM Number of packets of cache_clean*/
    //   statistics::Vector Migracachecl;
      /** MM Number of packets of cache_dirty*/
    //   statistics::Vector Migracachedir;
      /** MM Number of migration */
      statistics::Vector numofSwap;
      /** Average MM Number of packets from ac per second */
      statistics::Formula avPS;

    } stats;
    // overhead 

};

} // namespace memory
} // namespace gem5

#endif