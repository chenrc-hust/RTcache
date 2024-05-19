/**
 * @file
 * migrationmanager.cc
 * Last modify Time:2022.3.18
 * Author:hjy
 */
#include "base/trace.hh"
#include "mem/migrationmanager.hh"
#include "debug/MigrationManager.hh"

// constructor of the mm
MigrationManager::MigrationManager(const MigrationManagerParams &params)
    : SimObject(params),
      ac_side_port(name() + ".ac_side_port", *this),
      rt_response_port(name() + ".rt_response_port", *this),
      rt_request_port(name() + ".rt_request_port", *this),
      dp_side_port(name() + ".dp_side_port", *this),
      max_queue_len(1000000),
      PageSize(4096),
      MMnumofcycles(0),
      blockpacket1(false),
      blockpacket2(false),
      blockpacket3(false),
      blockpacket4(false),
      blockpacket5(false),
      blockpacket6(false),
      MMblock(false),
      stats(*this), // overhead
      _system(NULL) {
           _ac = accesscounter();
      }

Port& 
MigrationManager::getPort(const std::string &if_name, PortID idx) {
    if (if_name == "ac_side_port") {
        return ac_side_port;
    } else if (if_name == "rt_response_port") {
        return rt_response_port;
    } else if (if_name == "rt_request_port") {
        return rt_request_port;
    } else if (if_name == "dp_side_port") {
        return dp_side_port;
    } else {
        return SimObject::getPort(if_name, idx);
    }
}

// handle the request from the AC, judge if we can save the pkt
bool MigrationManager::handleACRequest(PacketPtr pkt) {
    // 如果当前迁移队列已满
    if (swap_pkts_qe.size() == max_queue_len) {     // the queue if full
        DPRINTF(MigrationManager, "the swap queue if full, cannot handle the AC request\n");
        return false;
    } 

    //迁移页面为逐出的dram页面重新变热
    if(pkt->pkttype == Packet::PacketType::flatNVMtoDRAM_hot){
        std::cout << "migration from ac, and limit is flat dram(hot),size: " << swap_pkts_qe.size() << ", pkt addr: " << pkt->nvmpage << ",victim pagess: " << pkt->drampage << std::endl;
        swap_pkts_qe.emplace(pkt);
        stats.Migraflathot[pkt->req->requestorId()]++;
    }

    //1.权限为dram_flat页面，选中的页面未迁移过
    if(pkt->pkttype == Packet::PacketType::flatNVMtoDRAM_first){
        std::cout << "migration from ac, and limit is flat dram(first),size: " << swap_pkts_qe.size() << ", pkt addr: " << pkt->nvmpage << ",victim pagess: " << pkt->drampage << std::endl;
        swap_pkts_qe.emplace(pkt);
        stats.Migraflatfir[pkt->req->requestorId()]++;
    }

    //2.权限为dram_flat页面，选中的页面已经迁移过
    else if(pkt->pkttype == Packet::PacketType::flatNVMtoDRAM_second){
        std::cout << "migration from ac, and limit is flat dram(second),size: " << swap_pkts_qe.size() << ", pkt addr: " << pkt->nvmpage << ",victim pagess: " << pkt->drampage << std::endl;
        swap_pkts_qe.emplace(pkt);
        stats.Migraflatsec[pkt->req->requestorId()]++;
    }

    //3.权限为dram_cache页面，有空闲页
    else if(pkt->pkttype == Packet::PacketType::cacheNVMtoDRAM_free){
        std::cout << "migration from ac, and limit is cache dram(free),size: " << swap_pkts_qe.size() << ", pkt addr: " << pkt->nvmpage << ",victim pagess: " << pkt->drampage << std::endl;
        swap_pkts_qe.emplace(pkt);
        stats.Migracachefree[pkt->req->requestorId()]++;
    }

    //4.权限为dram_cache页面，有干净页
    else if(pkt->pkttype == Packet::PacketType::cacheNVMtoDRAM_clean){
        std::cout << "migration from ac, and limit is cache dram(clean),size: " << swap_pkts_qe.size() << ", pkt addr: " << pkt->nvmpage << ",victim pagess: " << pkt->drampage << std::endl;
        swap_pkts_qe.emplace(pkt);
        stats.Migracachecl[pkt->req->requestorId()]++;
    }

    //5.权限为dram_cache页面，脏页
    else if(pkt->pkttype == Packet::PacketType::cacheNVMtoDRAM_dirty){
        std::cout << "migration from ac, and limit is cache dram(dirty),size: " << swap_pkts_qe.size() << ", pkt addr: " << pkt->nvmpage << ",victim pagess: " << pkt->drampage << std::endl;
        swap_pkts_qe.emplace(pkt);
        stats.Migracachedir[pkt->req->requestorId()]++;
    }

    stats.MigraFromAc[pkt->req->requestorId()]++;
    swapPage();
    return true;
}
bool MigrationManager::handleRTRequest(PacketPtr pkt) {
    uint64_t drampage = pkt->drampage;
    unsigned dataSize = 4;
    if(!blockpacket1){
        PacketPtr dram_read_pkt;
        RequestPtr read_dram_req = std::make_shared<Request>(drampage*PageSize, dataSize, 256, 0);
        dram_read_pkt = Packet::createRead(read_dram_req);
        dram_read_pkt->allocate();
        dram_read_pkt->isDram = true;
        if(!dp_side_port.sendPacket(dram_read_pkt)){
            return false;
        }
        else{
            dram_read_pkt->deleteData();
            return true;
        }
    }
}

