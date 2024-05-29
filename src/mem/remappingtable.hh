/**

 */

#ifndef __MEM_REMAPPING_TAB_HH__
#define __MEM_REMAPPING_TAB_HH__

#include "mem/port.hh"
#include "params/RemappingTable.hh"
#include "sim/sim_object.hh"
#include "sim/stats.hh"
#include "sim/system.hh"
#include "mem/pageswaper.hh"
#include "mem/wearlevelcontrol.hh"
// #include "mem/accesscounter.hh"
// #include "mem/migrationmanager.hh"
#include <queue>
#include <time.h>
#include "base/statistics.hh"
// 只模拟4G
//crc modify 240514

//yhl modify 220501
//#define _MAXNVMPAGE  2097152
#define _MAXDRAMPAGE  4*1024*1024/4
#define _MAXHBMPAGE  256*1024/4
namespace gem5 {

class System;

namespace memory
{

// class AccessCounter; 
//class MigrationManager;
class WearLevelControl;
class PageSwaper;

class RemapCache {//缓存中的映射表
private:
    uint64_t cache_capacity;//映射表缓存容量,,
    uint64_t set_capacity;//映射表缓存集合大小，8路关联缓存，set数量
    std::vector<std::vector<std::pair<uint64_t,uint64_t> > > remap_cache;
    //tag 标志 cache组号 块内地址 ，8路组相联
public:
    RemapCache(uint64_t capacity,uint64_t capacity_set) {
        this->cache_capacity = capacity;//总大小组数
        //组内大小
        this->set_capacity = capacity_set;
        remap_cache.resize(this->cache_capacity);

    }
    // 访问key对应的remap值(重映射表中第二列的内容)
    uint64_t getremappage(uint64_t key) {
        uint64_t cache_pos = key % this->cache_capacity;
        for(int i = 0; i < remap_cache[cache_pos].size(); i++){
          if(remap_cache[cache_pos][i].first == key){
            return remap_cache[cache_pos][i].second;
          }
        }
        return -1;
    }
    // 将对应重映射表项插入
    void put(uint64_t key, uint64_t value) {
        uint64_t cache_pos = key % this->cache_capacity;
        for(int i = 0; i < remap_cache[cache_pos].size(); i++){
          if(remap_cache[cache_pos][i].first == key){//如果找到了，删除对应项
            remap_cache[cache_pos].erase(remap_cache[cache_pos].begin() + i);
          }
        }
        if(remap_cache[cache_pos].size() == set_capacity){//如果满了，随机删除一项
          int random_index = rand()% remap_cache[cache_pos].size();
          remap_cache[cache_pos].erase(remap_cache[cache_pos].begin() + random_index);
        }
        std::pair<uint64_t,uint64_t> newremap;
        newremap.first = key;
        newremap.second = value;
        remap_cache[cache_pos].push_back(newremap);//插入对应项
    }

    //更新重映射表
    void update(uint64_t key, uint64_t value){
        uint64_t cache_pos = key % this->cache_capacity;
        for(int i = 0; i < remap_cache[cache_pos].size(); i++){
          if(remap_cache[cache_pos][i].first == key){
            remap_cache[cache_pos][i].second = value;
          }
        }
    }

    //判断key对应的集合是否满
    bool hasfulled(uint64_t key){
        uint64_t cache_pos = key % this->cache_capacity;
        return remap_cache[cache_pos].size() == set_capacity;
    }
  
