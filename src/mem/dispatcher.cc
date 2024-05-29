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
      rt_side_port(name() + ".rt_side_port", *this, Dispatcher::PortType::RemappingTable),
      ps_side_port(name() + ".ps_side_port", *this, Dispatcher::PortType::PageSwaper),
      wc_side_port(name() + ".wc_side_port", *this, Dispatcher::PortType::WearLevelcontrol),
      hbm_side_port(name() + ".hbm_side_port", *this, Dispatcher::PortType::PhysicalHbm),
      dram_side_port(name() + ".dram_side_port", *this, Dispatcher::PortType::PhysicalDram),
      event([this]{processEvent();}, name()), 
      adjust_latency(1000000),
       _system(NULL),
      stats(*this) // overhead
    {
        _wc = wearlevelcontrol();
        for (int i = 0; i < BlockType::BlockTypeSize; i++) {
            blocked[i] = false;
        }
    }

Port&
Dispatcher::getPort(const std::string& if_name, PortID idx) {//根据请求端口的名称返回对应的端口
    if (if_name == "rt_side_port") {
        return rt_side_port;
    } else if (if_name == "ps_side_port") {
        return ps_side_port;
    } else if (if_name == "wc_side_port") {
        return wc_side_port;
    } else if (if_name == "hbm_side_port") {
        return hbm_side_port;
    } else if (if_name == "dram_side_port") {
        return dram_side_port;
    } else {
        return SimObject::getPort(if_name, idx);
    }
}

AddrRangeList
Dispatcher::getAddrRanges() const {  //实际上程序直接访问的部分
    DPRINTF(Dispatcher, "Sending new ranges\n");
    // AddrRangeList hbmranges = hbm_side_port.getAddrRanges();
    AddrRangeList dramranges = dram_side_port.getAddrRanges();//只让cpu访问
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
        DPRINTF(Dispatcher, "Dispatcher::handleAtomic isCache\n");
        return hbm_side_port.sendAtomic(pkt);
    }//by crc 240516
    else{
        return dram_side_port.sendAtomic(pkt);
    }
    //by crc 240514
}

void
Dispatcher::handleFunctional(PacketPtr pkt) { 
    if (pkt->isCache){
        DPRINTF(Dispatcher, "hbm_side_port.sendFunctional \n");
        hbm_side_port.sendFunctional(pkt);//hbm相当于片上高速缓存部分？
    }//by crc 240516
    else{
        dram_side_port.sendFunctional(pkt);
    }
}
//dp会根据iscache来判断发给哪个内存。
bool
Dispatcher::handleRequest(PacketPtr pkt) {
    DPRINTF(Dispatcher, "Got request for addr %#x\n", pkt->getAddr());
    PacketPtr wcpkt = new Packet(pkt, false, true);//复制一个包发送到ac中做统计
    //这一段能不能修改到复制构造函数里
    wcpkt->remapaddr = pkt->remapaddr;
    wcpkt->iswrite = pkt->isWrite();
    wcpkt->isCache = pkt->isCache;
    wcpkt->isDram = pkt->isDram;
    wcpkt->hasswap = pkt->hasswap;
    wcpkt->reqport = pkt->reqport;
    wcpkt->respport = pkt->respport;
    wcpkt->pkttype = pkt->pkttype;
    //
    if(pkt->isCache&&!hbm_side_port.blocked()){//iscache会发送包到hbm
        hbm_side_port.sendPacket(pkt);
        if(pkt->reqport == Packet::PortType::RemappingTable){
            stats.numDisps[pkt->req->requestorId()]++;
            stats.numBusDram[pkt->req->requestorId()]++;
            if(pkt->isRead())
                stats.numBusDramRead[pkt->req->requestorId()]++;
        }
        else{
            stats.numDisps[pkt->req->requestorId()]++;
            stats.numPS[pkt->req->requestorId()]++;
        }
    }
    //2.访问内存的包
    else if (!dram_side_port.blocked()) {//isdram会发包到dram
            dram_side_port.sendPacket(pkt);//发包
            if(pkt->reqport == Packet::PortType::RemappingTable){
                // overhead
                stats.numDisps[pkt->req->requestorId()]++;
                stats.numBusDram[pkt->req->requestorId()]++;
                if(pkt->isRead())
                    stats.numBusDramRead[pkt->req->requestorId()]++;
            }
            else{
                stats.numDisps[pkt->req->requestorId()]++; 
                stats.numPS[pkt->req->requestorId()]++;  
            }
                
    }else{//hbm 和dram都发送失败，
        delete wcpkt;
        return false;
    }

    //发送拷贝包到wc中， 统计，这个包可能发送失败吗？
    if (wcpkt->reqport == Packet::PortType::RemappingTable&& wcpkt->pkttype != Packet::PacketType::AccessRt && !wc_side_port.blocked()) {
        wc_side_port.sendPacket(wcpkt);
        //by crc 240522
    }

    return true;
}
//接收响应包,hbm的包要响应吗？，应该不需要转发，直接返回true就行了
bool
Dispatcher::handleResponse(PacketPtr pkt) {
    if (pkt->reqport == Packet::PortType::RemappingTable&&!rt_side_port.blocked()) {
        rt_side_port.sendPacket(pkt);//
        rt_side_port.trySendRetry();
        return true;
        // DPRINTF(Dispatcher, "Got response for addr %#x to RemappingTable\n", pkt->getAddr());
    } else if (pkt->reqport == Packet::PortType::PageSwaper&&!ps_side_port.blocked()) {
        ps_side_port.sendPacket(pkt);/* 发送完响应之后尝试让req发送阻塞的包 */
        ps_side_port.trySendRetry();
        return true;
        // DPRINTF(Dispatcher, "Got response for addr %#x to PageSwaper \n", pkt->getAddr());
    }
    return false;
}

