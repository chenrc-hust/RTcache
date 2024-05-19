/**
 * @file
 * dispatcher.cc
 * Last modify Time:2021.3.15
 * Author:hjy
 */
#include "base/trace.hh"
#include "mem/dispatcher.hh"
#include "debug/Dispatcher.hh"

namespace gem5 {

namespace memory
{

Dispatcher::Dispatcher(const DispatcherParams& params)
    : SimObject(params),
      bandthofdram(0),
      bandthofhbm(0),
      rt_side_port(name() + ".rt_side_port", *this, Dispatcher::PortType::RemappingTable),
      //mm_side_port(name() + ".mm_side_port", *this, Dispatcher::PortType::MigrationManager),
    //   ac_side_port(name() + ".ac_side_port", *this, Dispatcher::PortType::Accesscounter),
      hbm_side_port(name() + ".hbm_side_port", *this, Dispatcher::PortType::PhysicalHbm),
      dram_side_port(name() + "dram_side_port", *this, Dispatcher::PortType::PhysicalDram),
      //nvm_side_port(name() + "nvm_side_port", *this, Dispatcher::PortType::PhysicalNvm),
    //   maxhbmsize(1342177280),
    //   maxdramsize(1073741824),
    //   maxnvmsize(8589934592),
      stats(*this), // overhead
      _system(NULL)
      {
        // _ac = accesscounter();
        for (int i = 0; i < BlockType::BlockTypeSize; i++) {
            blocked[i] = false;
        }
    }

Port&
Dispatcher::getPort(const std::string& if_name, PortID idx) {//根据请求端口的名称返回对应的端口
    if (if_name == "rt_side_port") {
        return rt_side_port;
    }/*  else if (if_name == "mm_side_port") {
        return mm_side_port;
    } else if (if_name == "ac_side_port") {
        return ac_side_port;
    }  */else if (if_name == "hbm_side_port") {
        return hbm_side_port;
    } else if (if_name == "dram_side_port") {
        return dram_side_port;
    } /* else if (if_name == "nvm_side_port") {
        return nvm_side_port;
    }  */else {
        return SimObject::getPort(if_name, idx);
    }
}

AddrRangeList
Dispatcher::getAddrRanges() const {
    DPRINTF(Dispatcher, "Sending new ranges\n");
    //AddrRangeList hbmranges = hbm_side_port.getAddrRanges();
    AddrRangeList dramranges = dram_side_port.getAddrRanges();
    //AddrRangeList res;
    //AddrRange ranges(dramranges.front().start(), hbmranges.front().end());
    //res.push_back(ranges);
    //return res;
    //AddrRangeList nvmranges = nvm_side_port.getAddrRanges();
    //by crc 240516
    return dramranges;
}

void
Dispatcher::sendRangeChange() {
    DPRINTF(Dispatcher, "send Range Change...\n");    
    rt_side_port.sendRangeChange();
}  


Tick
Dispatcher::handleAtomic(PacketPtr pkt) {
    if(pkt->isCache){
        return hbm_side_port.sendAtomic(pkt);
    }//by crc 240516
    else/*  if(pkt->isDram) */{
        return dram_side_port.sendAtomic(pkt);
    }
    //by crc 240514
    /* else{
        return nvm_side_port.sendAtomic(pkt);
    } */
}

void
Dispatcher::handleFunctional(PacketPtr pkt) { 
    if (pkt->isCache){
        hbm_side_port.sendFunctional(pkt);//hbm相当于片上高速缓存部分？
    }//by crc 240516
    else/*  if(pkt->isDram) */{
        dram_side_port.sendFunctional(pkt);
    }
    /* else{
        nvm_side_port.sendFunctional(pkt);
    } */
    
}

bool
Dispatcher::handleRequest(PacketPtr pkt) {
    DPRINTF(Dispatcher, "Got request for addr %#x\n", pkt->getAddr());
    PacketPtr acpkt = new Packet(pkt, false, true);
    //这一段能不能修改到复制构造函数里
    acpkt->remapaddr = pkt->remapaddr;
    acpkt->iswrite = pkt->isWrite();
    acpkt->isCache = pkt->isCache;
    acpkt->isDram = pkt->isDram;
    // acpkt->hasmigrate = pkt->hasmigrate;
    acpkt->reqport = pkt->reqport;
    acpkt->respport = pkt->respport;
    //发往DRAMcache的包
    if(pkt->isCache){
        if (blocked[BlockType::Rt2Hbm] /* || blocked[BlockType::Mm2Hbm] */ || !hbm_side_port.sendPacket(pkt)) {
            DPRINTF(Dispatcher, "Physical HBM is busy! Request blocked for addr %#x\n", pkt->remapaddr);
            if (pkt->reqport == Packet::PortType::RemappingTable) {// 如果请求端口是rt。则阻塞rt到hbm
                blocked[BlockType::Rt2Hbm] = true;
            } /* else if (pkt->reqport == Packet::PortType::MigrationManager) {
                blocked[BlockType::Mm2Hbm] = true;
            } */
            delete acpkt;//发送方会缓存包
            return false;
        }
        if(pkt->reqport == Packet::PortType::RemappingTable){
            stats.numDisps[pkt->req->requestorId()]++;
            stats.numBusDram[pkt->req->requestorId()]++;
            if(pkt->isRead())
                stats.numBusDramRead[pkt->req->requestorId()]++;
        }
        else{
            stats.numDisps[pkt->req->requestorId()]++;
            //stats.numMM[pkt->req->requestorId()]++;
        }
    }
    //2.在flat_dram中命中，也就是在内存中命中
    else if (pkt->isDram) {
        if (blocked[BlockType::Rt2Dram] /* || blocked[BlockType::Mm2Dram]  */|| !dram_side_port.sendPacket(pkt)) {//尝试发包
            DPRINTF(Dispatcher, "Physical DRAM is busy! Request blocked for addr %#x\n", pkt->remapaddr);
            if (pkt->reqport == Packet::PortType::RemappingTable) {
                blocked[BlockType::Rt2Dram] = true;
            }/*  else if (pkt->reqport == Packet::PortType::MigrationManager) {
                blocked[BlockType::Mm2Dram] = true;
            } */
            delete acpkt;
            return false;
        }
        if(pkt->reqport == Packet::PortType::RemappingTable){
            // overhead
            stats.numDisps[pkt->req->requestorId()]++;
            stats.numBusDram[pkt->req->requestorId()]++;
            if(pkt->isRead())
                stats.numBusDramRead[pkt->req->requestorId()]++;
        }
        else{
            //stats.numMM[pkt->req->requestorId()]++;
            stats.numDisps[pkt->req->requestorId()]++;   
        }
            
    } else{
        DPRINTF(Dispatcher, "handleRequest error should no pkt out dram and hbm ", pkt->remapaddr);
    }
    //1.如果是发往NVM的pkt，直接下发
    /* else{
        if (blocked[BlockType::Rt2Nvm] || blocked[BlockType::Mm2Nvm] || !nvm_side_port.sendPacket(pkt)){
            DPRINTF(Dispatcher, "Physical DRAM is busy! Request blocked for addr %#x\n", pkt->remapaddr);
            if (pkt->reqport == Packet::PortType::RemappingTable) {
                blocked[BlockType::Rt2Nvm] = true;
            } else if (pkt->reqport == Packet::PortType::MigrationManager) {
                blocked[BlockType::Mm2Nvm] = true;
            }
            return false;
        }
        if(pkt->reqport == Packet::PortType::RemappingTable){
            // overhead
            stats.numDisps[pkt->req->requestorId()]++;
            stats.numBusNvm[pkt->req->requestorId()]++;
            if(pkt->isRead())
                stats.numBusNvmRead[pkt->req->requestorId()]++;
        }
        else{
            stats.numMM[pkt->req->requestorId()]++;
            stats.numDisps[pkt->req->requestorId()]++;   
        }
    } */
    //发送方是RemappingTable，并且发送失败
    // if (acpkt->reqport == Packet::PortType::RemappingTable && (blocked[BlockType::Rt2Ac] || !ac_side_port.sendPacket(acpkt))) {
    //     DPRINTF(Dispatcher, "Access Counter is busy! Request blocked for addr %#x\n", acpkt->remapaddr);
    //     if (acpkt->reqport == Packet::PortType::RemappingTable) {
    //         blocked[BlockType::Rt2Ac] = true;
    //     }
    //     delete acpkt;
    //     return false;
    // }
    delete acpkt;
    return true;
}
//接收响应包
bool
Dispatcher::handleResponse(PacketPtr pkt) {
    if (pkt->reqport == Packet::PortType::RemappingTable) {
        // if (blocked[BlockType::Dram2Rt] || blocked[BlockType::Hbm2Rt] || !rt_side_port.sendPacket(pkt)) {
        // DPRINTF(Dispatcher, "Remapping table is busy! Response blocked for addr %#x\n", pkt->remapaddr);
        if (pkt->respport == Packet::PortType::PhysicalDram) {//将dram响应的数据包发给rt模块
            DPRINTF(Dispatcher, "pkt->respport == Packet::PortType::PhysicalDram\n");
            if(blocked[BlockType::Dram2Rt] ) {                
                DPRINTF(Dispatcher, "Remapping table is busy! dram has blocked Response blocked for addr %#x\n", pkt->remapaddr);
                return false;
            }
            else{//发送响应
                if (!rt_side_port.sendPacket(pkt)){                   
                    blocked[BlockType::Dram2Rt] = true;
                    DPRINTF(Dispatcher, "Remapping table is busy! dram Response blocked for addr %#x\n", pkt->remapaddr);         
                    return false;
                }
            }
        }
        else if(pkt->respport == Packet::PortType::PhysicalHbm){
            DPRINTF(Dispatcher, "pkt->respport == Packet::PortType::PhysicalHbm\n");
            if(blocked[BlockType::Hbm2Rt] ) {                
                DPRINTF(Dispatcher, "Remapping table is busy! hbm has blocked Response blocked for addr %#x\n", pkt->remapaddr);
                return false;
            }
            else{
                if (!rt_side_port.sendPacket(pkt)){                   
                    blocked[BlockType::Hbm2Rt] = true;
                    DPRINTF(Dispatcher, "Remapping table is busy! hbm Response blocked for addr %#x\n", pkt->remapaddr);  
                    return false;
                }
            }
        }
        /* else if(pkt->respport == Packet::PortType::PhysicalNvm){
            if(blocked[BlockType::Nvm2Rt] ) {                
                DPRINTF(Dispatcher, "Remapping table is busy! nvm has blocked Response blocked for addr %#x\n", pkt->remapaddr);
                return false;
            }
            else{
                if (!rt_side_port.sendPacket(pkt)){                   
                    blocked[BlockType::Nvm2Rt] = true;
                    DPRINTF(Dispatcher, "Remapping table is busy! nvm Response blocked for addr %#x\n", pkt->remapaddr);  
                    return false;
                }
            }
        } */
    } /* else if (pkt->reqport == Packet::PortType::MigrationManager) {
        if (pkt->respport == Packet::PortType::PhysicalDram) {
            if(blocked[BlockType::Dram2Mm] ){
                DPRINTF(Dispatcher, "Migration Manager is busy! dram has blocked Response blocked for addr %#x\n", pkt->remapaddr);
                return false;
            }
            else{
                if (!mm_side_port.sendPacket(pkt)){                   
                    blocked[BlockType::Dram2Mm] = true;
                    DPRINTF(Dispatcher, "Migration Manager is busy! dram Response blocked for addr %#x\n", pkt->remapaddr);
                    return false;
                }
            }
        }
        else if(pkt->respport == Packet::PortType::PhysicalHbm){
            if(blocked[BlockType::Hbm2Mm] ){
                DPRINTF(Dispatcher, "Migration Manager is busy! hbm has blocked Response blocked for addr %#x\n", pkt->remapaddr);
                return false;
            }
            else{
                if (!mm_side_port.sendPacket(pkt)){    
                    DPRINTF(Dispatcher, "Migration Manager is busy! hbm Response blocked for addr %#x\n", pkt->remapaddr);               
                    blocked[BlockType::Hbm2Mm] = true;
                    return false;
                }
            }
        }
        else if(pkt->respport == Packet::PortType::PhysicalNvm){
            if(blocked[BlockType::Nvm2Mm] ){
                DPRINTF(Dispatcher, "Migration Manager is busy! nvm has blocked Response blocked for addr %#x\n", pkt->remapaddr);
                return false;
            }
            else{
                if (!mm_side_port.sendPacket(pkt)){    
                    DPRINTF(Dispatcher, "Migration Manager is busy! nvm Response blocked for addr %#x\n", pkt->remapaddr);               
                    blocked[BlockType::Nvm2Mm] = true;
                    return false;
                }
            }
        }
    } */
    DPRINTF(Dispatcher, "Got response for addr %#x\n", pkt->getAddr());

    return true;
}
//阻塞时重发
void
Dispatcher::handleReqRetry() {
    assert(blocked[BlockType::Rt2Ac] ||
           blocked[BlockType::Rt2Dram] || blocked[BlockType::Mm2Dram] ||
           blocked[BlockType::Rt2Hbm] || blocked[BlockType::Mm2Hbm] ||
           blocked[BlockType::Rt2Nvm] || blocked[BlockType::Mm2Nvm]);

    /* if (blocked[BlockType::Mm2Dram] || blocked[BlockType::Mm2Nvm]) {
        blocked[BlockType::Mm2Dram] = false;
        blocked[BlockType::Mm2Hbm] = false;
        blocked[BlockType::Mm2Nvm] = false;
        mm_side_port.trySendRetry();
    }

    else  */if (blocked[BlockType::Rt2Ac] || blocked[BlockType::Rt2Dram] /* || blocked[BlockType::Rt2Nvm] */) {
        blocked[BlockType::Rt2Ac] = false;
        blocked[BlockType::Rt2Dram] = false;
        blocked[BlockType::Rt2Hbm] = false;
        // blocked[BlockType::Rt2Nvm] = false;
        rt_side_port.trySendRetry();
    }
    
}
//要求下一级重发响应，要求dram重发响应
void
Dispatcher::handleRespRetry() {
    assert(blocked[BlockType::Dram2Rt] || blocked[BlockType::Hbm2Rt] /* ||
           blocked[BlockType::Dram2Mm] || blocked[BlockType::Hbm2Mm] ||
           blocked[BlockType::Nvm2Mm] || blocked[BlockType::Nvm2Rt] */);
    if (blocked[BlockType::Dram2Rt]/*  || blocked[BlockType::Dram2Mm] */) {
        DPRINTF(Dispatcher, "handle RespRetry,Response for dram blocked  \n");
        blocked[BlockType::Dram2Rt] = false;
        /* blocked[BlockType::Dram2Mm] = false; */
        dram_side_port.trySendRetry();
    }
    // else
    if (blocked[BlockType::Hbm2Rt] /* || blocked[BlockType::Hbm2Mm] */) {
        DPRINTF(Dispatcher, "handle RespRetry,Response for hbm blocked  \n");
        blocked[BlockType::Hbm2Rt] = false;
        /* blocked[BlockType::Hbm2Mm] = false; */
        hbm_side_port.trySendRetry();
    }
    /* if (blocked[BlockType::Nvm2Rt] || blocked[BlockType::Nvm2Mm]) {
        DPRINTF(Dispatcher, "handle RespRetry,Response for nvm blocked  \n");
        blocked[BlockType::Nvm2Rt] = false;
        blocked[BlockType::Nvm2Mm] = false;
        nvm_side_port.trySendRetry();
    } */
}


Dispatcher::CpuSidePort::CpuSidePort(const std::string& _name,
                                     Dispatcher& _disp,
                                     Dispatcher::PortType _type)
    : ResponsePort(_name, &_disp),
      disp(_disp),
      porttype(_type),
      needRetry(false),
    //   blocked(false),
      hbm_blocked(false),
      dram_blocked(false)/* ,
      nvm_blocked(false) */{}

AddrRangeList
Dispatcher::CpuSidePort::getAddrRanges() const {
    return disp.getAddrRanges();
}

//尝试发包
bool
Dispatcher::CpuSidePort::sendPacket(PacketPtr pkt) {
    // panic_if(blocked, "Should never try to send if blocked!");
    panic_if(hbm_blocked && dram_blocked /* && nvm_blocked */, "Should never try to send if blocked!");
    if (!sendTimingResp(pkt)) {
        if(pkt->isCache){
            hbm_blocked = true;
        }
        else/*  if(pkt->isDram) */{
            dram_blocked = true;
        }                                                                                                                   
        /* else
            nvm_blocked = true; */
        return false;
    } else {
        return true;
    }
}

void
Dispatcher::CpuSidePort::trySendRetry()
{
    // if (needRetry && !blocked) {
    if (needRetry && !(dram_blocked && hbm_blocked/*  && nvm_blocked */)) {
        needRetry = false;
        DPRINTF(Dispatcher, "Sending retry req for %d\n", id);
        sendRetryReq();
    }
}

Tick
Dispatcher::CpuSidePort::recvAtomic(PacketPtr pkt) {
    return disp.handleAtomic(pkt);
}

void
Dispatcher::CpuSidePort::recvFunctional(PacketPtr pkt) {
    disp.handleFunctional(pkt);
}

void
Dispatcher::CpuSidePort::setReqPort(PacketPtr pkt) {
    if (porttype == Dispatcher::PortType::RemappingTable) {
        pkt->reqport = Packet::PortType::RemappingTable;
    } /* else if (porttype == Dispatcher::PortType::MigrationManager) {
        pkt->reqport = Packet::PortType::MigrationManager;
    } */
}

bool
Dispatcher::CpuSidePort::recvTimingReq(PacketPtr pkt) {
    setReqPort(pkt);
    if (!disp.handleRequest(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

void
Dispatcher::CpuSidePort::recvRespRetry() {
    // assert(blocked);
    // blocked = false;
    assert(dram_blocked /* || nvm_blocked */ || hbm_blocked);
    if(hbm_blocked)
        hbm_blocked = false;
    if(dram_blocked)
        dram_blocked = false;
    // if(nvm_blocked)
    //     nvm_blocked = false;
    disp.handleRespRetry();
}

Dispatcher::MemSidePort::MemSidePort(const std::string& _name,
                                     Dispatcher& _disp,
                                     Dispatcher::PortType _type)
    : RequestPort(_name, &_disp),
      disp(_disp),
      porttype(_type),
      needRetry(false),
      blocked(false) {}

bool
Dispatcher::MemSidePort::sendPacket(PacketPtr pkt) {
    panic_if(blocked, "Should never try to send if blocked!");
    // If we can't send the packet across the port, return false.
    if (!sendTimingReq(pkt)) {
        blocked = true;
        return false;
    } 
    else
        return true;
}

void
Dispatcher::MemSidePort::trySendRetry() {
    if (needRetry && !blocked) {
        needRetry = false;
        DPRINTF(Dispatcher, "Sending retry req for %d\n", id);
        sendRetryResp();
    }
}

void
Dispatcher::MemSidePort::setRespPort(PacketPtr pkt) {
    if (porttype == Dispatcher::PortType::PhysicalDram) {
        pkt->respport = Packet::PortType::PhysicalDram;
    } else/*  if (porttype == Dispatcher::PortType::PhysicalHbm)  */{
        pkt->respport = Packet::PortType::PhysicalHbm;
    } /* else if (porttype == Dispatcher::PortType::PhysicalNvm) {
        pkt->respport = Packet::PortType::PhysicalNvm;
    } */
    
}

bool
Dispatcher::MemSidePort::recvTimingResp(PacketPtr pkt) {
    setRespPort(pkt);
    if (!disp.handleResponse(pkt)) {
        needRetry = true;
        DPRINTF(Dispatcher, "Dispatcher::MemSidePort::recvTimingResp \n");
        return false;
    } else {
        return true;
    }
}

void
Dispatcher::MemSidePort::recvReqRetry() {
    assert(blocked);
    blocked = false;
    disp.handleReqRetry();
}

void
Dispatcher::MemSidePort::recvRangeChange() {
    disp.sendRangeChange();
}
// overhead
Dispatcher::MemStats::MemStats(Dispatcher &_disp)
    : statistics::Group(&_disp), disp(_disp),
    ADD_STAT(numBusDram, statistics::units::Count::get(), "Number of bus dram packets"),
    ADD_STAT(numBusHbm, statistics::units::Count::get(), "Number of bus hbm packets"),
    ADD_STAT(numBusNvm, statistics::units::Count::get(), "Number of bus nvm packets"),
    ADD_STAT(numMM, statistics::units::Count::get(), "Number of access mm packets"),
    ADD_STAT(numDisps, statistics::units::Count::get(), "Number of packets dispatch"),
    ADD_STAT(numBusDramRead, statistics::units::Count::get(), "Number of packets bus dram read"),
    ADD_STAT(numBusHbmRead, statistics::units::Count::get(), "Number of packets bus hbm read"),
    ADD_STAT(numBusNvmRead, statistics::units::Count::get(), "Number of packets bus nvm read"),
    ADD_STAT(avNumBusDram, statistics::units::Ratio::get(),
             "Average from bus dram number per second"),
    ADD_STAT(avNumBusHbm, statistics::units::Ratio::get(),
             "Average from bus hbm number per second"),
    ADD_STAT(avNumBusNvm, statistics::units::Ratio::get(),
             "Average from bus nvm number per second"),
    ADD_STAT(avDisps, statistics::units::Ratio::get(),
             "Average dispatch number per second")
{
}

void
Dispatcher::MemStats::regStats()
{
    using namespace statistics;

    statistics::Group::regStats();

    System *sys = disp.system();
    assert(sys);
    const auto max_requestors = sys->maxRequestors();

    numBusDram
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numBusDram.subname(i, sys->getRequestorName(i));
    }

    numBusHbm
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numBusHbm.subname(i, sys->getRequestorName(i));
    }

    numBusNvm
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numBusNvm.subname(i, sys->getRequestorName(i));
    }


    numMM
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numMM.subname(i, sys->getRequestorName(i));
    }

    numDisps
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numDisps.subname(i, sys->getRequestorName(i));
    }

    numBusDramRead
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numBusDramRead.subname(i, sys->getRequestorName(i));
    }

    numBusHbmRead
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numBusHbmRead.subname(i, sys->getRequestorName(i));
    }
    
    numBusNvmRead
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numBusNvmRead.subname(i, sys->getRequestorName(i));
    }

    avNumBusDram
        .precision(0)
        .prereq(numBusDram)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        avNumBusDram.subname(i, sys->getRequestorName(i));
    }

    avNumBusDram = numBusDram / simSeconds;

    avNumBusHbm
        .precision(0)
        .prereq(numBusHbm)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        avNumBusHbm.subname(i, sys->getRequestorName(i));
    }

    avNumBusHbm = numBusHbm / simSeconds;

    avNumBusNvm
        .precision(0)
        .prereq(numBusNvm)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        avNumBusNvm.subname(i, sys->getRequestorName(i));
    }

    avNumBusNvm = numBusNvm / simSeconds;

    avDisps
        .precision(0)
        .prereq(numDisps)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        avDisps.subname(i, sys->getRequestorName(i));
    }

    avDisps = numDisps / simSeconds;
}

} // namespace memory
} // namespace gem5

// overhead