    // 判断重映射表中是否有key对应的表项
    bool haspage(uint64_t key){
      uint64_t cache_pos = key % this->cache_capacity;
      for(int i = 0; i < remap_cache[cache_pos].size(); i++){
          if(remap_cache[cache_pos][i].first == key){
            return true;
          }
      }
      return false;
    }
};

class RemapDram{//内存中的映射表
private:
    uint64_t total_capacity;
    std::vector<uint64_t>remap_dram;

public:
    RemapDram(uint64_t capacity){
      this->total_capacity = capacity;
      remap_dram.resize(this->total_capacity);
      for(int i=0;i<this->total_capacity;i++){
        remap_dram[i] = i ;
      }
    }
    uint64_t getremappage(uint64_t key){
      return remap_dram[key];
    }
    void update(uint64_t key,uint64_t value){
      remap_dram[key] = value;
    }
};


class RemappingTable : public SimObject {
  private:

//类可能要改
    class BusSidePort : public ResponsePort {//响应membus的请求
      private:
        RemappingTable& remappingtable;
        bool needRetry;
        // bool blocked;//rt - dp  rt-ps ps-rt  rt -membus
        PacketPtr blockedPacket;
      public:
        BusSidePort(const std::string& _name, RemappingTable& _remappingtable);
        bool blocked() { return (blockedPacket != nullptr); }
        AddrRangeList getAddrRanges() const override;
        void sendPacket(PacketPtr pkt);//向上发送包，
        void trySendRetry();//尝试要求对应端口重发
      protected:
      //不同的名称代表了不同的模拟方式，实际上只有timingreq用的到
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;//调用trysendretry
    };

    //这个类是跟pageswaper连接的端口
    class PsSidePort : public ResponsePort {//发送响应到ps的端口，
      private:
        RemappingTable& remappingtable;
        bool needRetry;

        PacketPtr blockedPacket;
      public:
        PsSidePort(const std::string& _name, RemappingTable& _remappingtable);
        AddrRangeList getAddrRanges() const override;
        void sendPacket(PacketPtr pkt);
        bool blocked() { return (blockedPacket != nullptr); }
        void trySendRetry();
      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
    };

    // class PsRequestPort : public RequestPort {//发送请求到ps端口，
    //   private:
    //     RemappingTable& remappingtable;
    //     bool needRetry;
    //     bool blocked;
    //     PacketPtr blockedPacket; //如果自己要造新包，自己就要保存，如果
    //   public:
    //     PsRequestPort(const std::string& _name, RemappingTable& _remappingtable);
    //     bool sendPacket(PacketPtr pkt);
    //     void trySendRetry();
    //   protected:
    //     bool recvTimingResp(PacketPtr pkt) override;
    //     void recvReqRetry() override;
    //     void recvRangeChange() override;
    // };

    class DpSidePort : public RequestPort {//向dp转发包
      private:
        RemappingTable& remappingtable;
        bool needRetry;
        PacketPtr blockedPacket;

      public:
        DpSidePort(const std::string& _name, RemappingTable& _remappingtable);
        bool blocked() { return (blockedPacket != nullptr); }
        
        void sendPacket(PacketPtr pkt);
        void trySendRetry();
      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;//下层发送消息告知可以重发了
        void recvRangeChange() override;
    };

    BusSidePort bus_side_port;
    //by crc 240515
    PsSidePort ps_side_port;//响应端口
    DpSidePort dp_side_port;//dp request
    // PsRequestPort ps_request_port;//发送模拟的访存，访问hbm指令
    
    //
    AddrRangeList getAddrRanges() const;
    void sendRangeChange();
    Tick handleAtomic(PacketPtr pkt);
    void handleFunctional(PacketPtr pkt);

    bool handleBusReq(PacketPtr pkt);//处理bus_side_port的req请求
    bool handlePsReq(PacketPtr pkt);//处理pageswaper 的req请求
    bool handleDpResponse(PacketPtr pkt);//处理dp_side_port的resp请求
    // bool handlePsResponse(PacketPtr pkt);//处理模拟后发回的回应
    void handleBusReqRetry();//尝试让membus重新发包
    void handleRespRetry();//尝试让dp重新发送包 
    void handlePsReqRetry();//处理
    void updateRemap(PacketPtr pkt);//更新rt
    void handleBlockedPkt();//处理由于迁移而被阻塞的pkt
    bool handleblockedqueue(PacketPtr pkt);//实际处理函数
    EventFunctionWrapper event;
    void processEvent();
    Tick adjust_latency;//迁移阈值调整时延
    