// handle the reponse from the RT
bool 
MigrationManager::handleRTResponse(PacketPtr pkt) {
    return true;        // directly return true
}

// Disp的单独处理逻辑
bool 
MigrationManager::handleDispResponse(PacketPtr pkt) {
    // lb modify 10.14
    if(pkt != nullptr){
        delete pkt;
    }
    return true;
}

void MigrationManager::swapPage() {
    DPRINTF(MigrationManager, "in mm page swap module\n");
    while (!swap_pkts_qe.empty()) {
        PacketPtr pkt = swap_pkts_qe.front();
        if(pkt->pkttype == Packet::PacketType::flatNVMtoDRAM_first){
            if(!flatnvmtodram_first(pkt))
                break;
        }
        else if(pkt->pkttype == Packet::PacketType::flatNVMtoDRAM_second){
            if(!flatnvmtodram_second(pkt))
                break;
        }
        else if(pkt->pkttype == Packet::PacketType::cacheNVMtoDRAM_free){
            if(!cachenvmtodram_free(pkt))
                break;
        }
        else if(pkt->pkttype == Packet::PacketType::cacheNVMtoDRAM_clean){
            if(!cachenvmtodram_clean(pkt))
                break;
        }
        else if(pkt->pkttype == Packet::PacketType::cacheNVMtoDRAM_dirty){
            if(!cachenvmtodram_dirty(pkt))
                break;
        }
        else if(pkt->pkttype == Packet::PacketType::flatNVMtoDRAM_hot){
            if(!flatnvmtodram_hot(pkt))
                break;
        }
        swap_pkts_qe.pop();
    }
}

//迁移页面为逐出页面重新变热
bool
MigrationManager::flatnvmtodram_hot(PacketPtr pkt){
    uint64_t nvmpage = pkt->nvmpage;
    uint64_t drampage = pkt->drampage;
    unsigned dataSize = 4;
    RequestPtr update_req = std::make_shared<Request>(pkt->getAddr(), dataSize, 256, 0);
    PacketPtr update_pkt = Packet::createRead(update_req);
    update_pkt->setDramPage(drampage);
    update_pkt->setNvmPage(nvmpage);
    update_pkt->pkttype = Packet::PacketType::flatNVMtoDRAM_first;
    if(!MMblock)
        rt_request_port.sendPacket(update_pkt);
    while (MMnumofcycles < PageSize/4)//把,nvm 理解成另一个页面即可
    {
        DPRINTF(MigrationManager, "MMnumofcycles = %d\n",MMnumofcycles);//hjy add 3.18
        //1.先把dram页中的数据读出来
        if(!blockpacket1){
            PacketPtr dram_read_pkt;
            RequestPtr read_dram_req = std::make_shared<Request>(drampage*PageSize, dataSize, 256, 0);
            dram_read_pkt = Packet::createRead(read_dram_req);
            dram_read_pkt->allocate();
            dram_read_pkt->isDram = true;
            if(!dp_side_port.sendPacket(dram_read_pkt)){
                MMblock = true;
                break;
            }
            else{
                dram_read_pkt->deleteData();
                // delete dram_read_pkt;
                blockpacket1 = true;
            }
        }
        //2.读nvm中的数据
        if(!blockpacket3){
            PacketPtr nvm_read_pkt;
            RequestPtr read_nvm_req = std::make_shared<Request>(drampage*PageSize, dataSize, 256, 0);
            nvm_read_pkt = Packet::createRead(read_nvm_req);
            nvm_read_pkt->allocate();
            //nvm_read_pkt->hascache = true;//hjy add 3.18
            if(!dp_side_port.sendPacket(nvm_read_pkt)){
                MMblock = true;
                break;
            }
            else{
                nvm_read_pkt->deleteData();
                // delete dram_read_pkt;
                blockpacket3 = true;
            }
        }
        //3.dram数据写到nvm上
        if(!blockpacket4){
            dram_buffer = new uint8_t[4];
            RequestPtr write_nvm_req = std::make_shared<Request>(nvmpage*PageSize, dataSize, 2, 0);
            PacketPtr nvm_write_pkt = Packet::createWrite(write_nvm_req);
            nvm_write_pkt->dataStatic<uint8_t>(dram_buffer);
            if(!dp_side_port.sendPacket(nvm_write_pkt)){
                MMblock = true;
                break;
            }
            else{
                nvm_write_pkt->deleteData();
                blockpacket4 = true;
                // delete hbm_write_pkt;
                delete dram_buffer;
            }
        }
        //4.nvm数据写到dram上
        if(!blockpacket2){
            nvm_buffer = new uint8_t[4];
            RequestPtr write_dram_req = std::make_shared<Request>(drampage*PageSize, dataSize, 2, 0);
            PacketPtr dram_write_pkt = Packet::createWrite(write_dram_req);
            dram_write_pkt->dataStatic<uint8_t>(nvm_buffer);
            dram_write_pkt->isDram = true;//hjy add 3.18
            if(!dp_side_port.sendPacket(dram_write_pkt)){
                MMblock = true;
                break;
            }
            else{
                blockpacket1 = false;
                blockpacket3 = false;
                blockpacket4 = false;
                dram_write_pkt->deleteData();
                // delete hbm_write_pkt;
                delete nvm_buffer;
            }
        }
        MMnumofcycles++;
    }
    if(MMnumofcycles == PageSize/4){
        rt_request_port.sendPacket(update_pkt);
        MMblock = false;
        MMnumofcycles = 0;
        // overhead 统计页迁移的次数。
        stats.numofMigra[pkt->req->requestorId()]++;
        delete update_pkt;
        return true;
    }
    else{
        delete update_pkt;
        return false;
    }

}

