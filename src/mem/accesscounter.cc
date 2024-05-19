/**
 * @file
 * AccessCounter.cc
 * Last modify Time:2022.3.17
 * Author:zpy
 */

#include "base/trace.hh"
#include "mem/accesscounter.hh"
#include "debug/AccessCounter.hh"
//To be added 阈值调整部分

namespace gem5 {

namespace memory
{

AccessCounter::AccessCounter(const AccessCounterParams &params)
    : SimObject(params),
      dp_side_port(name() + ".dp_side_port",*this),
    //   mm_side_port(name() + ".mm_side_port", *this),
      event([this]{processEvent();}, name()),
      dp_side_blocked(false),
    //   mm_side_blocked(false),
    //   migration_threshold(16),
      last_action(0),
      PreviousNetBenefit(0),
    //   //py 0317 三种内存的最大页号 默认 NVM=8GB DRAM=1GB HBM=256MB
    //   MaxHbmpage (65536),
    //   MaxDrampage(262144),

    // HBM-16M  DRAM-64G
      MaxHbmpage(params.hbm_size/(4*1024)),  //yhl modify HBM-4M  DRAM-16M
      MaxDrampage(params.dram_size/(4*1024)),
    //   MaxNvmpage (params.nvm_size/(4*1024)),  

      maxhctablesize(params.nvm_size/(1024*1024)),
      adjust_latency(100000000),
      stats(*this), // overhead
      _system(NULL),
      ismigrating(false),
      PageSize(4096)
      {
          _dp = dispatcher();
          _rt = remappingtable();
          hctable_dram = new LRUhctable(maxhctablesize);
        //   hctable_nvm = new LRUhctable(maxhctablesize);
      }

Port& 
AccessCounter::getPort(const std::string &if_name, PortID idx) {
    if (if_name == "dp_side_port") {
        return dp_side_port;
    } 
    // else if(if_name == "mm_side_port") {
    //     return mm_side_port;
    // }
    else {
        return SimObject::getPort(if_name, idx);
    }
}
//需要修改  //by crc 240515
AddrRangeList 
AccessCounter::getAddrRanges() const{
    return mm_side_port.getAddrRanges();
}

void 
AccessCounter::sendRangeChange(){
    DPRINTF(AccessCounter, "send Range Change...\n");
    dp_side_port.sendRangeChange();
}

Tick 
AccessCounter::handleAtomic(PacketPtr pkt){
    return mm_side_port.sendAtomic(pkt);
}

void 
AccessCounter::handleFunctional(PacketPtr pkt){
    return mm_side_port.sendFunctional(pkt);
}

//通过Dp端口接受并处理dp模块发送的req请求
bool 
AccessCounter::handleDpRequest(PacketPtr pkt){
    if(dp_side_blocked){
        DPRINTF(AccessCounter, "AccessCounter module Dp req blocked directly for addr %#x\n", pkt->remapaddr);
        return false;
    }
    //py 0317
    DPRINTF(AccessCounter, "AccessCounter module access dp req for addr %#x\n", pkt->remapaddr);

    //py0113 NowPage是啥 remapaddr是啥 应该是对应的真实页面和真实地址
    uint64_t NowPage = pkt->remapaddr / PageSize;
    //1.更新hc_table,统计dram中的热页
    if(pkt->isDram){
        if(hctable_dram->haspage(NowPage)){
            hctable_dram->updatehctable(NowPage,pkt->iswrite);
        }
        else
            hctable_dram->put(NowPage,0,0);
    }
    //2.更新hctable_nvm,统计nvm中热页
    else{
        //nvm页面，更新热度表
        if(hctable_nvm->haspage(NowPage)){
            hctable_nvm->updatehctable(NowPage,pkt->iswrite);
        }
        else
            hctable_nvm->put(NowPage,0,0);
        //std::cout << "access page: " << NowPage << "page hotness: " << hctable_nvm->gethotness(NowPage) << std::endl;
        //如果到达阈值，需要迁移到dram页中，分两种情况
        if(!ismigrating && hctable_nvm->gethotness(NowPage) > migration_threshold){
            //1.页面写计数为0，权限为NVM页面，不迁移
            if(hctable_nvm->getwrite(NowPage) == 0){
                hctable_nvm->del(NowPage);  //删除对应表项
                return true;
            }
            else if(pkt->hasmigrate){ //页面为逐出dram页面重新变热，数据写回原dram页
                pkt->pkttype = Packet::PacketType::flatNVMtoDRAM_hot;
                pkt->setNvmPage(NowPage);
                pkt->setDramPage(_rt->getremapaddr(NowPage));
            }
            //2.页面权限设置为dram_flat页面，选择dram页面迁移
            else if(hctable_nvm->getwrite(NowPage) > (hctable_nvm->gethotness(NowPage) * 0.4)){
                pkt->limit = 1;
                std::pair<uint64_t,uint64_t> ReturnDramNvmPage; //替换页号
                ReturnDramNvmPage = _rt->VictDramFlat();
                if(ReturnDramNvmPage.second == 0){
                    pkt->pkttype = Packet::PacketType::flatNVMtoDRAM_first;
                    pkt->setNvmPage(NowPage);
                    pkt->setDramPage(ReturnDramNvmPage.first);
                }
                else{
                    pkt->pkttype = Packet::PacketType::flatNVMtoDRAM_second;
                    pkt->setNvmPage(NowPage);
                    pkt->setDramPage(ReturnDramNvmPage.first);
                    pkt->setOldNvmPage(ReturnDramNvmPage.second);
                }
            }
            //3.页面权限是设置为dram_cache，选择cache_dram内存池中页面迁移
            else{
                pkt->limit = 2;
                std::pair<uint64_t,std::pair<uint64_t,uint64_t>> ReturnCacheDramPage;
                ReturnCacheDramPage = _rt->VictDramCache();
               if(ReturnCacheDramPage.first != 0 && ReturnCacheDramPage.first != -1){
                    pkt->pkttype = Packet::PacketType::cacheNVMtoDRAM_free;
                    pkt->setNvmPage(NowPage);
                    pkt->setDramPage(ReturnCacheDramPage.first);
                }
                else if(ReturnCacheDramPage.first == -1){
                    pkt->pkttype = Packet::PacketType::cacheNVMtoDRAM_clean;
                    pkt->setNvmPage(NowPage);
                    pkt->setDramPage(ReturnCacheDramPage.second.first);
                    pkt->setOldNvmPage(ReturnCacheDramPage.second.second);
                } 
                else{
                    pkt->pkttype = Packet::PacketType::cacheNVMtoDRAM_dirty;
                    pkt->setNvmPage(NowPage);
                    pkt->setDramPage(ReturnCacheDramPage.second.first);
                    pkt->setOldNvmPage(NowPage);
                }
            }    
            if(!sendMigraReq(pkt)){
                DPRINTF(AccessCounter, "AccessCounter module can't send normal req for addr %#x\n", pkt->remapaddr);
                dp_side_blocked = true;
            }
        } 
    }
    return true;
}

void 
AccessCounter::ResetOneDrampage(uint64_t drampage){
    //rt调用该函数 对某个指定的dram页面清零
    dram_hctable.erase(drampage);
}

void 
AccessCounter::ResetDrampage(PacketPtr pkt){
    //rt调用该函数 对某个pkt的dram页面清零
    //hc_table[pkt->drampage] = 0;
}

//通过Mm端口接受并处理Mm模块发送的resp请求\
//py 0114 这个函数没用到
bool 
AccessCounter::handleMmResponse(PacketPtr pkt){
    if(mm_side_blocked){
        DPRINTF(AccessCounter, "AccessCounter module Mm resp blocked directly for addr %#x\n", pkt->remapaddr);
        return false;
    }
    if(!dp_side_port.sendPacket(pkt)){
        mm_side_blocked = true;
    }
    return true;
}


//判断DRAM页面是否为热页
bool AccessCounter::ishot(uint64_t page){
    return hctable_dram->haspage(page);
}

void 
AccessCounter::handleDpReqRetry(){
    assert(dp_side_blocked);
    dp_side_blocked = false;
    dp_side_port.trySendRetry();
}

void 
AccessCounter::handleMmRespRetry(){
    assert(mm_side_blocked);
    mm_side_blocked = false;
    mm_side_port.trySendRetry();
}

//py 0317
bool 
AccessCounter::sendMigraReq(PacketPtr pkt){
    DPRINTF(AccessCounter, "AccessCounter module send migration req success for addr %#x\n", pkt->remapaddr);
    // pkt->setHotAddr(uint64_t(pkt->drampage  * PageSize));
    // pkt->setColdAddr(uint64_t(pkt->hbmpage  * PageSize));
    if(!mm_side_port.sendPacket(pkt))
        return false;
    return true;
}




void AccessCounter::adjustMmThre(){
    
    uint64_t Benefit = (_rt->Cr_dram - _rt->Cr_nvm) * 50 / 1e9 + (_rt->Cw_dram - _rt->Cw_nvm) * 450 / 1e9;//迁移收益
    uint64_t Cost =(_rt->Migrafirst )* 600 / 1e9 +  (_rt->Migrasecond )* 600 / 1e9 + (_rt->Migrafree) * 150 / 1e9 + (_rt->Migraclean) * 150 / 1e9 + (_rt->Migradirty) * 600 / 1e9;//迁移开销
    uint64_t NetBenfit = Benefit - Cost;//迁移净收益
    if(Benefit < Cost){
        migration_threshold*=2;
    }
    else{
        if(NetBenfit > PreviousNetBenefit){
            if(last_action == 1){
                migration_threshold++;
                last_action = 1;
            }
            else{
                migration_threshold--;
                last_action = 0;
            }
        }
        else{
            if(last_action == 1){
                migration_threshold--;
                last_action = 0;
            }
            else{
                migration_threshold++;
                last_action = 1;
            }
        }
    }
    PreviousNetBenefit = NetBenfit;
} 

void AccessCounter::startup(){
    std::cout<< "startup "  << std::endl;
    schedule(event, adjust_latency);
}

void AccessCounter::processEvent() {
    DPRINTF(AccessCounter, "EventProcess\n");  
    /* 
        * 每隔adjust_latency查看HBM容量是否达到设定值
     */
    // if(curTick()!=0 && curTick() % adjust_latency == 0){
    //     controlHBMVolume();
    // }


    // // 每隔adjust_latency更新一次迁移阈值
    // // 阈值调整
    // if(curTick()!=0 && curTick() % adjust_latency == 0){
    //     adjustMmThre();
    //     std::cout<< "adjustMmThre done" << std::endl;
    // }

    adjustMmThre();
    //std::cout<< "migration_threshold: " << migration_threshold << std::endl;
    schedule(event, curTick() + adjust_latency);
    


}

AccessCounter::DpSidePort::DpSidePort(const std::string& _name,
                                     AccessCounter& _accesscounter)
    : ResponsePort(_name, &_accesscounter),
      accesscounter(_accesscounter),
      needRetry(false),
      blocked(false)
      {}

AddrRangeList 
AccessCounter::DpSidePort::getAddrRanges() const{
    return accesscounter.getAddrRanges();
}

bool 
AccessCounter::DpSidePort::sendPacket(PacketPtr pkt){
    panic_if(blocked, "Should never try to send if blocked!");
    return true;
}

//mm_side_block已经打开，dp_side_port可以重新发req了
void 
AccessCounter::DpSidePort::trySendRetry(){
    if (needRetry && !blocked) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(AccessCounter, "AccessCounter module sending Dp retry req for %d\n", id);
        sendRetryReq();
    }
}

Tick 
AccessCounter::DpSidePort::recvAtomic(PacketPtr pkt){
    return accesscounter.handleAtomic(pkt);
}

void 
AccessCounter::DpSidePort::recvFunctional(PacketPtr pkt){
    accesscounter.handleFunctional(pkt);
}

bool 
AccessCounter::DpSidePort::recvTimingReq(PacketPtr pkt){
    if(!accesscounter.handleDpRequest(pkt)){
        needRetry = true;
        return false;
    }
    return true;
}

void 
AccessCounter::DpSidePort::recvRespRetry(){
    assert(blocked);
    blocked = false;
    // Try to resend it. It's possible that it fails again.
    accesscounter.handleMmRespRetry();
}

AccessCounter::MmSidePort::MmSidePort(const std::string& _name,
                                     AccessCounter& _accesscounter)
    : RequestPort(_name, &_accesscounter),
      accesscounter(_accesscounter),
      needRetry(false),
      blocked(false)
      {}

bool 
AccessCounter::MmSidePort::sendPacket(PacketPtr pkt){
    panic_if(blocked, "Should never try to send if blocked!");
    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blocked = true;
        return false;
    } else {
        return true;
    }
}

