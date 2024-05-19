/**
 * @file
 * RemappingTable.cc
 * Last modify Time:2022.3.17
 * Author:wxx
 */
#include "base/trace.hh"
#include "mem/remappingtable.hh"
#include "debug/RemappingTable.hh"

namespace gem5 {

namespace memory
{

RemappingTable::RemappingTable(const RemappingTableParams &params)
    : SimObject(params),
      bus_side_port(name() + ".bus_side_port", *this),
    //   mm_side_port(name() + ".mm_side_port", *this),
      dp_side_port(name() + ".dp_side_port",*this),
      event([this]{processEvent();}, name()), 
      adjust_latency(10000000),
    //   mm_request_port(name() + ".mm_request_port", *this),
      bus_side_blocked(false),
    //   mm_side_blocked(false),
      dp_side_blocked(false),
    //   mm_request_blocked(false),
      
    //   Cr_nvm(0),
    //   Cw_nvm(0),
    //   Cr_dram(0),
    //   Cw_dram(0),
    //   Migrafirst(0),
    //   Migrasecond(0),
    //   Migrafree(0),
    //   Migraclean(0),
    //   Migradirty(0),
      PageSize(4096),
      MaxRemapSize(262144),// 存储1G的映射
    //   CacheRemapSize(params.nvm_size/(64*1024)), //重映射表片上缓存大小
        //缓存是缓存映射表表项，还是页面，
      CacheRemapSize(params.dram_size/(4*1024)), //重映射表片上缓存大小
      CacheDramSize(params.hbm_size/(4*1024)), //用于缓存的DRAM页面，1GB
    //   MaxNVMPage(params.nvm_size/(4*1024)),
      