//权限为flat_dram，所选目标dram地址为第一次迁移
bool
MigrationManager::flatnvmtodram_first(PacketPtr pkt){
    uint64_t nvmpage = pkt->nvmpage;
    uint64_t drampage = pkt->drampage;
    unsigned dataSize = 4;
    RequestPtr update_req = std::make_shared<Request>(pkt->getAddr(), dataSize, 256, 0);
    PacketPtr update_pkt = Packet::createRead(update_req);
    update_pkt->setDramPage(drampage);
    update_pkt->setNvmPage(nvmpage);
    update_pkt->pkttype = Packet::PacketType::flatNVMtoDRAM_first;
    if(!MMblock)
        rt_request_port.sendPacket(update_pkt); 
    while (MMnumofcycles < PageSize/4)
    {
        DPRINTF(MigrationManager, "MMnumofcycles = %d\n",MMnumofcycles);//hjy add 3.18
        //1.先把dram页中的数据读出来
        if(!blockpacket1){
            PacketPtr dram_read_pkt;
            RequestPtr read_dram_req = std::make_shared<Request>(drampage*PageSize, dataSize, 256, 0);
            dram_read_pkt = Packet::createRead(read_dram_req);
            dram_read_pkt->allocate();
            dram_read_pkt->isDram = true;
            if(!dp_side_port.sendPacket(dram_read_pkt)){
                MMblock = true;
                break;
            }
            else{
                dram_read_pkt->deleteData();
                // delete dram_read_pkt;
                blockpacket1 = true;
            }
        }
        //2.读nvm中的数据
        if(!blockpacket3){
            PacketPtr nvm_read_pkt;
            RequestPtr read_nvm_req = std::make_shared<Request>(nvmpage*PageSize, dataSize, 256, 0);
            nvm_read_pkt = Packet::createRead(read_nvm_req);
            nvm_read_pkt->allocate();
            //nvm_read_pkt->hascache = true;//hjy add 3.18
            if(!dp_side_port.sendPacket(nvm_read_pkt)){
                MMblock = true;
                break;
            }
            else{
                nvm_read_pkt->deleteData();
                // delete dram_read_pkt;
                blockpacket3 = true;
            }
        }
        //3.dram数据写到nvm上
        if(!blockpacket4){
            dram_buffer = new uint8_t[4];
            RequestPtr write_nvm_req = std::make_shared<Request>(nvmpage*PageSize, dataSize, 2, 0);
            PacketPtr nvm_write_pkt = Packet::createWrite(write_nvm_req);
            nvm_write_pkt->dataStatic<uint8_t>(dram_buffer);
            if(!dp_side_port.sendPacket(nvm_write_pkt)){
                MMblock = true;
                break;
            }
            else{
                nvm_write_pkt->deleteData();
                blockpacket4 = true;
                // delete hbm_write_pkt;
                delete dram_buffer;
            }
        }
        //4.nvm数据写到dram上
        if(!blockpacket2){
            nvm_buffer = new uint8_t[4];
            RequestPtr write_dram_req = std::make_shared<Request>(drampage*PageSize, dataSize, 2, 0);
            PacketPtr dram_write_pkt = Packet::createWrite(write_dram_req);
            dram_write_pkt->dataStatic<uint8_t>(nvm_buffer);
            dram_write_pkt->isDram = true;//hjy add 3.18
            if(!dp_side_port.sendPacket(dram_write_pkt)){
                MMblock = true;
                break;
            }
            else{
                blockpacket1 = false;
                blockpacket3 = false;
                blockpacket4 = false;
                dram_write_pkt->deleteData();
                // delete hbm_write_pkt;
                delete nvm_buffer;
            }
        }
        MMnumofcycles++;
    } 
    if(MMnumofcycles == PageSize/4){
        rt_request_port.sendPacket(update_pkt);
        MMblock = false;
        MMnumofcycles = 0;
        // overhead 统计页迁移的次数。
        stats.numofMigra[pkt->req->requestorId()]++;
        delete update_pkt;
        return true;
    }
    else{
        delete update_pkt;
        return false;
    }

}