  public:

    bool bus_side_blocked;//阻止bus往下发包
    bool ps_side_blocked;//ps发req包
    bool dp_side_blocked;//控制dp发回的响应包能不能发送给membus，往上发包
    bool ps_request_blocked;//控制往ps发包
    const uint64_t PageSize;//页大小
    // const uint64_t MaxNVMPage ;   //yhl modify 220501
    const uint64_t MaxDRAMPage ;//dram最大页数
    const uint64_t MaxHBMPage;
    //只需要DRAM  =4G ，crc  hbm = 256m
    const uint64_t HBMsize;//
    const int blocked_threshold;//阻塞队列的阈值，超过阈值触发全局阻塞
    bool isblockedpkt;//判断现在处理的是否是blocked_pkt队列中的元素
    // uint64_t selectfreepage;//选择出来的空闲dram页
    // uint64_t selectcleanpage;//选择出来的干净dram页
    // uint64_t selectnvmpage; //选择出来的nvm页
    
    std::unordered_map<uint64_t,uint64_t> remap_table; //记录映射信息<页号,页号> 
    // std::unordered_map<uint64_t,std::pair<uint64_t,uint64_t>> remap_table;
    std::vector<uint64_t>  blocked_pagenums; //需要阻塞的两个页面(存放的是引起block或update的pkt的地址),大小为0的时候不阻塞
    std::queue<PacketPtr> blocked_pkt;//存放flat往下的需要被阻塞的pkt
    //std::vector<uint64_t> hbm_list;//管理空hbm项
    
    PacketPtr mempkt;//暂存访问的pkt
    void startup();
    RemapCache* remapcache;//指向cache中的映射表
    RemapDram* remapdram;//指向dram中映射表
    
    
    
    
    //发送读包，模拟延迟
    void SendaRead(uint64_t page);
    // ac模块直接调用 wanxx add 03.17
    // std::pair<uint64_t,std::pair<uint64_t,uint64_t>> VictDramCache(); //挑选替换cache替换页面
    // std::pair<uint64_t,uint64_t> VictDramFlat();  //挑选flat替换页面
    // void convert_dirty(uint64_t page); // dram_cache干净页变脏
    uint64_t selectdrampage; //挑选的dram flat页面

    RemappingTable(const RemappingTableParams &params);
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
  // overhead
    System* system() const { return _system; }
    void system(System *sys) { _system = sys; }
    WearLevelControl* wearlevelcontrol() const { return _wc; }
    void wearlevelcontrol(WearLevelControl *wc) { _wc = wc; }
    // MigrationManager* migrationmanager() const { return _mm; }
    // void migrationmanager(MigrationManager *mm) { _mm = mm; }
    PageSwaper* pageswaper()const{ return _ps;}
    void pageswaper(PageSwaper *ps){ _ps = ps;}
  protected:

    System *_system;//使用指针可以嵌套对象之间的关系
    //  AccessCounter* _ac;
    WearLevelControl * _wc;
    PageSwaper * _ps;
    // MigrationManager* _mm;
    struct MemStats : public statistics::Group {
      MemStats(RemappingTable &_remap);

      void regStats() override;

      const RemappingTable &remap;

      /** Number of remap_table hit*/
      statistics::Vector numofRemapHit;
      /** Number of remap_table miss*/
      statistics::Vector numofRemapMiss;
      /** Number of remap_table update*/
      statistics::Vector numofRemapUpdate;
      /** Sum of flat read packets*/
      statistics::Vector sumofreadreq;
       /** Sum of flat write packets*/
      statistics::Vector sumofwritereq;
    } stats;
    // overhead 
};


} // namespace memory
} // namespace gem5

#endif