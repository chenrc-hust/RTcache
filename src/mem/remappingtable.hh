/**
 * @file
 * RemappingTable.hh
 * Last modify Time:2022.3.3
 * Author:wxx
 */

#ifndef __MEM_REMAPPING_TAB_HH__
#define __MEM_REMAPPING_TAB_HH__

#include "mem/port.hh"
#include "params/RemappingTable.hh"
#include "sim/sim_object.hh"
#include "sim/stats.hh"
#include "sim/system.hh"
//#include "mem/accesscounter.hh"
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

//class AccessCounter; 
//class MigrationManager;

class RemapCache {//缓存中的映射表
private:
    uint64_t cache_capacity;//映射表缓存容量,,
    uint64_t set_capacity;//映射表缓存集合大小，8路关联缓存
    std::vector<std::vector<std::pair<uint64_t,uint64_t> > > remap_cache;
    //tag 标志 cache组号 块内地址 ，8路组相联
public:
    RemapCache(uint64_t capacity,uint64_t capacity_set) {
        this->cache_capacity = capacity;
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
        bool blocked;
      public:
        BusSidePort(const std::string& _name, RemappingTable& _remappingtable);
        AddrRangeList getAddrRanges() const override;
        bool sendPacket(PacketPtr pkt);
        void trySendRetry();
      protected:
      //不同的名称代表了不同的模拟方式，实际上只有timingreq用的到
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
    };

    //这个类是跟mm连接的端口，里面会有一些特殊的处理
    // class MmSidePort : public ResponsePort {//发送响应的端口，mm暂时不用。
    //   private:
    //     RemappingTable& remappingtable;
    //     bool needRetry;
    //     bool blocked;
    //   public:
    //     MmSidePort(const std::string& _name, RemappingTable& _remappingtable);
    //     AddrRangeList getAddrRanges() const override;
    //     bool sendPacket(PacketPtr pkt);
    //     void trySendRetry();
    //   protected:
    //     Tick recvAtomic(PacketPtr pkt) override;
    //     void recvFunctional(PacketPtr pkt) override;
    //     bool recvTimingReq(PacketPtr pkt) override;
    //     void recvRespRetry() override;
    // };

    // class MmRequestPort : public RequestPort {//发送请求的端口，mm暂时不用。
    //   private:
    //     RemappingTable& remappingtable;
    //     bool needRetry;
    //     bool blocked;
    //     PacketPtr blockedPacket; //yhl modify
    //   public:
    //     MmRequestPort(const std::string& _name, RemappingTable& _remappingtable);
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
        bool blocked;
      public:
        DpSidePort(const std::string& _name, RemappingTable& _remappingtable);
        bool sendPacket(PacketPtr pkt);
        void trySendRetry();
      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    BusSidePort bus_side_port;
    //by crc 240515
    //MmSidePort mm_side_port;
    DpSidePort dp_side_port;
    //MmRequestPort mm_request_port;
    //
    AddrRangeList getAddrRanges() const;
    void sendRangeChange();
    Tick handleAtomic(PacketPtr pkt);
    void handleFunctional(PacketPtr pkt);

    bool handleBusReq(PacketPtr pkt);//处理bus_side_port的req请求
    // bool handleMmReq(PacketPtr pkt);//处理mm_side_port的req请求
    bool handleDpResponse(PacketPtr pkt);//处理dp_side_port的resp请求
    // bool handleMmResponse(PacketPtr pkt);//处理mm_request_portderesp请求
    void handleBusReqRetry();
    // void handleMmReqRetry();
    void handleRespRetry();
    // void handleMmResRetry();
    bool updateRemap(PacketPtr pkt);//更新rt
    void handleBlockedPkt();//处理由于迁移而被阻塞的pkt
    bool handleblockedqueue(PacketPtr pkt);
    EventFunctionWrapper event;
    void processEvent();
    Tick adjust_latency;//迁移阈值调整时延
    

  public:

    bool bus_side_blocked;
    // bool mm_side_blocked;
    bool dp_side_blocked;
    // bool mm_request_blocked;
    const uint64_t PageSize;
    const uint64_t MaxRemapSize;//
    const uint64_t CacheRemapSize;//
    const uint64_t CacheDramSize;// 用于缓存的DRAM页
    // uint64_t Cr_nvm;//访问nvm（迁移）次数
    // uint64_t Cw_nvm;//访问nvm（迁移）次数
    // uint64_t Cr_dram;//访问dram（迁移）次数
    // uint64_t Cw_dram;//访问dram（迁移）次数
    // uint64_t Migrafirst;//统计迁移次数
    // uint64_t Migrasecond;
    // uint64_t Migrafree;
    // uint64_t Migraclean;
    // uint64_t Migradirty;

    // const uint64_t MaxNVMPage ;   //yhl modify 220501
    const uint64_t MaxDRAMPage ;
    const uint64_t MaxHBMPage ;  //三种内存的最大页号 默认 NVM=8GB DRAM=1GB HBM=128MB
    //只需要DRAM  =4G ，crc  hbm = 256m
    int use_evict;

    const int blocked_threshold;//阻塞队列的阈值，超过阈值触发全局阻塞
    bool isblockedpkt;//判断现在处理的是否是blocked_pkt队列中的元素
    // uint64_t selectfreepage;//选择出来的空闲dram页
    // uint64_t selectcleanpage;//选择出来的干净dram页
    // uint64_t selectnvmpage; //选择出来的nvm页
    
    std::unordered_map<uint64_t,uint64_t> remap_table; //记录映射信息<页号,页号> 
    // std::unordered_map<uint64_t,std::pair<uint64_t,uint64_t>> remap_table;
    // std::vector<uint64_t>  blocked_pagenums; //需要阻塞的两个页面(存放的是引起block或update的pkt的地址),大小为0的时候不阻塞
    std::queue<PacketPtr> blocked_pkt;//存放flat往下的需要被阻塞的pkt
    //std::vector<uint64_t> hbm_list;//管理空hbm项

    void startup(); //yhl modify

    RemapCache* remapcache;//指向cache中的映射表
    RemapDram* remapdram;//指向dram中映射表
    // std::queue<int> FreeDramPage;// 记录空闲DRAM页
    // std::vector<int> CleanDramPage;// 记录干净DRAM页
    void sendtodram(uint64_t page,PacketPtr pkt); //发送dram读包，模拟延迟
    
    std::vector<int> DramMap;// 记录每一页DRAM的映射
   
   
    // ac模块直接调用 wanxx add 03.17
    std::pair<uint64_t,std::pair<uint64_t,uint64_t>> VictDramCache(); //挑选替换cache替换页面
    std::pair<uint64_t,uint64_t> VictDramFlat();  //挑选flat替换页面
    uint64_t getremapaddr(uint64_t key);  //获取映射地址
    // void convert_dirty(uint64_t page); // dram_cache干净页变脏
    uint64_t selectdrampage; //挑选的dram flat页面

    RemappingTable(const RemappingTableParams &params);
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
  // overhead
    System* system() const { return _system; }
    void system(System *sys) { _system = sys; }
    // AccessCounter* accesscounter() const { return _ac; }
    // void accesscounter(AccessCounter *ac) { _ac = ac; }
    // MigrationManager* migrationmanager() const { return _mm; }
    // void migrationmanager(MigrationManager *mm) { _mm = mm; }
  protected:

    System *_system;//使用指针可以嵌套对象之间的关系
    //  AccessCounter* _ac;
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