//权限为flat_dram，所选目标dram地址为第二次迁移
bool
MigrationManager::flatnvmtodram_second(PacketPtr pkt){
    uint64_t nvmpage = pkt->nvmpage;
    uint64_t drampage = pkt->drampage;
    uint64_t oldnvmpage = pkt->oldnvmpage;
    unsigned dataSize = 4;
    RequestPtr update_req = std::make_shared<Request>(pkt->getAddr(), dataSize, 256, 0);
    PacketPtr update_pkt = Packet::createRead(update_req);
    update_pkt->setDramPage(drampage);
    update_pkt->setNvmPage(nvmpage);
    update_pkt->setOldNvmPage(oldnvmpage);
    update_pkt->pkttype = Packet::PacketType::flatNVMtoDRAM_second;
    if(!MMblock)
        rt_request_port.sendPacket(update_pkt);
    while (MMnumofcycles < PageSize/4)
    {
        DPRINTF(MigrationManager, "MMnumofcycles = %d\n",MMnumofcycles);//hjy add 3.18
        //第一次迁移，把dram中的数据放入原来的nvm中，同时读出nvm中放的数据
        //1.先把dram页中的数据读出来
        if(!blockpacket1){
            PacketPtr dram_read_pkt;
            RequestPtr read_dram_req = std::make_shared<Request>(drampage*PageSize, dataSize, 256, 0);
            dram_read_pkt = Packet::createRead(read_dram_req);
            dram_read_pkt->allocate();
            dram_read_pkt->isDram = true;//hjy add 3.18
            if(!dp_side_port.sendPacket(dram_read_pkt)){
                MMblock = true;
                break;
            }
            else{
                dram_read_pkt->deleteData();
                // delete dram_read_pkt;
                blockpacket1 = true;
            }
        }
        //2.第一次读nvm中的数据，地址为oldnvmpage
        if(!blockpacket3){
            PacketPtr nvm_read_pkt;
            RequestPtr read_nvm_req = std::make_shared<Request>(oldnvmpage*PageSize, dataSize, 256, 0);
            nvm_read_pkt = Packet::createRead(read_nvm_req);
            nvm_read_pkt->allocate();
            if(!dp_side_port.sendPacket(nvm_read_pkt)){
                MMblock = true;
                break;
            }
            else{
                nvm_read_pkt->deleteData();
                // delete dram_read_pkt;
                blockpacket3 = true;
            }
        }
        //3.dram数据写到nvm上
        if(!blockpacket4){
            dram_buffer = new uint8_t[4];
            RequestPtr write_nvm_req = std::make_shared<Request>(oldnvmpage*PageSize, dataSize, 2, 0);
            PacketPtr nvm_write_pkt = Packet::createWrite(write_nvm_req);
            nvm_write_pkt->dataStatic<uint8_t>(dram_buffer);
            //nvm_write_pkt->hascache = true;//hjy add 3.18
            //nvm_write_pkt->hasremap = true;//hjy add 3.18
            if(!dp_side_port.sendPacket(nvm_write_pkt)){
                MMblock = true;
                break;
            }
            else{
                blockpacket3 = false;
                nvm_write_pkt->deleteData();
                // delete hbm_write_pkt;
                delete dram_buffer;
            }
        }
        //第二次迁移开始
        //4.第二次读nvm中数据，地址为新nvm页
        if(!blockpacket3){
            PacketPtr nvm_read_pkt;
            RequestPtr read_nvm_req = std::make_shared<Request>(nvmpage*PageSize, dataSize, 256, 0);
            nvm_read_pkt = Packet::createRead(read_nvm_req);
            nvm_read_pkt->allocate();
            //nvm_read_pkt->hascache = true;//hjy add 3.18
            if(!dp_side_port.sendPacket(nvm_read_pkt)){
                MMblock = true;
                break;
            }
            else{
                nvm_read_pkt->deleteData();
                // delete dram_read_pkt;
                blockpacket3 = true;
            }
        }
        //5.第一次读的nvm数据放入新的nvm页中
         if(!blockpacket4){
            dram_buffer = new uint8_t[4];
            RequestPtr write_nvm_req = std::make_shared<Request>(nvmpage*PageSize, dataSize, 2, 0);
            PacketPtr nvm_write_pkt = Packet::createWrite(write_nvm_req);
            nvm_write_pkt->dataStatic<uint8_t>(dram_buffer);
            if(!dp_side_port.sendPacket(nvm_write_pkt)){
                MMblock = true;
                break;
            }
            else{
                nvm_write_pkt->deleteData();
                blockpacket4 = true;
                // delete hbm_write_pkt;
                delete dram_buffer;
            }
        }
        //6.第二次读出的nvm数据写到dram上
        if(!blockpacket2){
            nvm_buffer = new uint8_t[4];
            RequestPtr write_dram_req = std::make_shared<Request>(drampage*PageSize, dataSize, 2, 0);
            PacketPtr dram_write_pkt = Packet::createWrite(write_dram_req);
            dram_write_pkt->dataStatic<uint8_t>(nvm_buffer);
            dram_write_pkt->isDram = true;
            //nvm_write_pkt->hascache = true;//hjy add 3.18
            //nvm_write_pkt->hasremap = true;//hjy add 3.18
            if(!dp_side_port.sendPacket(dram_write_pkt)){
                MMblock = true;
                break;
            }
            else{
                blockpacket1 = false;
                blockpacket2 = false;
                blockpacket3 = false;
                blockpacket4 = false;
                dram_write_pkt->deleteData();
                // delete hbm_write_pkt;
                delete nvm_buffer;
            }
        }
        MMnumofcycles++;
    }
    if(MMnumofcycles == PageSize/4){
        rt_request_port.sendPacket(update_pkt);
        MMblock = false;
        MMnumofcycles = 0;
        // overhead 统计页迁移的次数。
        stats.numofMigra[pkt->req->requestorId()]++;
        delete update_pkt;
        return true;
    }
    else{
        delete update_pkt;
        return false;
    }

}

