#include "base/trace.hh"
#include "mem/simple_dispatcher.hh"
#include "debug/Simple_Dispatcher.hh"

namespace gem5 {

namespace memory
{

Simple_Dispatcher::Simple_Dispatcher(const Simple_DispatcherParams &params)
    : SimObject(params),
      bus_side_port(name() + ".bus_side_port", *this, Simple_Dispatcher::PortType::SimpleDispatcher),
      dram_side_port(name() + ".dram_side_port", *this, Simple_Dispatcher::PortType::PhysicalDram),
      hbm_side_port(name() + ".hbm_side_port",*this, Simple_Dispatcher::PortType::PhysicalHbm),
      event([this]{processEvent();}, name()),
      busdram_side_blocked(false),
      bushbm_side_blocked(false),
      mem_side_blocked(false),
    //   maxhbmsize(1048576),
      maxdramsize(2147483648),//2GB
      stats(*this), // overhead
      _system(NULL){
        for (int i = 0; i < SMBlockType::SMBlockTypeSize; i++) {
            blocked[i] = false;
        }
      }

Port& 
Simple_Dispatcher::getPort(const std::string &if_name, PortID idx) {
    if (if_name == "bus_side_port") {
        return bus_side_port;
    } else if (if_name == "dram_side_port") {
        return dram_side_port;
    } else if(if_name == "hbm_side_port") {
        return hbm_side_port;
    }else {
        return SimObject::getPort(if_name, idx);
    }
}

AddrRangeList
Simple_Dispatcher::getAddrRanges() const {
    DPRINTF(Simple_Dispatcher, "Sending new ranges\n");
    AddrRangeList hbmranges = hbm_side_port.getAddrRanges();
    AddrRangeList dramranges = dram_side_port.getAddrRanges();
    //DPRINTF(Simple_Dispatcher, "The size of two ranges is:%d--------%d\n",hbmranges.size(),dramranges.size());
    AddrRangeList res;
    // modify 8.24
    AddrRange ranges(dramranges.front().start(),hbmranges.front().end());
    res.push_back(ranges);
    return res;
}

void
Simple_Dispatcher::sendRangeChange() {
    DPRINTF(Simple_Dispatcher, "send Range Change...\n");
    bus_side_port.sendRangeChange();
}

Tick
Simple_Dispatcher::handleAtomic(PacketPtr pkt) {
    if(IsDramOrHbm(pkt))
        return dram_side_port.sendAtomic(pkt);
    return hbm_side_port.sendAtomic(pkt);
}

void
Simple_Dispatcher::handleFunctional(PacketPtr pkt) { 
    if(IsDramOrHbm(pkt))
        dram_side_port.sendFunctional(pkt);
    else
        hbm_side_port.sendFunctional(pkt);
}

bool
Simple_Dispatcher::handleRequest(PacketPtr pkt) {
    DPRINTF(Simple_Dispatcher, "Got request for addr %#x\n", pkt->getAddr());
    // 统计时延
    PacketsLatency[pkt->getAddr()] = curTick();
    // add event on 8.16
    if (!event.scheduled()) {
        DPRINTF(Simple_Dispatcher, "req event schedule\n");
        schedule(event, curTick());
    }

    // Simply forward to the memory port
    //mem_side_port不能向下转发request需要阻塞bus_side_port让它停止接受上面发送的request
    //做地址的分发，将包通过不同的memport交给不同的physmem处理
    // true为dram
    if(IsDramOrHbm(pkt)){
        DPRINTF(Simple_Dispatcher, "Request to dram for addr %#x\n", pkt->getAddr());
        if (blocked[SMBlockType::SM2DRAM] || !dram_side_port.sendPacket(pkt)) 
        {
            DPRINTF(Simple_Dispatcher, "DRAM Request blocked for addr %#x\n", pkt->getAddr());
            blocked[SMBlockType::SM2DRAM] = true;
            // busdram_side_blocked = true;
            return false;
        }
        stats.numofdramHit[pkt->req->requestorId()]++;
        return true;
    }
    else{
        DPRINTF(Simple_Dispatcher, "Request to hbm for addr %#x\n", pkt->getAddr());
        if (blocked[SMBlockType::SM2HBM] || !hbm_side_port.sendPacket(pkt)) {
            DPRINTF(Simple_Dispatcher, "HBM Request blocked for addr %#x\n", pkt->getAddr());
            blocked[SMBlockType::SM2HBM] = true;
            // bushbm_side_blocked = true;
            return false;//hjy modify 6.7,之前这里没有return false
        } 
        stats.numofhbmHit[pkt->req->requestorId()]++;
        return true;
    }
}

bool
Simple_Dispatcher::handleResponse(PacketPtr pkt) {
    // 统计时延 
    if(PacketsLatency.find(pkt->getAddr()) != PacketsLatency.end()){
        Tick thislatency = curTick() - PacketsLatency[pkt->getAddr()];
        PacketsLatency.erase(pkt->getAddr());
        stats.sumofpacketslatency[pkt->req->requestorId()] += thislatency;
    }

    // add event on 8.16
    if (!event.scheduled()) {
        DPRINTF(Simple_Dispatcher, "resp event schedule\n");
        schedule(event, curTick());
    }
    if(pkt->respport == Packet::PortType::PhysicalDram){
        if(blocked[SMBlockType::DRAM2SM])
            return false;
        else{
            if(!bus_side_port.sendPacket(pkt)){
                blocked[SMBlockType::DRAM2SM] = true;
                return false;
            }
        }
    }
    else{
        if(blocked[SMBlockType::HBM2SM])
            return false;
        else{
            if(!bus_side_port.sendPacket(pkt)){
                blocked[SMBlockType::HBM2SM] = true;
                return false;
            }
        }
    }
    DPRINTF(Simple_Dispatcher, "Got response for addr %#x\n", pkt->getAddr());
    return true;
}

void
Simple_Dispatcher::handleReqRetry() {
    
    assert(blocked[SMBlockType::SM2DRAM] || blocked[SMBlockType::SM2HBM] );
        
    if(blocked[SMBlockType::SM2DRAM]) 
    {
        DPRINTF(Simple_Dispatcher, "handleReqRetry,Request for dram blocked  \n");
        blocked[SMBlockType::SM2DRAM] = false;
        bus_side_port.trySendRetry();
    }
    if(blocked[SMBlockType::SM2HBM]) 
    {
        DPRINTF(Simple_Dispatcher, "handleReqRetry,Request for hbm blocked  \n");
        blocked[SMBlockType::SM2HBM] = false;
        bus_side_port.trySendRetry();
    }
}

void
Simple_Dispatcher::handleRespRetry() {
    // assert(dram_side_blocked || hbm_side_blocked);
    
    assert(blocked[SMBlockType::DRAM2SM] || blocked[SMBlockType::HBM2SM] );
    if(blocked[SMBlockType::DRAM2SM] )
    {
        DPRINTF(Simple_Dispatcher, "handle RespRetry,Response for dram blocked  \n");
        blocked[SMBlockType::DRAM2SM] = false;         
        dram_side_port.trySendRetry();
    }
    else if(blocked[SMBlockType::HBM2SM] )
    {
        DPRINTF(Simple_Dispatcher, "handle RespRetry,Response for hbm blocked  \n");
        blocked[SMBlockType::HBM2SM] = false;         
        hbm_side_port.trySendRetry();
    }
}


bool
Simple_Dispatcher::IsDramOrHbm(PacketPtr pkt) {
    uint64_t addr = pkt->getAddr();
    return addr < maxdramsize;//映射表存储在高地址
}

void Simple_Dispatcher::processEvent() {
    DPRINTF(Simple_Dispatcher, "EventProcess\n");
}

Simple_Dispatcher::BusSidePort::BusSidePort(const std::string& _name,
                                     Simple_Dispatcher& _simple_dispatch,
                                     Simple_Dispatcher::PortType _type)
    : ResponsePort(_name, &_simple_dispatch),
      simple_dispatcher(_simple_dispatch),
      porttype(_type),
      needRetry(false),
      hbm_blocked(false),
      dram_blocked(false) {}

AddrRangeList
Simple_Dispatcher::BusSidePort::getAddrRanges() const {
    return simple_dispatcher.getAddrRanges();
}

bool
Simple_Dispatcher::BusSidePort::sendPacket(PacketPtr pkt) {
    panic_if(dram_blocked && hbm_blocked, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingResp(pkt)) {
        if( simple_dispatcher.IsDramOrHbm(pkt) )
            dram_blocked = true;
        else
            hbm_blocked = true;
        return false;
    } else {
        return true;
    }
}

void
Simple_Dispatcher::BusSidePort::trySendRetry()
{
    if (needRetry && !(hbm_blocked && dram_blocked) ) {
        needRetry = false;
        DPRINTF(Simple_Dispatcher, "bus_side,Sending retry req for %d\n", id);
        sendRetryReq();
    }
}

Tick
Simple_Dispatcher::BusSidePort::recvAtomic(PacketPtr pkt) {
    return simple_dispatcher.handleAtomic(pkt);
}

void
Simple_Dispatcher::BusSidePort::recvFunctional(PacketPtr pkt) {
    simple_dispatcher.handleFunctional(pkt);
}

void
Simple_Dispatcher::BusSidePort::setReqPort(PacketPtr pkt) {
    pkt->reqport = Packet::PortType::SimpleDispatcher;
}

bool
Simple_Dispatcher::BusSidePort::recvTimingReq(PacketPtr pkt) {
    setReqPort(pkt);
    if (!simple_dispatcher.handleRequest(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

void
Simple_Dispatcher::BusSidePort::recvRespRetry() {
    assert(hbm_blocked || dram_blocked);
    DPRINTF(Simple_Dispatcher, "bus_side,recv Resp Retry for %d\n", id);
    if(hbm_blocked)
        hbm_blocked= false;
    else if(dram_blocked)
        dram_blocked= false;
    simple_dispatcher.handleRespRetry();
}

Simple_Dispatcher::MemSidePort::MemSidePort(const std::string& _name,
                                     Simple_Dispatcher& _simple_dispatch,
                                     Simple_Dispatcher::PortType _type)
    : RequestPort(_name, &_simple_dispatch),
      simple_dispatcher(_simple_dispatch),
      porttype(_type),
      needRetry(false),
      blocked(false) {}

bool
Simple_Dispatcher::MemSidePort::sendPacket(PacketPtr pkt) {
    panic_if(blocked, "Should never try to send if blocked!");
    if (!sendTimingReq(pkt)) {
        blocked = true;
        return false;
    } else {
        return true;
    }
}

void
Simple_Dispatcher::MemSidePort::trySendRetry() {
    if (needRetry && !blocked ) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(Simple_Dispatcher, "MemSidePort ,Sending retry req for %d\n", id);
        sendRetryResp();
    }
}

void
Simple_Dispatcher::MemSidePort::setRespPort(PacketPtr pkt) {
    if (porttype == Simple_Dispatcher::PortType::PhysicalDram) {
        pkt->respport = Packet::PortType::PhysicalDram;
    } else if (porttype == Simple_Dispatcher::PortType::PhysicalHbm) {
        pkt->respport = Packet::PortType::PhysicalHbm;
    }
}

bool
Simple_Dispatcher::MemSidePort::recvTimingResp(PacketPtr pkt) {
    // Just forward to the memobj.
    setRespPort(pkt);
    if (!simple_dispatcher.handleResponse(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

void
Simple_Dispatcher::MemSidePort::recvReqRetry() {
    assert(blocked);
    DPRINTF(Simple_Dispatcher, "MemSidePort,recv Req Retry for %d\n", id);
    blocked = false;
    simple_dispatcher.handleReqRetry();
}

void
Simple_Dispatcher::MemSidePort::recvRangeChange() {
    simple_dispatcher.sendRangeChange();
}
// overhead
Simple_Dispatcher::MemStats::MemStats(Simple_Dispatcher &_simple_dp)
    : statistics::Group(&_simple_dp), simple_dp(_simple_dp),
    ADD_STAT(numofdramHit, statistics::units::Count::get(), "Number of dram hit"),
    ADD_STAT(numofhbmHit, statistics::units::Count::get(), "Number of hbm hit"),
    ADD_STAT(sumofpacketslatency, statistics::units::Count::get(), "Sum of bus packets latency")
{
}

void
Simple_Dispatcher::MemStats::regStats()
{
    using namespace statistics;

    statistics::Group::regStats();

    System *sys = simple_dp.system();
    assert(sys);
    const auto max_requestors = sys->maxRequestors();

    numofdramHit
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numofdramHit.subname(i, sys->getRequestorName(i));
    }

    numofhbmHit
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numofhbmHit.subname(i, sys->getRequestorName(i));
    }

    sumofpacketslatency
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        sumofpacketslatency.subname(i, sys->getRequestorName(i));
    }
}

} // namespace memory
} // namespace gem5
// overhead