    // HBM-16M  DRAM-64
      MaxDRAMPage(params.dram_size/(4*1024)), 
      MaxHBMPage(params.hbm_size/(4*1024)), 

    
    //   // HBM-128M  DRAM-1G
    //   MaxDRAMPage(262144),
    //   MaxHBMPage(32768),
    //   MaxDRAMPage(4096),   
    //   MaxHBMPage(1024),
      blocked_threshold(1024),
      isblockedpkt(false),
      _system(NULL), // overhead
      stats(*this)
      { 
          //缓存中的映射表 //by crc 240516
          // remapcache = new RemapCache(CacheRemapSize/(16*8),8);
          remapcache = new RemapCache(CacheDramSize/(16*8),8);
          //remapdram = new RemapDram(MaxNVMPage+MaxDRAMPage);
          remapdram = new RemapDram(MaxDRAMPage);
        //   for(int i = 0; i < CacheDramSize; i++){
        //     FreeDramPage.push(i+MaxNVMPage+MaxDRAMPage);
        //   }
          for(int i = 0; i < CacheDramSize; i++)
          {
            DramMap.push_back(-1);
          }
        //   _ac = accesscounter();
        //   _mm = migrationmanager();
        std::cout << "rt params memory dram size: " << params.dram_size<<std::endl;
        // std::cout << "ab memory p. dram size: " << p.dram_size<<std::endl;
        std::cout << "rt params hbm_size size: " << params.hbm_size<<std::endl;
        // std::cout << "ab memory p. hbm_size size: " << p.hbm_size<<std::endl;
      }

Port& 
RemappingTable::getPort(const std::string &if_name, PortID idx) {
    if (if_name == "bus_side_port") {
        return bus_side_port;
    } /* else if (if_name == "mm_side_port") {
        return mm_side_port;
    }  */else if(if_name == "dp_side_port") {
        return dp_side_port;
    }/* else if(if_name == "mm_request_port"){
        return mm_request_port;
    } */else {
        return SimObject::getPort(if_name, idx);
    }
}

AddrRangeList
RemappingTable::getAddrRanges() const{
    return dp_side_port.getAddrRanges();
}

void
RemappingTable::sendRangeChange() {
    DPRINTF(RemappingTable, "send Range Change...\n");
    bus_side_port.sendRangeChange();
}

Tick
RemappingTable::handleAtomic(PacketPtr pkt){
    return dp_side_port.sendAtomic(pkt);
}

void
RemappingTable::handleFunctional(PacketPtr pkt){
    return dp_side_port.sendFunctional(pkt);
}

//处理bus端口发送的req请求
bool 
RemappingTable::handleBusReq(PacketPtr pkt){ //wanxx
    //如果全局阻塞，不能接受bus往下传的req
    if(bus_side_blocked){
        DPRINTF(RemappingTable, "RemappingTable module Bus req blocked directly for addr %#x\n", pkt->getAddr());
        return false;
    }
    // 统计读包总数
    if(pkt->isRead()){
        stats.sumofreadreq[pkt->req->requestorId()]++;
    }
    else{
        stats.sumofwritereq[pkt->req->requestorId()]++;
    }
    pkt->remapaddr =pkt->getAddr();
    //DPRINTF(RemappingTable, "RemappingTable module access bus req for addr %#x\n", pkt->remapaddr);
    uint64_t nowpage = (pkt->remapaddr) / PageSize;//固定页面大小为4KB
    uint64_t nowoffset = (pkt->remapaddr) % PageSize;//偏移量
    //1.查看阻塞页面的队列大小是否为2，如果不为2直接下一步，否则说明当前有迁移正在进行，需要判断当前包是否需要访问被阻塞的页面
    // if(blocked_pagenums.size() == 3 && (nowpage == blocked_pagenums[0] || nowpage == blocked_pagenums[1] || nowpage == blocked_pagenums[2])){
    //     //如果被阻塞将其加入阻塞队列
    //     blocked_pkt.push(pkt);
    //     //阻塞队列大小超过阈值则全局阻塞
    //     if(blocked_pkt.size() > blocked_threshold){
    //         bus_side_blocked = true;
    //         return false;
    //     }
    // }
    // else{
        //获取映射后地址
        if(remapcache->haspage(nowpage)){
            //在缓存中直接获取映射地址
            pkt->remapaddr = (remapcache->getremappage(nowpage)) * PageSize + nowoffset;
            stats.numofRemapHit[pkt->req->requestorId()]++;
        }
        else{
            
            //_mm->send_read_dram(pkt->remapaddr);
            //sendtodram(nowpage, pkt);//模拟访问dram映射表延迟

            pkt->remapaddr = (remapdram->getremappage(nowpage)) * PageSize + nowoffset;
            remapcache->put(nowpage,(pkt->remapaddr)/PageSize);//放入缓存中
            stats.numofRemapMiss[pkt->req->requestorId()]++;
            DPRINTF(RemappingTable, "RemappingTable module try to put the addr to remapcache %#x\n", pkt->remapaddr);
        }
        //std::cout << "access address: " << pkt->remapaddr << ", access is read: " << pkt->isRead() << std::endl;
        if(pkt->remapaddr >=0 /* MaxNVMPage * PageSize */){ //by crc 240515
            pkt->isDram = true;
        }
        if(pkt->remapaddr >= (MaxDRAMPage/*  + MaxNVMPage */) * PageSize){
            pkt->isCache = true;
        }
        //by crc 240516
        if(pkt->remapaddr != pkt->getAddr()){
            DPRINTF(RemappingTable, "RemappingTable module error in  pkt->remapaddr %#x  != pkt->getAddr() %#x\n", pkt->remapaddr, pkt->getAddr());
            // pkt->hasmigrate = true;
        }
        // if(pkt->hasmigrate){//统计访问迁移后页面的次数
        //     if(pkt->isDram){
        //         if(pkt->isRead())
        //             Cr_dram++;
        //         else
        //             Cw_dram++;
        //     }
        //     else
        //         if(pkt->isRead())
        //             Cr_nvm++;
        //         else
        //             Cw_nvm++;
        // }
        //2.往下层的dispathcher模块转发req
        if(!dp_side_port.sendPacket(pkt)){
            DPRINTF(RemappingTable, "RemappingTable module bus req can't forward for addr %#x\n", pkt->remapaddr);
            bus_side_blocked = true;
            return false;
        }
        // if(pkt->isWrite() && pkt->isCache)
        // {
        //     convert_dirty(pkt->remapaddr);
        // }
    // }
    return true;
}
//by crc 240515
//处理Mm端口发送的req请求,To be added and modified
// bool
// RemappingTable::handleMmReq(PacketPtr pkt){ //wanxx
//     if(mm_side_blocked){
//         DPRINTF(RemappingTable, "RemappingTable module Mm block req blocked directly for addr %#x\n", pkt->remapaddr);
//         return false;
//     }
//     DPRINTF(RemappingTable, "RemappingTable module access mm req for addr %#x\n", pkt->remapaddr);
    
//     if(blocked_pagenums.size() < 3){
//         //还要判断上一次阻塞造成的阻塞队列中的pkt是否全部处理完成，处理完毕后才可接受新的block请求
//         if(blocked_pkt.size()==0){
//             // blocked_pagenums.emplace_back((pkt->getHbmAddr()));
//             // blocked_pagenums.emplace_back((pkt->getDramAddr()));
//             if(pkt->pkttype == Packet::PacketType::flatNVMtoDRAM_first){
//                 blocked_pagenums.emplace_back(pkt->getNvmPage());
//                 blocked_pagenums.emplace_back(pkt->getDramPage());
//                 blocked_pagenums.emplace_back(INT64_MAX);
//             }
//             else if(pkt->pkttype == Packet::PacketType::flatNVMtoDRAM_second){
//                 blocked_pagenums.emplace_back(pkt->getNvmPage());
//                 blocked_pagenums.emplace_back(pkt->getDramPage());
//                 blocked_pagenums.emplace_back(pkt->getOldNvmPage());
//             }
//             else if(pkt->pkttype == Packet::PacketType::cacheNVMtoDRAM_free){
//                 blocked_pagenums.emplace_back(pkt->getNvmPage());
//                 blocked_pagenums.emplace_back(pkt->getDramPage());
//                 blocked_pagenums.emplace_back(INT64_MAX);
//             }
//             else if(pkt->pkttype == Packet::PacketType::cacheNVMtoDRAM_clean){
//                 blocked_pagenums.emplace_back(pkt->getNvmPage());
//                 blocked_pagenums.emplace_back(pkt->getDramPage());
//                 blocked_pagenums.emplace_back(pkt->getOldNvmPage());
//             }
//             else if(pkt->pkttype == Packet::PacketType::cacheNVMtoDRAM_dirty){
//                 blocked_pagenums.emplace_back(pkt->getNvmPage());
//                 blocked_pagenums.emplace_back(pkt->getDramPage());
//                 blocked_pagenums.emplace_back(pkt->getOldNvmPage());
//             }
//         }
//         else{
//             DPRINTF(RemappingTable, "last blockedqueue has not been completed");
//             mm_side_blocked = true;
//             return false;
//         }
//         //std::cout<<"remap_table blocked!" << std::endl;
//     }
//     else{
//         //否则如果当前发送的包与blocked_pagenums队尾的元素一样，说明是update请求，需要更新remap_table
//         // if(blocked_pagenums.back() == pkt->getDramAddr()){
//             // std::cout<<"remap_table update!" <<"from:"<<uint64_t(pkt->getHbmAddr() / PageSize) <<"  to:"<<uint64_t(pkt->getDramAddr()/PageSize)<< std::endl;
//         if(!updateRemap(pkt)){
//             DPRINTF(RemappingTable, "update remapping table false");
//             mm_side_blocked = true;
//             return false;
//         }
//         //将之前被阻塞的pkt往下发
//         handleBlockedPkt();
//         //std::cout<<"handleBlockedPkt success!" << std::endl;
//         // }
//     }
//     // }
//     return true;
// }

//处理Dp端口发送的resp响应
bool
RemappingTable::handleDpResponse(PacketPtr pkt){
    if(dp_side_blocked){
        DPRINTF(RemappingTable, "RemappingTable module Dp resp blocked directly for addr %#x\n", pkt->getAddr());
        return false;
    }
    DPRINTF(RemappingTable, "RemappingTable module access dp resp for addr %#x\n", pkt->getAddr());
    if(!bus_side_port.sendPacket(pkt)){
        dp_side_blocked = true;
        return false;
    }
    return true;
}
//by crc 240515
// bool
// RemappingTable::handleMmResponse(PacketPtr pkt){
//     if(dp_side_blocked){
//         DPRINTF(RemappingTable, "RemappingTable module Mm resp blocked directly for addr %#x\n", pkt->getAddr());
//         return false;
//     }
//     DPRINTF(RemappingTable, "RemappingTable module access Mm resp for addr %#x\n", pkt->getAddr());
//     if(!bus_side_port.sendPacket(pkt)){
//         dp_side_blocked = true;
//         return false;
//     }
//     return true;
// }


//尝试重新发送bus下发的被阻塞的请求
void
RemappingTable::handleBusReqRetry(){
    assert(bus_side_blocked);
    // 如果当前处理的不是blocked_pkt中的pkt就需要让bus重发req
    if(!isblockedpkt){
        // DPRINTF(RemappingTable, "RemappingTable module Retry Bus request for addr %#x\n", pkt->remapaddr);
        bus_side_blocked = false;
        bus_side_port.trySendRetry();
    }
    else{
        handleBlockedPkt();
    }
}

//by crc 240515
// void
// RemappingTable::handleMmReqRetry(){
//     // DPRINTF(RemappingTable, "RemappingTable module retry Mm request for addr %#x\n", pkt->remapaddr);
//     if(mm_side_blocked){
//         mm_side_blocked = false;
//         mm_side_port.trySendRetry();
//     }
// }

//尝试让dp重新发送包
void
RemappingTable::handleRespRetry(){
    assert(dp_side_blocked);
    // DPRINTF(RemappingTable, "RemappingTable module retry response for addr %#x\n", pkt->remapaddr);
    dp_side_blocked = false;
    dp_side_port.trySendRetry();
}

// void
// RemappingTable::convert_dirty(uint64_t page){
//     uint64_t nowpage = page / PageSize;
//     for (int i = 0; i < CleanDramPage.size(); i++){
//         if(CleanDramPage[i] == nowpage){
//             CleanDramPage.erase(CleanDramPage.begin() + i);
//             break;
//         }
//     }
//     return;
// }

// wxx 3.3
// wxx add 3.17 新增nvm->dram迁移
// bool
// RemappingTable::updateRemap(PacketPtr pkt){

//     // 1.1 dram_flat页面，第一次迁移
//     if(pkt->pkttype == Packet::PacketType::flatNVMtoDRAM_first){
//     // 需要加载的nvm和dram
//         uint64_t nvmpage = pkt->getNvmPage();
//         uint64_t drampage = pkt->getDramPage();
//         //修改RT
//         remapcache->update(nvmpage,drampage);
//         remapcache->update(drampage,nvmpage);
//         remapdram->update(nvmpage,drampage);
//         remapdram->update(drampage,nvmpage);
//         //测试
//         //uint64_t remapdrampage = remapcache->getremappage(nvmpage);
//         //std::cout << "old address: " << nvmpage << ", new address: " << remapdrampage << std::endl;

//         //修改热度表
//         _ac->hctable_nvm->del(nvmpage);
//         _ac->hctable_dram->del(drampage);
//         Migrafirst++;
//     }
//     // 1.2 dram_flat页面，第二次迁移
//     else if(pkt->pkttype == Packet::PacketType::flatNVMtoDRAM_second){

//         uint64_t nvmpage = pkt->getNvmPage();
//         uint64_t oldnvmpage = pkt->getOldNvmPage();
//         uint64_t drampage = pkt->getDramPage();

//         remapcache->update(oldnvmpage,oldnvmpage);
//         remapcache->update(nvmpage,drampage);
//         remapcache->update(drampage,nvmpage);
//         remapdram->update(oldnvmpage,oldnvmpage);
//         remapdram->update(nvmpage,drampage);
//         remapdram->update(drampage,nvmpage);

//         _ac->hctable_nvm->del(nvmpage);
//         _ac->hctable_nvm->del(oldnvmpage);
//         _ac->hctable_dram->del(drampage);
//         Migrasecond++;
//     }
//     // 2.1 dram_cache页面，有空闲页
//     else if(pkt->pkttype == Packet::PacketType::cacheNVMtoDRAM_free){

//         uint64_t nvmpage = pkt->getNvmPage();
//         uint64_t drampage = pkt->getDramPage();
        
//         remapcache->update(nvmpage,drampage);
//         remapdram->update(nvmpage,drampage);

//         DramMap[drampage - MaxNVMPage - MaxDRAMPage] = nvmpage;
//         CleanDramPage.emplace_back(drampage);

//         _ac->hctable_nvm->del(nvmpage);
//         _ac->hctable_dram->del(drampage);

//         Migrafree++;
//     }
//     // 2.2 dram_cache页面，有干净页
//     else if(pkt->pkttype == Packet::PacketType::cacheNVMtoDRAM_clean){

//         uint64_t drampage = pkt->getDramPage();
//         uint64_t nvmpage = pkt->getNvmPage();
//         uint64_t oldnvmpage = pkt->getOldNvmPage();
        
//         remapcache->update(nvmpage,drampage);
//         remapcache->update(oldnvmpage,oldnvmpage);
//         remapdram->update(nvmpage,drampage);
//         remapdram->update(oldnvmpage,oldnvmpage);

//         DramMap[drampage - MaxNVMPage - MaxDRAMPage] = nvmpage;
//         CleanDramPage.emplace_back(drampage);

//         _ac->hctable_nvm->del(nvmpage);
//         _ac->hctable_nvm->del(oldnvmpage);
//         _ac->hctable_dram->del(drampage);

//         Migraclean++;
//     }
//     // 2.3 cache_dram页面，没有干净页和空闲页
//     else if(pkt->pkttype == Packet:: PacketType::cacheNVMtoDRAM_dirty){
        
//         uint64_t drampage = pkt->getDramPage();
//         uint64_t nvmpage = pkt->getNvmPage();
//         uint64_t oldnvmpage = pkt->getOldNvmPage();

//         remapcache->update(nvmpage,drampage);
//         remapcache->update(oldnvmpage,oldnvmpage);
//         remapdram->update(nvmpage,drampage);
//         remapdram->update(oldnvmpage,oldnvmpage);

//         DramMap[drampage - MaxNVMPage -MaxDRAMPage] = nvmpage;
//         CleanDramPage.emplace_back(drampage);
//         _ac->hctable_nvm->del(nvmpage);
//         _ac->hctable_nvm->del(oldnvmpage);
//         _ac->hctable_dram->del(drampage);

//         Migradirty++;
//     }

//     else if(pkt->pkttype == Packet::PacketType::flatNVMtoDRAM_hot){
//     // 需要加载的nvm和dram
//         uint64_t nvmpage = pkt->getNvmPage();
//         uint64_t drampage = pkt->getDramPage();
//         //修改RT
//         remapcache->update(nvmpage,drampage);
//         remapcache->update(drampage,nvmpage);
//         _ac->hctable_nvm->del(nvmpage);
//         _ac->hctable_dram->del(drampage);

//         Migrafirst++;
//     }

//     // overhead
//     blocked_pagenums.clear();
//     stats.numofRemapUpdate[pkt->req->requestorId()]++;
//     return true;
// }

void
RemappingTable::handleBlockedPkt(){
    bus_side_blocked = true;
    isblockedpkt = true;
    while(!blocked_pkt.empty()){
        PacketPtr pkt = blocked_pkt.front();
        if(handleblockedqueue(pkt))
            blocked_pkt.pop();
        else
            return;
    }
    isblockedpkt = false;
    bus_side_blocked = false;
    //阻塞队列处理完成，打开
    //by crc 240515
    // handleMmReqRetry();
}
//remapaddr和原来的做 区分
bool 
RemappingTable::handleblockedqueue(PacketPtr pkt){
     uint64_t nowpage = (pkt->remapaddr) / PageSize;//固定页面大小为4KB
     uint64_t nowoffset = (pkt->remapaddr) % PageSize;//偏移量
    //获取映射后地址
    if(remapcache->haspage(nowpage)){
        //在缓存中直接获取映射地址
        pkt->remapaddr = remapcache->getremappage(nowpage) * PageSize + nowoffset;
        stats.numofRemapHit[pkt->req->requestorId()]++;
    }
    else{
        //如果映射不在缓存中，从DRAM中获取映射并更新缓存
        //模拟从dram中读数据
        /* unsigned dataSize = 16;
        PacketPtr dram_read_pkt;
        RequestPtr read_dram_req = std::make_shared<Request>(MaxNVMPage*PageSize, dataSize, 256, 0);
        dram_read_pkt = Packet::createRead(read_dram_req);
        dram_read_pkt->allocate();
        dram_read_pkt->isDram = true;//hjy add 3.18
        if(!dp_side_port.sendPacket(dram_read_pkt)){
            return false;
        }
        else{
            // delete dram_read_pkt;
            dram_read_pkt->deleteData();
        } */
        //获取映射地址
        pkt->remapaddr = remapdram->getremappage(nowpage) * PageSize + nowoffset;
        remapcache->put(nowpage,pkt->remapaddr / PageSize);
        stats.numofRemapMiss[pkt->req->requestorId()]++;
    }
    //std::cout << "block address: " << pkt->remapaddr << std::endl;
    if(pkt->remapaddr >=0/*  MaxNVMPage * PageSize */){
            pkt->isDram = true;
        }
    if(pkt->remapaddr >= (MaxDRAMPage /* + MaxNVMPage */) * PageSize){
            pkt->isCache = true;
        }
    if(pkt->remapaddr != pkt->getAddr()){
        DPRINTF(RemappingTable, "RemappingTable module error in  pkt->remapaddr %#x  != pkt->getAddr() %#x\n", pkt->remapaddr, pkt->getAddr());
        //pkt->hasmigrate = true;//页面迁移过
    }
    //3.往下层的dispathcher模块转发req
    if(!dp_side_port.sendPacket(pkt)){
        DPRINTF(RemappingTable, "RemappingTable module blocked_pkt can't forward for addr %#x\n", pkt->getAddr());
        // bus_side_blocked = true;
        return false;
    }
    return true;
}
//模拟访存读取数据
void
RemappingTable::sendtodram(uint64_t page, PacketPtr pkt){
    PacketPtr SendtoDram_pkt = new Packet(pkt, false, true);
    uint64_t drampage = rand() % MaxDRAMPage /* + MaxNVMPage */; 
    // SendtoDram_pkt->setNvmPage(page);
    SendtoDram_pkt->setDramPage(drampage);
    // mm_request_port.sendPacket(SendtoDram_pkt);
}

uint64_t 
RemappingTable::getremapaddr(uint64_t key){
    uint64_t remapaddr = 0;
    if(remapcache->haspage(key)){//64位 取后48位
        remapaddr = remapcache->getremappage(key) & 0x0000ffffffffffff;
    }
    else{
        remapaddr = remapdram ->getremappage(key) & 0x0000ffffffffffff;
        remapcache->put(key,remapaddr);
    }
    return remapaddr;
}

// wxx add 3.3 tlt 9.1
// std::pair<uint64_t,std::pair<uint64_t,uint64_t>>
// RemappingTable::VictDramCache(){
//     // 1.如果DRAM_Cache还有空页
//     if(FreeDramPage.size()){
//         selectfreepage = FreeDramPage.front(); 
//         selectcleanpage = 0;
//         selectnvmpage =0;
//         FreeDramPage.pop();
//     }// 2.如果DRAM_Cache没有空页
//     else{
//         if(CleanDramPage.size()){
//             //有干净页 freepage为0
//             selectfreepage = 0;
//             int start = 0;
//             while(start == 0 || _ac->ishot(selectcleanpage)){
//                 // srand((unsigned int)time(0));//初始化种子为随机值  yhliu modify 0415
//                 int random_index = rand() % CleanDramPage.size();
//                 selectcleanpage = CleanDramPage[random_index];
//                 selectnvmpage = DramMap[selectcleanpage - MaxNVMPage - MaxDRAMPage];
//                 start++;
//                 if(start == CleanDramPage.size()){
//                     break;
//                 }
//             }
//         }
//         // pkt->hotaddr = selecthotpage * PageSize;
//         // pkt->coldaddr = selectcoldpage * PageSize;
//         else{
//             //没有干净页freepage为-1
//             selectfreepage = -1;
//             int start = 0;
//             while(start == 0 || _ac->ishot(selectcleanpage)){
//                 int random_index = rand() % CacheDramSize;
//                 selectcleanpage = random_index + MaxNVMPage + MaxDRAMPage;
//                 selectnvmpage = DramMap[random_index];
//                 start++;
//                 if(start == CacheDramSize){
//                     break;
//                 }
//             }
//         }
//     }
//     // wxx add 3.17
//     std::pair<uint64_t,std::pair<uint64_t,uint64_t>> free_clear_nvm;
//     free_clear_nvm.first = selectfreepage;   //free DRAM页
//     free_clear_nvm.second.first = selectcleanpage;    //clear DRAM页
//     free_clear_nvm.second.second = selectnvmpage; //nvm页
//     return free_clear_nvm;
//     //pkt的hotaddr和coldaddr分别存放被替换出来的两个页号，下一步随着sendPacket发给MM
//     // if(pkt->needsResponse())
//     //     pkt->makeResponse();
//     // if(mm_side_port.sendPacket(pkt))
//     //     return true;
//     // else
//     //     return false;
// }

// std::pair<uint64_t,uint64_t>
// RemappingTable::VictDramFlat(){
//     //挑选不在热度表中的冷页
//     int start =0;
//     while(start == 0 || _ac->ishot(selectdrampage)){
//         int random_index = rand() % MaxDRAMPage;
//         selectdrampage = MaxNVMPage + random_index;
//         selectnvmpage = remapdram->getremappage(selectdrampage);
//         if(selectdrampage == selectnvmpage){
//             selectnvmpage = 0;
//         }
//         start ++;
//         if(start == 10000){
//             break;
//         }
//     }
//     std::pair<uint64_t,uint64_t> dram_nvm;
//     dram_nvm.first = selectdrampage;
//     dram_nvm.second = selectnvmpage;
//     return dram_nvm;
// }


RemappingTable::BusSidePort::BusSidePort(const std::string& _name,
                                     RemappingTable& _remappingtable)
    : ResponsePort(_name, &_remappingtable),
      remappingtable(_remappingtable),
      needRetry(false),
      blocked(false)
      {}

AddrRangeList 
RemappingTable::BusSidePort::getAddrRanges() const{
    return remappingtable.getAddrRanges();
}

//通过bus port向上(bus)发送resp packet
bool
RemappingTable::BusSidePort::sendPacket(PacketPtr pkt){
    panic_if(blocked, "Should never try to send if blocked!");
    // If we can't send the packet across the port, store it for later.
    if (!sendTimingResp(pkt)) {
        blocked = true;
        return false;
    } else {
        DPRINTF(RemappingTable, "RemappingTable module send to bus for addr %#x\n", pkt->getAddr());
        return true;
    }
}

//接受dp传上来的resp的重发
void 
RemappingTable::BusSidePort::recvRespRetry(){
    assert(blocked);
    DPRINTF(RemappingTable, "RemappingTable module recv dp resp retry...\n");
    blocked = false;
    remappingtable.handleRespRetry();
}

Tick 
RemappingTable::BusSidePort::recvAtomic(PacketPtr pkt){
    return remappingtable.handleAtomic(pkt);
}

void 
RemappingTable::BusSidePort::recvFunctional(PacketPtr pkt){
    return remappingtable.handleFunctional(pkt);
}

//接受上层bus发送的req请求
bool 
RemappingTable::BusSidePort::recvTimingReq(PacketPtr pkt){
   
    if(!remappingtable.handleBusReq(pkt)){
        needRetry = true;
        return false;
    }
    else
        return true;
}

//当阻塞被打开后，需要告诉上层bus让它重新开始向下转发新的pkt
void
RemappingTable::BusSidePort::trySendRetry(){
    if (needRetry && !blocked) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(RemappingTable, "RemappingTable module sending Bus retry req for %d\n", id);
        sendRetryReq();
    }
}


// RemappingTable::MmSidePort::MmSidePort(const std::string& _name,
//                                      RemappingTable& _remappingtable)
//     : ResponsePort(_name, &_remappingtable),
//       remappingtable(_remappingtable),
//       needRetry(false),
//       blocked(false)
//       {}

// AddrRangeList 
// RemappingTable::MmSidePort::getAddrRanges() const{
//     return remappingtable.getAddrRanges();
// }

// bool
// RemappingTable::MmSidePort::sendPacket(PacketPtr pkt){
//     // panic_if(blocked, "Should never try to send if blocked!");
//     if (!sendTimingResp(pkt)) {
//         blocked = true;
//         return false;
//     } else {
//         return true;
//     }
// }

//当阻塞时告诉Mm让它重新发req
// void
// RemappingTable::MmSidePort::trySendRetry(){
//     if (needRetry && !blocked) {
//         // Only send a retry if the port is now completely free
//         needRetry = false;
//         DPRINTF(RemappingTable, "RemappingTable module sending Mm retry req for %d\n", id);
//         sendRetryReq();
//     }
// }

// Tick 
// RemappingTable::MmSidePort::recvAtomic(PacketPtr pkt){
//     return remappingtable.handleAtomic(pkt);
// }

// void 
// RemappingTable::MmSidePort::recvFunctional(PacketPtr pkt){
//     return remappingtable.handleFunctional(pkt);
// }

// bool 
// RemappingTable::MmSidePort::recvTimingReq(PacketPtr pkt){
//     if(!remappingtable.handleMmReq(pkt)){
//         needRetry = true;
//         return false;
//     }
//     return true;
// }

//hjy modify 7.23
//这地方还是不对，阻塞的时候不好处理，是不是还是需要多接端口才行
// void 
// RemappingTable::MmSidePort::recvRespRetry(){
//     assert(blocked);
//     DPRINTF(RemappingTable, "RemappingTable module recv MM resp retry...\n");
//     blocked = false;
    
//     return;
// }

// RemappingTable::MmRequestPort::MmRequestPort(const std::string& _name, 
//                                      RemappingTable& _remappingtable)
//     : RequestPort(_name, &_remappingtable),
//       remappingtable(_remappingtable),
//       needRetry(false),
//       blocked(false),
//       blockedPacket(nullptr) //yhl modify
//       {}

// bool
// RemappingTable::MmRequestPort::sendPacket(PacketPtr pkt){
//     panic_if(blockedPacket != nullptr, "rt,Should never try to send if blocked!");
//     // If we can't send the packet across the port, store it for later.
//     if (!sendTimingReq(pkt)) {
//         blocked = true;
//         blockedPacket = pkt;
//         return false;
//     } else {
//         DPRINTF(RemappingTable, "RemappingTable module sending req to mm for addr %#x\n", pkt->remapaddr);
//         return true;
//     }
// }

// void 
// RemappingTable::MmRequestPort::trySendRetry(){
//     if (needRetry && !blocked) {
//         // Only send a retry if the port is now completely free
//         needRetry = false;
//         DPRINTF(RemappingTable, "RemappingTable module sending Mm retry resp for %d\n", id);
//         sendRetryResp();
//     }
// }

// // 接受从MM发送的resp
// bool 
// RemappingTable::MmRequestPort::recvTimingResp(PacketPtr pkt){
//     if (!remappingtable.handleMmResponse(pkt)) {
//         needRetry = true;
//         return false;
//     } else {
//         return true;
//     }
// }

// //该函数会告知remappingtable模块可以尝试重新往下转发req了
// void 
// RemappingTable::MmRequestPort::recvReqRetry(){
//     // We should have a blocked packet if this function is called.
//     // assert(blocked);
//     // blocked = false;

//     // remappingtable.handleBusReqRetry();

//     // We should have a blocked packet if this function is called.
//     assert(blockedPacket != nullptr);

//     // Grab the blocked packet.
//     PacketPtr pkt = blockedPacket;
//     blockedPacket = nullptr;

//     // Try to resend it. It's possible that it fails again.
//     sendPacket(pkt);
//     std::cout << "recvReqRetry" <<std::endl; 
// }

// void 
// RemappingTable::MmRequestPort::recvRangeChange(){
//     remappingtable.sendRangeChange();
// }

RemappingTable::DpSidePort::DpSidePort(const std::string& _name,
                                     RemappingTable& _remappingtable)
    : RequestPort(_name, &_remappingtable),
      remappingtable(_remappingtable),
      needRetry(false),
      blocked(false)
      {}

//尝试通过dp port 下发数据包
bool 
RemappingTable::DpSidePort::sendPacket(PacketPtr pkt){
    panic_if(blocked, "Should never try to send if blocked!");
    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blocked = true;
        return false;
    } else {
        DPRINTF(RemappingTable, "RemappingTable module sending req to dp for addr %#x\n", pkt->remapaddr);
        return true;
    }
}