//cahe_dram有空闲页面
bool
MigrationManager::cachenvmtodram_free(PacketPtr pkt){
    uint64_t drampage = pkt->drampage;
    uint64_t nvmpage = pkt->nvmpage;
    unsigned dataSize = 4; 
    RequestPtr update_req = std::make_shared<Request>(pkt->getAddr(), dataSize, 256, 0);
    PacketPtr update_pkt = Packet::createRead(update_req);
    update_pkt->setDramPage(drampage);
    update_pkt->setNvmPage(nvmpage);
    update_pkt->pkttype = Packet::PacketType::cacheNVMtoDRAM_free;
    if(!MMblock)
        rt_request_port.sendPacket(update_pkt);
    while(MMnumofcycles < PageSize/4){
        DPRINTF(MigrationManager, "MMnumofcycles = %d\n",MMnumofcycles);//hjy add 3.18
        //1.先把nvm页中的数据读出来
        if(!blockpacket3){
            PacketPtr nvm_read_pkt;
            RequestPtr read_nvm_req = std::make_shared<Request>(nvmpage*PageSize, dataSize, 256, 0);
            nvm_read_pkt = Packet::createRead(read_nvm_req);
            nvm_read_pkt->allocate();
            //nvm_read_pkt->hascache = true;//hjy add 3.18
            if(!dp_side_port.sendPacket(nvm_read_pkt)){
                MMblock = true;
                break;
            }
            else{
                nvm_read_pkt->deleteData();
                // delete dram_read_pkt;
                blockpacket3 = true;
            }
        }
        //2.再写到dram页中
        if(!blockpacket6){
            nvm_buffer = new uint8_t[4];
            RequestPtr write_dram_req = std::make_shared<Request>(drampage*PageSize, dataSize, 2, 0);
            PacketPtr dram_write_pkt = Packet::createWrite(write_dram_req);
            dram_write_pkt->dataStatic<uint8_t>(nvm_buffer);
            dram_write_pkt->isDram = true;
            dram_write_pkt->isCache =true;
            //nvm_write_pkt->hascache = true;//hjy add 3.18
            //nvm_write_pkt->hasremap = true;//hjy add 3.18
            if(!dp_side_port.sendPacket(dram_write_pkt)){
                MMblock = true;
                break;
            }
            else{
                blockpacket3 = false;
                blockpacket6 = false;
                dram_write_pkt->deleteData();
                // delete hbm_write_pkt;
                delete nvm_buffer;
            }
        }
        MMnumofcycles++;
    }
    if(MMnumofcycles == PageSize/4){
        rt_request_port.sendPacket(update_pkt);
        MMblock = false;
        MMnumofcycles = 0;
        // overhead 统计页迁移的次数。
        stats.numofMigra[pkt->req->requestorId()]++;
        delete update_pkt;
        return true;
    }
    else{
        delete update_pkt;
        return false;
    }
}

//cahe_dram有干净页
bool
MigrationManager::cachenvmtodram_clean(PacketPtr pkt){
    uint64_t drampage = pkt->drampage;
    uint64_t nvmpage = pkt->nvmpage;
    uint64_t oldnvmpage = pkt->oldnvmpage;
    unsigned dataSize = 4; 
    RequestPtr update_req = std::make_shared<Request>(pkt->getAddr(), dataSize, 256, 0);
    PacketPtr update_pkt = Packet::createRead(update_req);
    update_pkt->setDramPage(drampage);
    update_pkt->setNvmPage(nvmpage);
    update_pkt->setOldNvmPage(oldnvmpage);
    update_pkt->pkttype = Packet::PacketType::cacheNVMtoDRAM_clean;
    if(!MMblock)
        rt_request_port.sendPacket(update_pkt);
    while(MMnumofcycles < PageSize/4){
        DPRINTF(MigrationManager, "MMnumofcycles = %d\n",MMnumofcycles);//hjy add 3.18
        //1.先把nvm页中的数据读出来
        if(!blockpacket3){
            PacketPtr nvm_read_pkt;
            RequestPtr read_nvm_req = std::make_shared<Request>(nvmpage*PageSize, dataSize, 256, 0);
            nvm_read_pkt = Packet::createRead(read_nvm_req);
            nvm_read_pkt->allocate();
            //nvm_read_pkt->hascache = true;//hjy add 3.18
            if(!dp_side_port.sendPacket(nvm_read_pkt)){
                MMblock = true;
                break;
            }
            else{
                nvm_read_pkt->deleteData();
                // delete dram_read_pkt;
                blockpacket3 = true;
            }
        }
        //2.再写到cache_dram页中
        if(!blockpacket6){
            nvm_buffer = new uint8_t[4];
            RequestPtr write_dram_req = std::make_shared<Request>(drampage*PageSize, dataSize, 2, 0);
            PacketPtr dram_write_pkt = Packet::createWrite(write_dram_req);
            dram_write_pkt->dataStatic<uint8_t>(nvm_buffer);
            dram_write_pkt->isDram = true;
            dram_write_pkt->isCache =true;
            if(!dp_side_port.sendPacket(dram_write_pkt)){
                MMblock = true;
                break;
            }
            else{
                blockpacket3 = false;
                blockpacket6 = false;
                dram_write_pkt->deleteData();
                // delete hbm_write_pkt;
                delete nvm_buffer;
            }
        }
        MMnumofcycles++;
    }
    if(MMnumofcycles == PageSize/4){
        rt_request_port.sendPacket(update_pkt);
        MMblock = false;
        MMnumofcycles = 0;
        // overhead 统计页迁移的次数。
        stats.numofMigra[pkt->req->requestorId()]++;
        delete update_pkt;
        return true;
    }
    else{
        delete update_pkt;
        return false;
    }
}