void 
AccessCounter::MmSidePort::trySendRetry(){
    if (needRetry && !blocked) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(AccessCounter, "AccessCounter module sending Mm retry resp for %d\n", id);
        sendRetryResp();
    }
}

// AC模块不需要接受Mm发送的resp 也不需要向Dp转发
bool 
AccessCounter::MmSidePort::recvTimingResp(PacketPtr pkt){
    return true;
}

//因为没有成功向Mm模块发送migra req，需要重新处理
void 
AccessCounter::MmSidePort::recvReqRetry(){
    // We should have a blocked packet if this function is called.
    assert(blocked);
    blocked = false;

    accesscounter.handleDpReqRetry();
}

void 
AccessCounter::MmSidePort::recvRangeChange(){
    accesscounter.sendRangeChange();
}

// overhead
AccessCounter::MemStats::MemStats(AccessCounter &_ac)
    : Stats::Group(&_ac), ac(_ac),
    ADD_STAT(numofSecondRemap, UNIT_COUNT, "Number of second remap"),
    ADD_STAT(numofAcPackets, UNIT_COUNT, "Number of ac packets"),
    ADD_STAT(numofAfterWarmupdate, UNIT_COUNT, "Number of after warm up updates"),
    ADD_STAT(tickofWarmedUp, UNIT_COUNT, "first complete warm up tick")
{
}

void
AccessCounter::MemStats::regStats()
{
    using namespace Stats;

    Stats::Group::regStats();

    System *sys = ac.system();
    
    assert(sys);
    const auto max_requestors = sys->maxRequestors();

    numofSecondRemap
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numofSecondRemap.subname(i, sys->getRequestorName(i));
    }
    numofAcPackets
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numofAcPackets.subname(i, sys->getRequestorName(i));
    }
    numofAfterWarmupdate
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numofAfterWarmupdate.subname(i, sys->getRequestorName(i));
    }

    tickofWarmedUp
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        tickofWarmedUp.subname(i, sys->getRequestorName(i));
    }

}

} // namespace memory
} // namespace gem5
// overhead