Dispatcher::CpuSidePort::CpuSidePort(const std::string& _name,
                                     Dispatcher& _disp,
                                     Dispatcher::PortType _type)
    : ResponsePort(_name),
      disp(_disp),
      porttype(_type),
      needRetry(false),
      blockedPacket(nullptr){}

AddrRangeList
Dispatcher::CpuSidePort::getAddrRanges() const {
    return disp.getAddrRanges();
}

//尝试发包给上一级
void
Dispatcher::CpuSidePort::sendPacket(PacketPtr pkt) {//
    // panic_if(blocked, "Should never try to send if blocked!");
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");
    
    if (!sendTimingResp(pkt)) {                                                                                                     
        DPRINTF(Dispatcher, "Dispatcher CpuSidePort sendPacket false\n");
        blockedPacket = pkt;
    }
}

/* 需要上层重发，并且当前没有待发送的response */
void
Dispatcher::CpuSidePort::trySendRetry()
{   
    if (needRetry && blockedPacket == nullptr) {
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
Dispatcher::CpuSidePort::setReqPort(PacketPtr pkt) {//在dp中接收到包时就设置包的类型
    if (porttype == Dispatcher::PortType::RemappingTable) {
        pkt->reqport = Packet::PortType::RemappingTable;
    } else if (porttype == Dispatcher::PortType::PageSwaper) {
        pkt->reqport = Packet::PortType::PageSwaper;
    }
}
//
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
    assert(blockedPacket != nullptr);

    PacketPtr pkt = blockedPacket;
    
    blockedPacket = nullptr;

    DPRINTF(Dispatcher, "Dispatcher CpuSidePort Retrying response pkt %s\n", pkt->print());
    
    sendPacket(pkt);

    trySendRetry();
}

Dispatcher::MemSidePort::MemSidePort(const std::string& _name,
                                     Dispatcher& _disp,
                                     Dispatcher::PortType _type)
    : RequestPort(_name),
      disp(_disp),
      porttype(_type),
      needRetry(false),
      blockedPacket(nullptr) {}

void
Dispatcher::MemSidePort::sendPacket(PacketPtr pkt) {
    panic_if(blockedPacket!= nullptr, "Should never try to send if blocked!");
    // If we can't send the packet across the port, return false.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

void
Dispatcher::MemSidePort::setRespPort(PacketPtr pkt) {
    if (porttype == Dispatcher::PortType::PhysicalDram) {
        pkt->respport = Packet::PortType::PhysicalDram;
    } else if (porttype == Dispatcher::PortType::PhysicalHbm) {
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
    assert(blockedPacket != nullptr);

    PacketPtr pkt = blockedPacket;

    blockedPacket = nullptr;

    sendPacket(pkt);
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
    ADD_STAT(numPS, statistics::units::Count::get(), "Number of access ps packets"),
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


    numPS
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numPS.subname(i, sys->getRequestorName(i));
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
void Dispatcher::startup(){
    // schedule(event, adjust_latency);
}

void Dispatcher::processEvent() {
    // DPRINTF(Dispatcher, "Dispatcher EventProcess");
    // for(auto it:blocked)std::cout<<" "<<it<<" "<<std::endl;
    // DPRINTF(Dispatcher, " Dispatcher EventProcess \n");
    // schedule(event, curTick() + adjust_latency);
}

} // namespace memory
} // namespace gem5

// overhead