//cahe_dram没有空闲页和干净页
bool
MigrationManager::cachenvmtodram_dirty(PacketPtr pkt){
    uint64_t drampage = pkt->drampage;
    uint64_t nvmpage = pkt->nvmpage;
    uint64_t oldnvmpage = pkt->oldnvmpage;
    unsigned dataSize = 4; 
    RequestPtr update_req = std::make_shared<Request>(pkt->getAddr(), dataSize, 256, 0);
    PacketPtr update_pkt = Packet::createRead(update_req);
    update_pkt->setDramPage(drampage);
    update_pkt->setNvmPage(nvmpage);
    update_pkt->setOldNvmPage(oldnvmpage);
    update_pkt->pkttype = Packet::PacketType::cacheNVMtoDRAM_dirty;
    if(!MMblock)
        rt_request_port.sendPacket(update_pkt);
    
    //第一次迁移，将dram页中数据逐出到oldnvmpage中
    while(MMnumofcycles < PageSize/4){
        DPRINTF(MigrationManager, "MMnumofcycles = %d\n",MMnumofcycles);//hjy add 3.18
        //1.先把cache_dram页中的数据读出来
        if(!blockpacket5){
            PacketPtr dram_read_pkt;
            RequestPtr read_dram_req = std::make_shared<Request>(drampage*PageSize, dataSize, 256, 0);
            dram_read_pkt = Packet::createRead(read_dram_req);
            dram_read_pkt->allocate();
            dram_read_pkt->isDram = true;
            dram_read_pkt->isCache =true;
            if(!dp_side_port.sendPacket(dram_read_pkt)){
                MMblock = true;
                break;
            }
            else{
                dram_read_pkt->deleteData();
                // delete dram_read_pkt;
                blockpacket5 = true;
            }
        }
        //2.再写到oldnvmpage中
        if(!blockpacket4){
            dram_buffer = new uint8_t[4];
            RequestPtr write_nvm_req = std::make_shared<Request>(nvmpage*PageSize, dataSize, 2, 0);
            PacketPtr nvm_write_pkt = Packet::createWrite(write_nvm_req);
            nvm_write_pkt->dataStatic<uint8_t>(dram_buffer);
            if(!dp_side_port.sendPacket(nvm_write_pkt)){
                MMblock = true;
                break;
            }
            else{
                nvm_write_pkt->deleteData();
                blockpacket4 = true;
                // delete hbm_write_pkt;
                delete dram_buffer;
            }
        }
        //3.读出nvmpage中的数据
        if(!blockpacket3){
            PacketPtr nvm_read_pkt;
            RequestPtr read_nvm_req = std::make_shared<Request>(nvmpage*PageSize, dataSize, 256, 0);
            nvm_read_pkt = Packet::createRead(read_nvm_req);
            nvm_read_pkt->allocate();
            if(!dp_side_port.sendPacket(nvm_read_pkt)){
                MMblock = true;
                break;
            }
            else{
                nvm_read_pkt->deleteData();
                blockpacket3 = true;
            }
        }
        //4.将数据写到drampage中
        if(!blockpacket6){
            nvm_buffer = new uint8_t[4];
            RequestPtr write_dram_req = std::make_shared<Request>(drampage*PageSize, dataSize, 2, 0);
            PacketPtr dram_write_pkt = Packet::createWrite(write_dram_req);
            dram_write_pkt->dataStatic<uint8_t>(nvm_buffer);
            dram_write_pkt->isDram = true;
            dram_write_pkt->isCache =true;
            if(!dp_side_port.sendPacket(dram_write_pkt)){
                MMblock = true;
                break;
            }
            else{
                blockpacket3 = false;
                blockpacket4 = false;
                blockpacket5 = false;
                blockpacket6 = false;
                dram_write_pkt->deleteData();
                // delete hbm_write_pkt;
                delete nvm_buffer;
            }
        }
        MMnumofcycles++;
    }
    if(MMnumofcycles == PageSize/4){
        rt_request_port.sendPacket(update_pkt);
        MMblock = false;
        MMnumofcycles = 0;
        // overhead 统计页迁移的次数。
        stats.numofMigra[pkt->req->requestorId()]++;
        delete update_pkt;
        return true;
    }
    else{
        delete update_pkt;
        return false;
    }
}

// --------------------------- the deal function of every port------------------------------

// constructor
MigrationManager::AcSidePort::AcSidePort(const std::string& _name,
                                     MigrationManager& _migrationmanager)
    : ResponsePort(_name, &_migrationmanager),
      migrationmanager(_migrationmanager),
      blocked(false),
      needRetry(false) 
      {}