void 
RemappingTable::DpSidePort::trySendRetry(){
    if (needRetry && !blocked) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(RemappingTable, "RemappingTable module sending Dp retry resp for %d\n", id);
        sendRetryResp();
    }
}

// 接受从下层dispatcher发送的resp
bool 
RemappingTable::DpSidePort::recvTimingResp(PacketPtr pkt){
    if (!remappingtable.handleDpResponse(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

//该函数会告知remappingtable模块可以尝试重新往下转发req了
void 
RemappingTable::DpSidePort::recvReqRetry(){
    // We should have a blocked packet if this function is called.
    assert(blocked);
    blocked = false;

    remappingtable.handleBusReqRetry();
}

void 
RemappingTable::DpSidePort::recvRangeChange(){
    remappingtable.sendRangeChange();
}

// overhead
RemappingTable::MemStats::MemStats(RemappingTable &_remap)
    : statistics::Group(&_remap), remap(_remap),
    ADD_STAT(numofRemapHit, statistics::units::Count::get(), "Number of remap_table hit"),
    ADD_STAT(numofRemapMiss, statistics::units::Count::get(), "Number of remap_table miss"),
    ADD_STAT(numofRemapUpdate, statistics::units::Count::get(), "Number of remap_table update"),
    ADD_STAT(sumofreadreq, statistics::units::Count::get(), "Sum of bus read packets"),
    ADD_STAT(sumofwritereq, statistics::units::Count::get(), "Sum of bus write packets")
{
}

void
RemappingTable::MemStats::regStats()
{
    using namespace statistics;

    statistics::Group::regStats();

    System *sys = remap.system();
    assert(sys);
    const auto max_requestors = sys->maxRequestors();

    numofRemapHit
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numofRemapHit.subname(i, sys->getRequestorName(i));
    }

    numofRemapMiss
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numofRemapMiss.subname(i, sys->getRequestorName(i));
    }

    numofRemapUpdate
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numofRemapUpdate.subname(i, sys->getRequestorName(i));
    }

    sumofreadreq
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        sumofreadreq.subname(i, sys->getRequestorName(i));
    }

    sumofwritereq
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        sumofwritereq.subname(i, sys->getRequestorName(i));
    }
}

void RemappingTable::startup(){
    if (use_evict){
        schedule(event, adjust_latency);
    }
}

void RemappingTable::processEvent() {
    DPRINTF(RemappingTable, "EventProcess\n");
    schedule(event, curTick() + adjust_latency);
   
}

} // namespace memory
} // namespace gem5