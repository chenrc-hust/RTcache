/**
 * @file
 * pageswaper.cc
// //by crc 240520
 */
#include "base/trace.hh"
#include "mem/pageswaper.hh"
#include "debug/PageSwaper.hh"


namespace gem5 {

namespace memory
{

// constructor of the mm
PageSwaper::PageSwaper(const PageSwaperParams &params)
    : SimObject(params),
      wc_side_port(name() + "wc_side_port", *this),
    //   rt_response_port(name() + ".rt_response_port", *this),
      rt_request_port(name() + ".rt_request_port", *this),
      dp_side_port(name() + ".dp_side_port", *this),
      max_queue_len(100),
      PageSize(4096),
      PSnumofcycles(0),
      blockpacket1(false),
      blockpacket2(false),
      blockpacket3(false),
      blockpacket4(false),
    //   blockpacket5(false),
    //   blockpacket6(false),
      PSblock(false),
      event([this]{processEvent();}, name()), 
      adjust_latency(1000000),
      swap_count(10),
      _system(NULL), // overhead
      stats(*this) {
           _wc = wearlevelcontrol();
      }

Port& 
PageSwaper::getPort(const std::string &if_name, PortID idx) {
    if (if_name == "wc_side_port") {
        return wc_side_port;
    } /* else if (if_name == "rt_response_port") {
        return rt_response_port;
    } */ else if (if_name == "rt_request_port") {
        return rt_request_port;
    } else if (if_name == "dp_side_port") {
        return dp_side_port;
    } else {
        return SimObject::getPort(if_name, idx);
    }
}

// handle the request from the wc, judge if we can save the pkt
// 接收 wc的包，判断待交换的队列是否已满
bool PageSwaper::handleWCRequest(PacketPtr pkt) {//只调用一次
    // 如果当前迁移队列已满
    if (swap_pkts_qe.size() == max_queue_len) {     // the queue if full
        DPRINTF(PageSwaper, "the swap queue if full, cannot handle the WC request\n");
        return false;
    } 
    //尝试交换选中的两个页面，首先加入队列
    if(pkt->pkttype == Packet::PacketType::PageSwap){
        // std::cout << "swap  from wc,size: " 
        //     << swap_pkts_qe.size() << ", page_1 addr: " 
        //     << pkt->page_1 << ",page_2 addr: " << pkt->page_2 << std::endl;
        swap_pkts_qe.emplace(pkt);
        stats.SwapFromWc[pkt->req->requestorId()]++;//统计收到的交换请求
    }

    stats.ReqFromWc[pkt->req->requestorId()]++;//统计从wc收到的所有请求
    swapPage();
    return true;
}

bool //接收到rt的响应后直接swap
PageSwaper::handleRTResponse(PacketPtr pkt) {//by crc 240522

    /* 返回的包可能有信息 */
    swapPage();
    return true;  // directly return true
}
//为什么一个要delete一个不用？
// Disp的单独处理逻辑,处理响应，删除包然后返回true即可
bool 
PageSwaper::handleDispResponse(PacketPtr pkt) {
//by crc 240528
    if(pkt != nullptr){
        if(pkt->needsResponse())pkt->makeResponse();
        delete pkt;
    }
    swap_count--;//by crc 240528 控制尝试次数

    if(swap_count==0){swapPage();swap_count=10;}
    return true;
}

void PageSwaper::swapPage() {
    DPRINTF(PageSwaper, "in PS page swap module swap_pkts_qe.size() = %d\n",swap_pkts_qe.size());
    while (!swap_pkts_qe.empty()) {
        PacketPtr pkt = swap_pkts_qe.front();
        if(pkt->pkttype == Packet::PacketType::PageSwap){
            if(!dramtodram(pkt))
                break;
        }else 
            DPRINTF(PageSwaper, "pkt->pkttype != Packet::PacketType::PageSwap\n");
        swap_pkts_qe.pop();
    }
}

bool
PageSwaper::dramtodram(PacketPtr pkt){//和ps没关系，dp没关系
    uint64_t page_1 = pkt->page_1;//待交换地址
    uint64_t page_2 = pkt->page_2;
    DPRINTF(PageSwaper, "try to swap dramtodram page_1 %#x page_2 %#x\n",page_1,page_2);
    unsigned dataSize = 256;//单次读写的大小，可以改为256B
    RequestPtr update_req = std::make_shared<Request>(pkt->getAddr(), dataSize, 256, 0);
    PacketPtr update_pkt = Packet::createRead(update_req);
    update_pkt->setPage_1(page_1);
    update_pkt->setPage_2(page_2);
    update_pkt->pkttype = Packet::PacketType::PageSwap;//把当前交换的两个页面发回rt修改映射关系
    if(!PSblock&&!rt_request_port.blocked())  //首先发第一个包，告知要开始更新了，同时确保上一次更新完成的包被rt接收
    { 
        DPRINTF(PageSwaper, "first time try send update pkt to rt\n");
        rt_request_port.sendPacket(update_pkt);//第一遍发送
        if(!rt_request_port.blocked())PSblock = true;//成功后阻塞，
        else return false;
    }
    // 如果发送成功了，
    while (PSnumofcycles < PageSize/dataSize && PSblock )//第一次更新发送成功
    {
        DPRINTF(PageSwaper, "PSnumofcycles = %d\n",PSnumofcycles);////by crc 240522
        //1.先把页面1中的数据读出来
        if(!blockpacket1){
            PacketPtr page_1_read_pkt;
            RequestPtr read_page_1_req = std::make_shared<Request>(page_1*PageSize, dataSize, 256, 0);
            page_1_read_pkt = Packet::createRead(read_page_1_req);
            page_1_read_pkt->allocate();
            // page_1_read_pkt->isDram = true;
            if(!dp_side_port.blocked()){
                dp_side_port.sendPacket(page_1_read_pkt);
                page_1_read_pkt->deleteData();
                blockpacket1 = true;
                DPRINTF(PageSwaper, "blockpacket1\n");
            }
            else break;
        }
        //2.读第二个页面中的数据
        if(!blockpacket3){
            PacketPtr page_2_read_pkt;
            RequestPtr read_page_2_req = std::make_shared<Request>(page_2*PageSize, dataSize, 256, 0);
            page_2_read_pkt = Packet::createRead(read_page_2_req);
            page_2_read_pkt->allocate();
            // page_2_read_pkt->isDram = true;
            //nvm_read_pkt->hascache = true;//hjy add 3.18
            if(!dp_side_port.blocked()){
                dp_side_port.sendPacket(page_2_read_pkt);
                page_2_read_pkt->deleteData();
                blockpacket3 = true;
                DPRINTF(PageSwaper, "blockpacket3\n");
            }
            else break;
        }
        //3.dram数据写到nvm上
        if(!blockpacket4){
            page_1_buffer = new uint8_t[4];//都是模拟，没有具体的数据
            RequestPtr write_page_2_req;
            PacketPtr page_2_write_pkt ;
            write_page_2_req = std::make_shared<Request>(page_2*PageSize, dataSize, 2, 0);
            page_2_write_pkt = Packet::createWrite(write_page_2_req);
            page_2_write_pkt->dataStatic<uint8_t>(page_1_buffer);//
            // page_2_write_pkt->isDram = true;//by crc 240520
            if(!dp_side_port.blocked()){
                dp_side_port.sendPacket(page_2_write_pkt);
                page_2_write_pkt->deleteData();
                blockpacket4 = true;
                delete page_1_buffer; 
                DPRINTF(PageSwaper, "blockpacket4\n");
            }
            else break;
        }
        //4.nvm数据写到dram上
        if(!blockpacket2){
            PacketPtr page_1_write_pkt;
            page_2_buffer = new uint8_t[4];
            RequestPtr write_page_1_req = std::make_shared<Request>(page_1*PageSize, dataSize, 2, 0);
            page_1_write_pkt = Packet::createWrite(write_page_1_req);
            page_1_write_pkt->dataStatic<uint8_t>(page_2_buffer);
            if(!dp_side_port.blocked()){
                dp_side_port.sendPacket(page_1_write_pkt);
                page_1_write_pkt->deleteData();
                blockpacket1 = false;
                blockpacket3 = false;
                blockpacket4 = false;
                // delete hbm_write_pkt;
                delete page_2_buffer;
                DPRINTF(PageSwaper, "blockpacket2\n");
            }
            else break;
        }
        PSnumofcycles++;
    }
    if(PSnumofcycles == PageSize/dataSize&&!rt_request_port.blocked()){// 发完之后告知rt已经迁移完毕,发送前检查rt_request_port可访问性
        rt_request_port.sendPacket(update_pkt);//更新完毕后发第二次
        /* 假如在这rt不能接收怎么办 */
        /* 请求包发到响应去了
         */
        if(!rt_request_port.blocked())//发送成功则转为响应
            update_pkt->makeResponse();
        else{    
            //失败返回
            return false; 
        }
        wc_side_port.sendPacket(update_pkt);//wc必能接收
        PSblock = false;
        PSnumofcycles = 0;
        // overhead 统计页迁移的次数。
        stats.numofSwap[pkt->req->requestorId()]++;
        // delete update_pkt;
        return true;
    }
    else{
        // delete update_pkt;
        return false;
    }

}

// }
// --------------------------- the deal function of every port------------------------------

// constructor,响应端
PageSwaper::WcSidePort::WcSidePort(const std::string& _name,
                                     PageSwaper& _pageswaper)
    : ResponsePort(_name),
      pageswaper(_pageswaper),
      needRetry(false),
      blockedPacket(nullptr)
      {}

// Just forward to the .
AddrRangeList
PageSwaper::WcSidePort::getAddrRanges() const
{
    return pageswaper.getAddrRanges();
}

void
PageSwaper::WcSidePort::recvFunctional(PacketPtr pkt)
{
    return pageswaper.handleFunctional(pkt);
}

// -----------key functions----------
//接收wc的时序访问请求
bool
PageSwaper::WcSidePort::recvTimingReq(PacketPtr pkt)
{
    // / 判断能否接收请求
    if (!pageswaper.handleWCRequest(pkt)) {     // if cannot, return false
        needRetry = true;       // and we set the needRetry flag, which means we should send retry request after
        return false;
    } // else, handle successfully
    return true;
}

void
PageSwaper::WcSidePort::trySendRetry()    // We will actively call this function，要求上级重发，
{
   if (needRetry && blockedPacket == nullptr)      // only if when the needRetry if true;   
    { 
        DPRINTF(PageSwaper, "Sending retry req  to WC\n");
        needRetry = false;
        sendRetryReq();         //
    }
}

// 当一次page完成时发送完成包到wc
void
PageSwaper::WcSidePort::sendPacket(PacketPtr pkt)  // （√）
{
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");//

    if (!sendTimingResp(pkt)) {
        blockedPacket = pkt;
    }
}

// 当wc接收响应失败时要求ps重发
void
PageSwaper::WcSidePort::recvRespRetry() 
{
    assert(blockedPacket != nullptr);

    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    sendPacket(pkt);

    trySendRetry();
}

Tick
PageSwaper::WcSidePort::recvAtomic(PacketPtr pkt) {
    return pageswaper.handleAtomic(pkt);
}


//    ---------------------------------------------- Request(Master) port-------------------
// constructor
PageSwaper::RtRequestPort::RtRequestPort(const std::string& _name,
                                     PageSwaper& _pageswaper)
    : RequestPort(_name),
      pageswaper(_pageswaper),
      needRetry(false),
      blockedPacket(nullptr) 
      {}

void
PageSwaper::RtRequestPort::sendPacket(PacketPtr pkt)
{
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");
    DPRINTF(PageSwaper, "PageSwaper::RtRequestPort::sendPacket try sendpacket  %s \n",__func__);
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
        DPRINTF(PageSwaper, "PageSwaper sendPacket fail for  RtRequestPort %s \n",__func__);
    }else{
        /*  发送成功之后呢？ */

        DPRINTF(PageSwaper, "PageSwaper sendPacket success to RtRequestPortfor %s \n",__func__);
    }
}

// when we receive the resp from RT, it means we block/update the RT successully
bool
PageSwaper::RtRequestPort::recvTimingResp(PacketPtr pkt)
{
    DPRINTF(PageSwaper, "recvTimingResp for RtRequestPort %s \n",__func__);
    return pageswaper.handleRTResponse(pkt);
}

void
PageSwaper::RtRequestPort::recvReqRetry() //
{
    DPRINTF(PageSwaper, "Sending retry req for %s \n",__func__);
    assert(blockedPacket != nullptr);

    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // sendPacket(pkt);
    pageswaper.swapPage();
}


void
PageSwaper::RtRequestPort::recvRangeChange()
{
    pageswaper.sendRangeChange();
}


// constructor
PageSwaper::DispatcherSidePort::DispatcherSidePort(const std::string& _name,
                                     PageSwaper& _pageswaper)
    : RequestPort(_name),
      pageswaper(_pageswaper),
      blockedPacket(nullptr) 
      {}

bool
PageSwaper::DispatcherSidePort::recvTimingResp(PacketPtr pkt)
{
    return pageswaper.handleDispResponse(pkt);
}

void
PageSwaper::DispatcherSidePort::sendPacket(PacketPtr pkt)
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
    panic_if(blockedPacket != nullptr, "ps,Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
        //发送失败也跳过？
        DPRINTF(PageSwaper, "sendTimingReq TO DP FAIL\n");
    }else DPRINTF(PageSwaper, "sendTimingReq TO DP sucess\n");
}