// Just forward to the mm.
AddrRangeList
MigrationManager::AcSidePort::getAddrRanges() const
{
    return migrationmanager.getAddrRanges();
}

void
MigrationManager::AcSidePort::recvFunctional(PacketPtr pkt)
{
    return migrationmanager.handleFunctional(pkt);
}

// -----------key functions----------
// 1.1 first，this function receives the request from AC。（√）
bool
MigrationManager::AcSidePort::recvTimingReq(PacketPtr pkt)
{
    // / judge if MM can hadle this request
    if (!migrationmanager.handleACRequest(pkt)) {     // if cannot, return false
        needRetry = true;       // and we set the needRetry flag, which means we should send retry request after
        return false;
    } // else, handle successfully
    needRetry = false;      // reset the flag
    return true;
}

// if we return false in recvTimingReq func, we need to ask AC to send the request again when we can handle the request now(when the queue
// is not full. More specifically, when we swap the page successfully).
void
MigrationManager::AcSidePort::trySendRetry()    // We will actively call this function
{
    assert(needRetry);      // only if when the needRetry if true;
    DPRINTF(MigrationManager, "Sending retry req  to AC\n");
    needRetry = false;
    sendRetryReq();         // sendReqRetry，当slave忙完以后，就给master发这个。master会重发sendTimingReq
}

// this function will not used, becasue we donnot need to send pkt to AC
void
MigrationManager::AcSidePort::sendPacket(PacketPtr pkt)  // （√）
{

}

// when master is free, this func will receive the request
void
MigrationManager::AcSidePort::recvRespRetry() 
{

}

Tick
MigrationManager::AcSidePort::recvAtomic(PacketPtr pkt) {
    return migrationmanager.handleAtomic(pkt);
}


MigrationManager::RtResponsePort::RtResponsePort(const std::string& _name,
                                     MigrationManager& _migrationmanager)
    : ResponsePort(_name, &_migrationmanager),
      migrationmanager(_migrationmanager),
      blocked(false),
      needRetry(false)
      {}

// Just forward to the mm.
AddrRangeList
MigrationManager::RtResponsePort::getAddrRanges() const
{
    return migrationmanager.getAddrRanges();
}

void
MigrationManager::RtResponsePort::recvFunctional(PacketPtr pkt)
{
    return migrationmanager.handleFunctional(pkt);
}

bool
MigrationManager::RtResponsePort::recvTimingReq(PacketPtr pkt)
{
    // / judge if MM can hadle this request
    if (!migrationmanager.handleRTRequest(pkt)) {     // if cannot, return false
        needRetry = true;       // and we set the needRetry flag, which means we should send retry request after
        return false;
    } // else, handle successfully
    needRetry = false;      // reset the flag
    return true;
}

void
MigrationManager::RtResponsePort::trySendRetry()    // We will actively call this function
{
    assert(needRetry);      // only if when the needRetry if true;
    DPRINTF(MigrationManager, "Sending retry req  to AC\n");
    needRetry = false;
    sendRetryReq();         // sendReqRetry，当slave忙完以后，就给master发这个。master会重发sendTimingReq
}

// this function will not used, becasue we donnot need to send pkt to AC
void
MigrationManager::RtResponsePort::sendPacket(PacketPtr pkt)  // （√）
{
}

// when master is free, this func will receive the request
void
MigrationManager::RtResponsePort::recvRespRetry() 
{

}

Tick
MigrationManager::RtResponsePort::recvAtomic(PacketPtr pkt) {
    return migrationmanager.handleAtomic(pkt);
}


//    ---------------------------------------------- Request(Master) port-------------------
// constructor
MigrationManager::RtRequestPort::RtRequestPort(const std::string& _name,
                                     MigrationManager& _migrationmanager)
    : RequestPort(_name, &_migrationmanager),
      migrationmanager(_migrationmanager),
      blocked(false),
      counter(0),
      needRetry(false)
      //blockedPacket(nullptr) 
      {}

bool
MigrationManager::RtRequestPort::sendPacket(PacketPtr pkt)
{
    panic_if(blocked, "The remapping table is busy");
    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {      // if the RT is busy
        blocked = true;
        return false;
    } else {        // 如果发成功
        return true;
    }
}

// when we receive the resp from RT, it means we block/update the RT successully
bool
MigrationManager::RtRequestPort::recvTimingResp(PacketPtr pkt)
{
    return migrationmanager.handleRTResponse(pkt);
}

void
MigrationManager::RtRequestPort::recvReqRetry() //一般不会调用
{
    migrationmanager.swapPage();
}

void
MigrationManager::RtRequestPort::trySendRetry()        //一般不会调用
{
    if (needRetry && !blocked) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(MigrationManager, "Sending retry req for\n");
        sendRetryResp();         // sendReqRetry，当slave忙完以后，就给master发这个
    }
}

void
MigrationManager::RtRequestPort::recvRangeChange()
{
    migrationmanager.sendRangeChange();
}


// constructor
MigrationManager::DispatcherSidePort::DispatcherSidePort(const std::string& _name,
                                     MigrationManager& _migrationmanager)
    : RequestPort(_name, &_migrationmanager),
      migrationmanager(_migrationmanager),
      blocked(false),
      needRetry(false),
      blockedPacket(nullptr) 
      {}

