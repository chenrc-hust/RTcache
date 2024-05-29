/**
 * @file
 * AccessCounter.cc
 * Last modify Time:2022.3.17
 * Author:zpy
 */

#include "base/trace.hh"
#include "mem/wearlevelcontrol.hh"
#include "debug/WearLevelControl.hh"
//To be added 阈值调整部分

namespace gem5 {

namespace memory
{

WearLevelControl::WearLevelControl(const WearLevelControlParams &params)
    : SimObject(params),
      dp_side_port(name() + ".dp_side_port",*this),
      ps_side_port(name() + ".ps_side_port", *this),
      
      dp_side_blocked(false),
      ps_side_blocked(false),//
      event([this]{processEvent();}, name()),
      wearlevel_threshold(5000),
    // HBM-16M  DRAM-64G
      MaxDrampage(params.dram_size/(4*1024)),
    //   MaxNvmpage (params.nvm_size/(4*1024)),  
//by crc 240516
      adjust_latency(100000000),
      PageSize(4096),
      // overhead
      _system(NULL),
       stats(*this)
      {
          _dp = dispatcher();
          _rt = remappingtable();
          for(auto i = 0;i<MaxDrampage;i++)CounterMap.emplace_back(0);
      }

Port& 
WearLevelControl::getPort(const std::string &if_name, PortID idx) {
    if (if_name == "dp_side_port") {
        return dp_side_port;
    } 
    else if(if_name == "ps_side_port") {
        return ps_side_port;
    }
    else {
        return SimObject::getPort(if_name, idx);
    }
}
// 需要修改  //by crc 240515
AddrRangeList 
WearLevelControl::getAddrRanges() const{
    //by crc 240516
    return ps_side_port.getAddrRanges();
}

void 
WearLevelControl::sendRangeChange(){
    DPRINTF(WearLevelControl, "send Range Change...\n");
    dp_side_port.sendRangeChange();
}
//by crc 240516
Tick 
WearLevelControl::handleAtomic(PacketPtr pkt){
    return ps_side_port.sendAtomic(pkt);
}

void 
WearLevelControl::handleFunctional(PacketPtr pkt){
    return ps_side_port.sendFunctional(pkt);
}

//通过Dp端口接受并处理dp模块发送的req请求
/* 不需要返回false，因为只是记录访问 */
/*  */
bool 
WearLevelControl::handleDpRequest(PacketPtr pkt){
    //不会阻塞

    // DPRINTF(WearLevelControl, "WearLevelControl module access dp req for addr %#x\n", pkt->getAddr());

    //NowPage是啥 remapaddr是啥 应该是对应的真实页面和真实地址
    uint64_t NowPage = pkt->remapaddr / PageSize;//pkt访问的地址
    //统计时以页面为粒度，
    //访问dram，并且是写    
    if(pkt->isWrite()){
        // if(MaxDrampage>=NowPage)std::cout<<NowPage<<std::endl;
        CounterMap[NowPage]++;//不清零当作寿命
        stats.pagelife[NowPage]++;
        write_count++;
        if(write_count>=wearlevel_threshold){
            /*  */
            auto max_ = max_element(CounterMap.begin(),CounterMap.end()) - CounterMap.begin() ;
            auto min_ = min_element(CounterMap.begin(),CounterMap.end()) - CounterMap.begin() ;
            // for(auto it = 0 ;it<CounterMap.size();it++)if(CounterMap[it])std::cout<<"CounterMap  "<<CounterMap[it]<<std::endl;
            // std::cout<<"max_ "<<CounterMap[max_]<<" min_ "<<CounterMap[min_]<<std::endl;
            DPRINTF(WearLevelControl, "WearLevelControl module select max_  %#x  time  %d min_  %#x time %d \n"
                ,max_,CounterMap[max_],min_,CounterMap[min_]);
            pkt->pkttype = Packet::PacketType::PageSwap;//标记这个包触发交换
            pkt->setPage_1(max_);
            pkt->setPage_2(min_);
            ///* 问题处 */
            if( max_ == min_ )write_count = 0;
            else if(!ps_side_blocked){//没有进行中的迁移
                ps_side_blocked = true;
                write_count = 0;
                sendPsReq(pkt);
            }else{
                 DPRINTF(WearLevelControl, "WearLevelControl module send failed ,page swaper not complete %#x\n", pkt->getAddr());
            }
        }
    }//迁移带来的开销
    return true;
}


/*后续可以考虑在接收到上次页面交换完成后主动发起一次磨损均衡 */
bool 
WearLevelControl::handlePsResponse(PacketPtr pkt){
    assert(ps_side_blocked);

    DPRINTF(WearLevelControl, "WearLevelControl receive  Ps pageswape complete %#x\n", pkt->getAddr());
    
    ps_side_blocked = false;//允许后续磨损均衡
    
    if(pkt!=nullptr)
        delete pkt;

    return true;
}

// //by crc 240528
void //发送更新包
WearLevelControl::sendPsReq(PacketPtr pkt){
    if (pkt->needsResponse()) {
        DPRINTF(WearLevelControl, "WearLevelControl::sendPsReq Packet 0x%x: response",pkt->getAddr());
        pkt->makeResponse();
    }
    uint64_t page_1 = pkt->page_1;//待交换地址
    uint64_t page_2 = pkt->page_2;
    unsigned dataSize = 4;
    //交给ps应该是减少阻塞
    PacketPtr _read_pkt;
    RequestPtr read_req = std::make_shared<Request>(page_2*PageSize, dataSize, 256, 0);//256设置为预取指令

    _read_pkt = Packet::createRead(read_req);
    _read_pkt->allocate();
    _read_pkt->pkttype = Packet::PacketType::PageSwap;
    _read_pkt->setPage_1(page_1);
    _read_pkt->setPage_2(page_2);
    ps_side_port.sendPacket(_read_pkt);

}


// void WearLevelControl::processEvent() {
//     DPRINTF(AccessCounter, "EventProcess\n");  
//     /* 
//         * 每隔adjust_latency查看HBM容量是否达到设定值
//      */
//     // if(curTick()!=0 && curTick() % adjust_latency == 0){
//     //     controlHBMVolume();
//     // }


//     // // 每隔adjust_latency更新一次迁移阈值
//     // // 阈值调整
//     // if(curTick()!=0 && curTick() % adjust_latency == 0){
//     //     adjustMmThre();
//     //     std::cout<< "adjustMmThre done" << std::endl;
//     // }

//     // adjustMmThre();
//     //std::cout<< "migration_threshold: " << migration_threshold << std::endl;
//     schedule(event, curTick() + adjust_latency);
    
// }

WearLevelControl::DpSidePort::DpSidePort(const std::string& _name,
                                     WearLevelControl& _wearlevelcontrol)
    : ResponsePort(_name),
      wearlevelcontrol(_wearlevelcontrol),
      needRetry(false),
      blocked(false)
      {}

AddrRangeList 
WearLevelControl::DpSidePort::getAddrRanges() const{
    return wearlevelcontrol.getAddrRanges();
}
/* dp不需要响应 */
bool 
WearLevelControl::DpSidePort::sendPacket(PacketPtr pkt){
    panic_if(blocked, "Should never try to send if blocked!");

    return true;
}

//ps_side_block已经打开，dp_side_port可以重新发req了
void 
WearLevelControl::DpSidePort::trySendRetry(){
    // if (needRetry && !blocked) {
    //     // Only send a retry if the port is now completely free
    //     needRetry = false;
    //     DPRINTF(WearLevelControl, "WearLevelControl module sending Dp retry req ");
    //     sendRetryReq();
    // }
}

Tick 
WearLevelControl::DpSidePort::recvAtomic(PacketPtr pkt){
    return wearlevelcontrol.handleAtomic(pkt);
}

void 
WearLevelControl::DpSidePort::recvFunctional(PacketPtr pkt){
    wearlevelcontrol.handleFunctional(pkt);
}
//收到dp发来的req请求
bool 
WearLevelControl::DpSidePort::recvTimingReq(PacketPtr pkt){

    return wearlevelcontrol.handleDpRequest(pkt);
}
//dp要求重发resp，一次接收不需要重发
void 
WearLevelControl::DpSidePort::recvRespRetry(){
    // assert(blocked);
    // blocked = false;
    // // Try to resend it. It's possible that it fails again.
    // //
    // wearlevelcontrol.handlePsRespRetry();
}

WearLevelControl::PsSidePort::PsSidePort(const std::string& _name,
                                     WearLevelControl& _wearlevelcontrol)
    : RequestPort(_name),
      wearlevelcontrol(_wearlevelcontrol),
      blockedPacket(nullptr)
      {}

void 
WearLevelControl::PsSidePort::sendPacket(PacketPtr pkt){
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");
    // If we can't send the packet across the port, store it for later.

    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
        DPRINTF(WearLevelControl, "WearLevelControl module sendTimingReq Ps block  %#x\n", pkt->getAddr());
    }
}

// wc接收ps的resp来开启下一次的pageswap
bool 
WearLevelControl::PsSidePort::recvTimingResp(PacketPtr pkt){
    return wearlevelcontrol.handlePsResponse(pkt);
}

// 因为没有成功向ps模块发送page swap ，需要重新处理
void 
WearLevelControl::PsSidePort::recvReqRetry(){
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    sendPacket(pkt);
}

void 
WearLevelControl::PsSidePort::recvRangeChange(){
    wearlevelcontrol.sendRangeChange();
}

// overhead
WearLevelControl::MemStats::MemStats(WearLevelControl &_wc)
    : statistics::Group(&_wc), wc(_wc),
    ADD_STAT(pagelife, statistics::units::Count::get(), "pagelife")
{
}

void
WearLevelControl::MemStats::regStats()
{
    using namespace statistics;

    statistics::Group::regStats();

    System *sys = wc.system();
    
    assert(sys);
    const auto max_requestors = sys->maxRequestors();

    pagelife
        .init(wc.MaxDrampage)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < wc.MaxDrampage; i++) {
        pagelife.subname(i, csprintf("pagenum-%i", i));
    }

}
void WearLevelControl::startup(){
    // schedule(event, adjust_latency);
}

void WearLevelControl::processEvent() {
    DPRINTF(WearLevelControl, "WearLevelControl EventProcess\n");
    // schedule(event, curTick() + adjust_latency);
}

} // namespace memory
} // namespace gem5
// overhead