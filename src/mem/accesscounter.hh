/**
 * @file
 * AccessCounter.hh
 * Last modify Time:2022.3.17
 * Author:zpy
 */

#ifndef __MEM_ACCESSCOUNTER_HH__
#define __MEM_ACCESSCOUNTER_HH__

#include "mem/port.hh"
#include "params/AccessCounter.hh"
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

namespace gem5 {

class System;

namespace memory
{

class Dispatcher;
class RemappingTable;
class LRUhctable{
private:
    uint64_t capacity_;
    std::list<std::pair<uint64_t,std::pair<uint64_t,uint64_t>>> cache;
    std::unordered_map<uint64_t,std::list<std::pair<uint64_t,std::pair<uint64_t,uint64_t> >>::iterator> map;
public:
    LRUhctable(uint64_t capacity){
      this->capacity_ = capacity;
    }
    //更新计数表
    void updatehctable(uint64_t key, bool iswrite){
        std::pair<uint64_t,std::pair<uint64_t,uint64_t>> newkey = *map[key];
        newkey.second.second ++;
        if(iswrite){
          newkey.second.first++;
        }
        cache.erase(map[key]);
        cache.push_front(newkey);
        map[key] = cache.begin();
    }
    //插入新表项
    void put(uint64_t key, uint64_t value1,uint64_t value2) {
        if(map.find(key)!=map.end()){
            cache.erase(map[key]);//如果找到了就先把它删了之后再写入
            map.erase(key);
        }
        else{
            if(map.size() == capacity_){//如果已经满了，则要删除队尾元素
                map.erase((cache.back()).first);
                cache.pop_back();//cache和map中数据都要删除
            }
        }
        cache.push_front(std::make_pair(key,std::make_pair(value1,value2)));
        map[key] = cache.begin();
    }
    void del(uint64_t key) {
      if(map.find(key)!=map.end()){
          cache.erase(map[key]);//如果找到了就删除
          map.erase(key);
      }
    }
    uint64_t gethotness(uint64_t key){ //获取热度
      if(map.find(key) == map.end()){
        return -1;
      }
      else{
        std::pair<uint64_t,std::pair<uint64_t,uint64_t>> newkey = *map[key];
        return newkey.second.second;
      }
    }
    uint64_t getwrite(uint64_t key){  //获取写计数
      if(map.find(key) == map.end()){
        return -1;
      }
      else{
        std::pair<uint64_t,std::pair<uint64_t,uint64_t>> newkey = *map[key];
        return newkey.second.first;
      }
    }
    // 判断计数表中是否有key对应的表项
    bool haspage(uint64_t key){
      return map.find(key)!=map.end();
    }

};
class AccessCounter : public SimObject {
  private:

//类可能要改
    class DpSidePort : public ResponsePort {
      private:
        AccessCounter& accesscounter;
        bool needRetry;
        bool blocked;
      public:
        DpSidePort(const std::string& _name, AccessCounter& _accesscounter);
        AddrRangeList getAddrRanges() const override;
        bool sendPacket(PacketPtr pkt);//发送resp
        void trySendRetry();
      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
    };

     class MmSidePort : public RequestPort {
      private:
        AccessCounter& accesscounter;
        bool needRetry;
        bool blocked;
      public:
        MmSidePort(const std::string& _name, AccessCounter& _accesscounter);
        bool sendPacket(PacketPtr pkt);
        void trySendRetry();
      protected:
        bool recvTimingResp(PacketPtr pkt) override;//接受从Mm发送的响应
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    DpSidePort dp_side_port;
    //MmSidePort mm_side_port;
    EventFunctionWrapper event; // accesscounter event
    void processEvent();

    bool dp_side_blocked;
    // bool mm_side_blocked;

    AddrRangeList getAddrRanges() const;
    void sendRangeChange();
    Tick handleAtomic(PacketPtr pkt);
    void handleFunctional(PacketPtr pkt);
    bool handleDpRequest(PacketPtr pkt); 


    bool handleMmResponse(PacketPtr pkt);
    void handleDpReqRetry();
    void handleMmRespRetry();
    bool sendMigraReq(PacketPtr pkt);//正常迁移req
    void updatehctable(uint64_t page);
    void adjustMmThre();//调整迁移阈值
    void controlHBMVolume(); //控制HBM容量大小 lyh mldify 220503

  public:

    int migration_threshold;//页面热度的迁移阈值

    const uint64_t MaxHbmpage  ;//hbm的最大页号
    //modify 8.24
    uint64_t MaxDrampage;//dram最大页号
    
    //modify 2022.1.19
    // uint64_t MaxNvmpage;//dram最大页号
    uint64_t victimpage;//当前可被选择的冷页页号
    uint64_t VictimHbmpage;//判断hbm是否用完
    //modify 2022.1.19
    uint64_t ReturnDrampage;
    uint64_t ReturnHbmpage;
    // uint64_t ReturnNvmpage;
    uint64_t maxhctablesize;//MEA计数器大小
    Tick adjust_latency;//迁移阈值调整时延
    void startup(); //yhl modify
    bool ishot(uint64_t Page); //是否为热页
    uint64_t PreviousNetBenefit;//前一间隔净收益
    uint64_t last_action;//前一间隔措施


    LRUhctable* hctable_dram;//记录dram页面以及对应的热度
    // LRUhctable* hctable_nvm;//记录nvm页面以及对应的热度
    std::unordered_map<uint64_t,std::pair<uint32_t,uint32_t>> dram_hctable;//记录dram页面以及对应的热度
    // std::unordered_map<uint64_t,std::pair<uint32_t,uint32_t>> nvm_hctable;//记录nvm页面以及对应的热度
    bool ismigrating;
    int PageSize;
  
    //modify 2022.3.17
    void ResetDrampage(PacketPtr pkt);//对某个pkt的dram页面清零
    void ResetOneDrampage(uint64_t drampage);//对某个指定的dram页面清零

    AccessCounter(const AccessCounterParams &params);
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
    struct MemStats : public statistics::Group {
      MemStats(AccessCounter &_ac);

      void regStats() override;

      const AccessCounter &ac;

      /** Number of second remap*/
      statistics::Vector numofSecondRemap;
      /** Number of ac packets*/
      statistics::Vector numofAcPackets;
      /** Number of after warm up updates*/
      statistics::Vector numofAfterWarmupdate;//hjy add 9.9
       /** first complete warm up tick*/
      statistics::Vector tickofWarmedUp;//hjy add 9.9
    } stats;
    // overhead 
};


} // namespace memory
} // namespace gem5

#endif