bool
MigrationManager::DispatcherSidePort::recvTimingResp(PacketPtr pkt)
{
    return migrationmanager.handleDispResponse(pkt);
}

bool
MigrationManager::DispatcherSidePort::sendPacket(PacketPtr pkt)
{
    /* old
    // Note: This flow control is very simple since the memobj is blocking.

    panic_if(blocked, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        return false;
    } else {
        return true;
    }

    //return true;
    */

    //yhl modify 220521
    panic_if(blockedPacket != nullptr, "mm,Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }

    return true;
}

void
MigrationManager::DispatcherSidePort::recvReqRetry()//一般不会调用
{
    // migrationmanager.swapPage();

    //yhl modify 220521

    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);

    std::cout << "recvRetry" << std::endl;


}




void
MigrationManager::DispatcherSidePort::trySendRetry()//一般不会调用
{
    //assert(needRetry);
    if (needRetry && !blocked) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(MigrationManager, "Sending retry req for\n");
        sendRetryResp();         // sendReqRetry，当slave忙完以后，就给master发这个
    }
}

void
MigrationManager::DispatcherSidePort::recvRangeChange()
{
    //owner->sendRangeChange();
    migrationmanager.sendRangeChange();
}


// 需要master(request)类型的端口返回
AddrRangeList 
MigrationManager::getAddrRanges() const {
    DPRINTF(MigrationManager, "MigrationManager sending new ranges\n");
    //DPRINTF(Simple_Dispatcher, "The size of two ranges is:%d--------%d\n",hbmranges.size(),dramranges.size());
    AddrRangeList remapping = rt_request_port.getAddrRanges();
    AddrRangeList dispatcher = dp_side_port.getAddrRanges();

    AddrRangeList res;
    AddrRange ranges(remapping.front().start(), dispatcher.front().end());
    res.push_back(ranges);
    return res;
}

// need the slave(response) type port to return
void 
MigrationManager::sendRangeChange() {
    DPRINTF(MigrationManager, "send Range Change...\n");
    ac_side_port.sendRangeChange();
}


bool 
MigrationManager::IsDispatcher(PacketPtr pkt){
    if (pkt->reqport == Packet::PortType::Dispatcher) {     // if the pkt is from Dispatcher
        return true;
    } else if (pkt->reqport == Packet::PortType::RemappingTable) {
        return false;
    }
}

Tick
MigrationManager::handleAtomic(PacketPtr pkt) {
    // need to judge which 
    if(IsDispatcher(pkt))
        return dp_side_port.sendAtomic(pkt);
    else{
        return rt_request_port.sendAtomic(pkt);
    }
}

void
MigrationManager::handleFunctional(PacketPtr pkt) { 
    if(IsDispatcher(pkt)) {
        dp_side_port.sendFunctional(pkt);
    } else {
        rt_request_port.sendFunctional(pkt);
    }
}
// overhead
MigrationManager::MemStats::MemStats(MigrationManager &_mm)
    : Stats::Group(&_mm), mm(_mm),
    ADD_STAT(MigraFromAc, UNIT_COUNT, "MM Number of packets from ac"),
    ADD_STAT(Migraflathot, UNIT_COUNT, "MM Number of packets of flat_hot"),
    ADD_STAT(Migraflatfir, UNIT_COUNT, "MM Number of packets of flat_first"),
    ADD_STAT(Migraflatsec, UNIT_COUNT, "MM Number of packets of flat_second"),
    ADD_STAT(Migracachefree, UNIT_COUNT, "MM Number of packets of cache_free"),
    ADD_STAT(Migracachecl, UNIT_COUNT, "MM Number of packets of cache_clean"),
    ADD_STAT(Migracachedir, UNIT_COUNT, "MM Number of packets of cache_dirty"),
    ADD_STAT(numofMigra, UNIT_COUNT, "MM Number of migration"),
    ADD_STAT(avMM, UNIT_RATE(Stats::Units::Count, Stats::Units::Second),
             "Average MigrationManager number per second")
{
}

void
MigrationManager::MemStats::regStats()
{
    using namespace Stats;

    Stats::Group::regStats();

    System *sys = mm.system();
    assert(sys);
    const auto max_requestors = sys->maxRequestors();

    MigraFromAc
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        MigraFromAc.subname(i, sys->getRequestorName(i));
    }

    Migraflathot
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        Migraflathot.subname(i, sys->getRequestorName(i));
    }

    Migraflatfir
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        Migraflatfir.subname(i, sys->getRequestorName(i));
    }

    Migraflatsec
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        Migraflatsec.subname(i, sys->getRequestorName(i));
    }

    Migracachefree
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        Migracachefree.subname(i, sys->getRequestorName(i));
    }

    Migracachecl
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        Migracachecl.subname(i, sys->getRequestorName(i));
    }

    Migracachedir
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        Migracachedir.subname(i, sys->getRequestorName(i));
    }

    numofMigra
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numofMigra.subname(i, sys->getRequestorName(i));
    }

    avMM
        .precision(0)
        .prereq(MigraFromAc)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        avMM.subname(i, sys->getRequestorName(i));
    }

    avMM = MigraFromAc / simSeconds;

}
// overhead