//这里调用了一次就卡死
void
PageSwaper::DispatcherSidePort::recvReqRetry()//一般不会调用
{

    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);

    // std::cout << "recvRetry" << std::endl;

}

void
PageSwaper::DispatcherSidePort::recvRangeChange()
{
    //owner->sendRangeChange();
    pageswaper.sendRangeChange();
}


// 需要master(request)类型的端口返回
AddrRangeList 
PageSwaper::getAddrRanges() const {
    DPRINTF(PageSwaper, "PageSwaper sending new ranges\n");
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
PageSwaper::sendRangeChange() {
    DPRINTF(PageSwaper, "send Range Change...\n");
    wc_side_port.sendRangeChange();
}


bool 
PageSwaper::IsDispatcher(PacketPtr pkt){
    if (pkt->reqport == Packet::PortType::Dispatcher) {     // if the pkt is from Dispatcher
        return true;
    }else/* else if (pkt->reqport == Packet::PortType::RemappingTable) */ {
        return false;
    }
}

Tick
PageSwaper::handleAtomic(PacketPtr pkt) {
    // need to judge which 
    if(IsDispatcher(pkt))
        return dp_side_port.sendAtomic(pkt);
    else{
        return rt_request_port.sendAtomic(pkt);
    }
}

void
PageSwaper::handleFunctional(PacketPtr pkt) { 
    if(IsDispatcher(pkt)) {
        dp_side_port.sendFunctional(pkt);
    } else {
        rt_request_port.sendFunctional(pkt);
    }
}
// overhead
PageSwaper::MemStats::MemStats(PageSwaper &_ps)
    : statistics::Group(&_ps), ps(_ps),
    ADD_STAT(SwapFromWc, statistics::units::Count::get(), "PS Number of packets from wc"),
    ADD_STAT(numofSwap, statistics::units::Count::get(), "PS Number of swap"),
    ADD_STAT(avPS, statistics::units::Rate<
                statistics::units::Count, statistics::units::Second>::get(),
             "Average PageSwaper number per tick")
{
}

void
PageSwaper::MemStats::regStats()
{
    using namespace statistics;

    statistics::Group::regStats();

    System *sys = ps.system();
    assert(sys);
    const auto max_requestors = sys->maxRequestors();

    SwapFromWc
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        SwapFromWc.subname(i, sys->getRequestorName(i));
    }

    ReqFromWc
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        ReqFromWc.subname(i, sys->getRequestorName(i));
    }

    numofSwap
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numofSwap.subname(i, sys->getRequestorName(i));
    }

    avPS
        .precision(0)
        .prereq(SwapFromWc)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        avPS.subname(i, sys->getRequestorName(i));
    }

    avPS = SwapFromWc / simSeconds;

}
void PageSwaper::startup(){
    // schedule(event, adjust_latency);
}

void PageSwaper::processEvent() {
    DPRINTF(PageSwaper, "PageSwaper EventProcess blockpacket1 %d blockpacket2 %d blockpacket3 %d blockpacket4 %d PSblock %d PSnumofcycles %d\n",
    blockpacket1,blockpacket2,blockpacket3,blockpacket4,PSblock,PSnumofcycles);
    // schedule(event, curTick() + adjust_latency);
}

} // namespace memory
} // namespace gem5
// overhead 