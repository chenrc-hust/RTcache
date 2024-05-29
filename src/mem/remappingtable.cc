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
      ps_side_port(name() + ".ps_side_port", *this),//接收ps的响应
      dp_side_port(name() + ".dp_side_port",*this),
    //   ps_request_port(name() + ".ps_request_port",*this),//
      event([this]{processEvent();}, name()), 
      adjust_latency(1000000),
      bus_side_blocked(false),
      ps_side_blocked(false),
      dp_side_blocked(false),
      ps_request_blocked(false),
      PageSize(4096),
      //缓存是缓存映射表表项
      MaxDRAMPage(params.dram_size/(4*1024)), 
      MaxHBMPage(params.hbm_size/(4*1024)),
      HBMsize(params.hbm_size), //hbm
      blocked_threshold(1024),
      isblockedpkt(false),
      mempkt(nullptr),
      _system(NULL), // overhead
      stats(*this)
      { 
          //缓存中的映射表 //by crc 240516
          remapcache = new RemapCache(HBMsize/(8*8),8);//高速缓存中的映射表 ，8个一组，单个大小为8B
          remapdram = new RemapDram(MaxDRAMPage);//整个内存对应的映射表
          _wc = wearlevelcontrol();
          _ps = pageswaper();
        std::cout << "rt params memory dram page size: " << MaxDRAMPage<<std::endl;
        std::cout << "rt params hbm_size pagesize: " << MaxHBMPage<<std::endl;
      }

Port& 
RemappingTable::getPort(const std::string &if_name, PortID idx) {
    if (if_name == "bus_side_port") {
        return bus_side_port;
    } else if (if_name == "ps_side_port") {
        return ps_side_port;
    } else if(if_name == "dp_side_port") {
        return dp_side_port;
    }/* else if(if_name == "ps_request_port"){
        return ps_request_port;
    } */else {
        return SimObject::getPort(if_name, idx);
    }
}

AddrRangeList
RemappingTable::getAddrRanges() const{//向下
    return dp_side_port.getAddrRanges();
}

void
RemappingTable::sendRangeChange() {//向上
    DPRINTF(RemappingTable, "send Range Change...\n");
    bus_side_port.sendRangeChange();
}

Tick
RemappingTable::handleAtomic(PacketPtr pkt){//
    return dp_side_port.sendAtomic(pkt);
}

void
RemappingTable::handleFunctional(PacketPtr pkt){
    return dp_side_port.sendFunctional(pkt);
}

//
//处理bus端口发送的req请求,能接收就返回true
bool 
RemappingTable::handleBusReq(PacketPtr pkt){ //by crc 240520
    //如果全局阻塞，不能接受bus往下传的req
    if(bus_side_blocked||dp_side_port.blocked()){
        DPRINTF(RemappingTable, "RemappingTable module Bus req blocked directly for addr %#x  isRead %d\n", pkt->getAddr(),pkt->isRead());
        return false;
    }
    // 统计读包总数
    if(pkt->isRead()){
        stats.sumofreadreq[pkt->req->requestorId()]++;
    }
    else{
        stats.sumofwritereq[pkt->req->requestorId()]++;
    }
    pkt->remapaddr = pkt->getAddr();//获取地址
    DPRINTF(RemappingTable, "RemappingTable module first access bus req for addr %#x  isRead %d \n", pkt->remapaddr,pkt->isRead());
    uint64_t nowpage = (pkt->remapaddr) / PageSize;//固定页面大小为4KB,起始偏移
    uint64_t nowoffset = (pkt->remapaddr) % PageSize;//偏移量
    //是否为3
    // std::cout<<" blocked_pagenums.size() "<<blocked_pagenums.size()<<" blocked_pkt.size() "<<blocked_pkt.size()<<" "<<pkt->isRead()<<" "<<std::hex<<pkt->remapaddr<<std::endl;//和这个无关，因为第一次的磨损均衡都没有结束
    
    if(blocked_pagenums.size() == 3 && (nowpage == blocked_pagenums[0] || nowpage == blocked_pagenums[1] || nowpage == blocked_pagenums[2])){//如果当前要访问的地址被阻塞，停止此次访问
        
        // std::cout<<blocked_pagenums[0]<<" "<<blocked_pagenums[1]<<" "<<blocked_pagenums[2]<<std::endl;
        //如果被阻塞将其加入阻塞队列
        blocked_pkt.push(pkt);
        //阻塞队列大小超过阈值则全局阻塞
        DPRINTF(RemappingTable, "RemappingTable bus req has block because swap page %#x\n", pkt->getAddr());
        if(blocked_pkt.size() > blocked_threshold){
            bus_side_blocked = true;
            return false;
        }
       
    }
    else{//不在交换中
        /* 第一步获取地址 */
        
        uint64_t accesspage;
        if(remapcache->haspage(nowpage)){//命中，应该有一个模拟命中的包?
            //在缓存中直接获取映射地址
            //命中时设置为访问hbm
            accesspage = MaxDRAMPage+rand()%MaxHBMPage;
            //发送到hbm中，映射表的读取属于关键路径，
            
            pkt->remapaddr = (remapcache->getremappage(nowpage)) * PageSize + nowoffset;
            stats.numofRemapHit[pkt->req->requestorId()]++;
        }
        else{

            //_mm->send_read_dram(pkt->remapaddr);
            accesspage = rand()%MaxDRAMPage;
            //remapaddr用来模拟映射
            pkt->remapaddr = (remapdram->getremappage(nowpage)) * PageSize + nowoffset;
            remapcache->put(nowpage,(pkt->remapaddr)/PageSize);//放入缓存中
            stats.numofRemapMiss[pkt->req->requestorId()]++;
        }
        //by crc 240516
        bus_side_blocked = true;//阻塞上一层，
        pkt->accesspage = accesspage;
        mempkt = pkt;
        if(pkt->remapaddr!=pkt->getAddr())
            DPRINTF(RemappingTable, "RemappingTable bus req has swaped pkt->getAddr() %#x pkt->remapaddr %#x \n", pkt->getAddr(),pkt->remapaddr);
        SendaRead(accesspage);//发送模拟包，比如下面正在处理页面交换，
        //
    }
    return true;
}

/*比如自己造的包，怎么回收*/
void 
RemappingTable::SendaRead(uint64_t page){//模拟一个包 ，设置为取指
    unsigned dataSize = 4;//在这里申请，在response回收
    if(!dp_side_port.blocked()){//
        DPRINTF(RemappingTable, "rt simulate in address %#x  to access %#x \n",page,mempkt->getAddr());
        
        RequestPtr read_req = std::make_shared<Request>(page*PageSize, dataSize, 256, 0);//256设置为预取指令
        //智能指针，在后面删除就可以了
        PacketPtr _read_pkt = Packet::createRead(read_req);/* 智能指针 */

        if(page>=MaxDRAMPage){
            _read_pkt ->isCache = true;
            DPRINTF(RemappingTable, "rt simulate to access hbm \n");
        
        }
        /* 设置为AccessRt*/
        _read_pkt ->pkttype = Packet::PacketType::AccessRt;
        dp_side_port.sendPacket(_read_pkt);
    }else{
         DPRINTF(RemappingTable, "rt simulate fatal %#x \n",page);
    }

}

//by crc 240515
//处理Ps端口发送的req请求,To be added and modified,update 请求
//如果还有未处理的包
//收到请求后阻塞membus，不再接收
bool
RemappingTable::handlePsReq(PacketPtr pkt){ //wanxx
    if(ps_side_blocked||bus_side_blocked){//控制能否接收ps发送的req包
        DPRINTF(RemappingTable, "RemappingTable module Ps block req blocked directly for addr %#x\n", pkt->remapaddr);
        return false;
    }
    
    if(blocked_pagenums.size() < 3){//小于3就是0，现在没有被阻塞的页
        //还要判断上一次阻塞造成的阻塞队列中的pkt是否全部处理完成，处理完毕后才可接受新的block请求
        if(blocked_pkt.size()==0){
            // blocked_pagenums.emplace_back((pkt->getHbmAddr()));
            // blocked_pagenums.emplace_back((pkt->getDramAddr()));
            if(pkt->pkttype == Packet::PacketType::PageSwap){
                DPRINTF(RemappingTable, "RemappingTable module recv  Ps update req first time  for addr %#x addr %#x\n", pkt->getPage_1(),pkt->getPage_2());
                blocked_pagenums.emplace_back(pkt->getPage_1());
                blocked_pagenums.emplace_back(pkt->getPage_2());
                blocked_pagenums.emplace_back(INT64_MAX);
                /* 这里是不是要安排一个包作为响应 */
                // pkt->makeResponse();
                // ps_side_port.sendPacket(pkt);
                return true;
            }
        }
        else{/* 没有处理完 */
            DPRINTF(RemappingTable, "last blockedqueue has not been completed fatal error");
            /* 是不是应该调用一下处理阻塞包的函数 */
            return false;
        }
    }
    else{//等于说明当前pkt是page swaper更新完成的标
        //否则如果当前发送的包与blocked_pagenums队尾的元素一样，说明是update请求，需要更新remap_table
        DPRINTF(RemappingTable, "RemappingTable module recv  Ps update req for addr %#x addr %#x\n", pkt->getPage_1(),pkt->getPage_2());
        updateRemap(pkt);//update,更新表
        //将之前被阻塞的pkt往下发
        handleBlockedPkt();//重新查表，查表开销要算吗
        //std::cout<<"handleBlockedPkt success!" << std::endl;
        // }
    }
    // }
    DPRINTF(RemappingTable, "RemappingTable::handlePsReq complete\n");
    return true;
}
/* 每次先响应req，处理完毕后响应resp */
//处理Dp端口发送的resp响应，携带数据转发给membus
//如果返回的包类型是accessRT，并且地址相同，
//就发送当前包
bool
RemappingTable::handleDpResponse(PacketPtr pkt){//成功了，发送AccessRt 包
    /* 一次处理一个请求  */
    assert(bus_side_blocked&&!dp_side_port.blocked());
    DPRINTF(RemappingTable, "RemappingTable module access handleDpResponse in addr %#x PacketType %d\n", pkt->getAddr(),pkt->pkttype);
    
    if(mempkt!=nullptr/* &&pkt->getAddr()/PageSize == mempkt->accesspage&&pkt->pkttype == Packet::PacketType::AccessRt */){
        delete pkt;//删除当前包，
        
        dp_side_port.sendPacket(mempkt);//向下发送，实际访问包，如果失败了，重发，
        if(mempkt->isWrite()){
            bus_side_blocked = false;//如果下发包是write包直接释放端口
            mempkt = nullptr;
            bus_side_port.trySendRetry();
            return true;
        }
        mempkt = nullptr;//
        
    }else{/* 向上发送实际上的数据响应包 */
        DPRINTF(RemappingTable, "RemappingTable module try access dp resp for addr %#x\n", pkt->getAddr());
        bus_side_port.sendPacket(pkt);//
        bus_side_blocked = false;
        ps_side_port.trySendRetry();//首先尝试接收更新包
        bus_side_port.trySendRetry();
        DPRINTF(RemappingTable, "RemappingTable module access dp resp for addr %#x complete \n", pkt->getAddr());
    }
    return true;   
}

//修改重映射表
void
RemappingTable::updateRemap(PacketPtr pkt){
    // pageswap
    if(pkt->pkttype == Packet::PacketType::PageSwap){
    // 需要加载的page1 page2
        uint64_t page_1 = pkt->getPage_1();
        uint64_t page_2 = pkt->getPage_2();
        //修改RT
        remapcache->update(page_1,page_2);
        remapcache->update(page_2,page_1);
        remapdram->update(page_1,page_2);
        remapdram->update(page_2,page_1);
        //测试
        //uint64_t remapdrampage = remapcache->getremappage(nvmpage);
        // std::cout << "old address: " <<page_1<< ", new address: " << page_2 << std::endl;

    }
    // overhead
    blocked_pagenums.clear();
    stats.numofRemapUpdate[pkt->req->requestorId()]++;
    // return true;
}
//
void
RemappingTable::handleBlockedPkt(){
    bus_side_blocked = true;//阻塞membus
    dp_side_blocked = true;//阻塞dp
    isblockedpkt = true;//处理被阻塞包
    DPRINTF(RemappingTable, "RemappingTable::handleBlockedPkt blocked_pkt.size %d\n",blocked_pkt.size());
    while(!blocked_pkt.empty()){//如果阻塞队列非空
        PacketPtr pkt = blocked_pkt.front();
        if(handleblockedqueue(pkt))
            blocked_pkt.pop();
        else
            return;//处理失败
    }
    isblockedpkt = false;
    dp_side_blocked = false;
    bus_side_blocked = false;
    //阻塞队列处理完成，打开
    //by crc 240515
    DPRINTF(RemappingTable, "RemappingTable::handleBlockedPkt complete\n");
    ps_side_port.trySendRetry();
    //by crc 240515  ps重发更新req包    
}
//remapaddr和原来的做 区分，处理因为访问正在更新的地址而阻塞的包，类似于handlebusreq
bool 
RemappingTable::handleblockedqueue(PacketPtr pkt){
    if(mempkt!=nullptr||dp_side_port.blocked())return false;
    pkt->remapaddr = pkt->getAddr();//获取地址
    DPRINTF(RemappingTable, "RemappingTable module access page swap blocked bus req for addr %#x\n", pkt->remapaddr);
    uint64_t nowpage = (pkt->remapaddr) / PageSize;//固定页面大小为4KB,起始偏移
    uint64_t nowoffset = (pkt->remapaddr) % PageSize;//偏移量
    //是否为3
    // std::cout<<blocked_pkt.size()<<" "<<pkt->isRead()<<" "<<std::hex<<pkt->remapaddr<<std::endl;//和这个无关，因为第一次的磨损均衡都没有结束
    /* 第一步获取地址 */
    uint64_t accesspage;
    if(remapcache->haspage(nowpage)){//命中，应该有一个模拟命中的包?
        //在缓存中直接获取映射地址
        //命中时设置为访问hbm
        accesspage = MaxDRAMPage+rand()%MaxHBMPage;
        //发送到hbm中，映射表的读取属于关键路径，
        pkt->remapaddr = (remapcache->getremappage(nowpage)) * PageSize + nowoffset;
        stats.numofRemapHit[pkt->req->requestorId()]++;
    }
    else{
        //_mm->send_read_dram(pkt->remapaddr);
        accesspage = rand()%MaxDRAMPage;
        //remapaddr用来模拟映射
        pkt->remapaddr = (remapdram->getremappage(nowpage)) * PageSize + nowoffset;
        remapcache->put(nowpage,(pkt->remapaddr)/PageSize);//放入缓存中
        stats.numofRemapMiss[pkt->req->requestorId()]++;
    }
    //by crc 240516
    bus_side_blocked = true;//阻塞上一层，
    pkt->accesspage = accesspage;
    mempkt = pkt;
    SendaRead(accesspage);//发送模拟包，比如下面正在处理页面交换，
    //     
    return true;
}


RemappingTable::BusSidePort::BusSidePort(const std::string& _name,
                                     RemappingTable& _remappingtable)
    : ResponsePort(_name),
      remappingtable(_remappingtable),
      needRetry(false),
    //   blocked(false),
      blockedPacket(nullptr)
      {}

AddrRangeList 
RemappingTable::BusSidePort::getAddrRanges() const{
    return remappingtable.getAddrRanges();
}

//通过bus port向上(bus)发送resp packet，然后这里的pkt来源是下面的dp发送的
void
RemappingTable::BusSidePort::sendPacket(PacketPtr pkt){
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingResp(pkt)) {
        DPRINTF(RemappingTable, "RemappingTable module send resp fail to bus for addr %#x\n", pkt->getAddr());
        blockedPacket = pkt;
    }else {
        DPRINTF(RemappingTable, "RemappingTable module send to bus for addr %#x\n", pkt->getAddr());
    }
}

//接受membus重发resp的请求,不需要管是在block还是正常访问
void 
RemappingTable::BusSidePort::recvRespRetry(){
    assert(blockedPacket != nullptr);
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    //尝试重新发送dp respon包,一定是数据包
    sendPacket(pkt);

    DPRINTF(RemappingTable, "RemappingTable module recv dp resp retry...\n");

    trySendRetry();
   
}
//向dp发送，
Tick 
RemappingTable::BusSidePort::recvAtomic(PacketPtr pkt){
    return remappingtable.handleAtomic(pkt);
}

void 
RemappingTable::BusSidePort::recvFunctional(PacketPtr pkt){
    return remappingtable.handleFunctional(pkt);
}

//接受上层bus发送的req请求，函数交给membus调用
bool 
RemappingTable::BusSidePort::recvTimingReq(PacketPtr pkt){
   
    if(!remappingtable.handleBusReq(pkt)){
        needRetry = true;
        return false;
    }else
        return true;
}

//当阻塞被打开后，需要告诉上层bus让它重新开始向下转发新的pkt 
void
RemappingTable::BusSidePort::trySendRetry(){
    if (needRetry && blockedPacket == nullptr) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(RemappingTable, "RemappingTable module sending Bus retry req for %d\n", id);
        sendRetryReq();
    }
}

/* response port */
RemappingTable::PsSidePort::PsSidePort(const std::string& _name,
                                     RemappingTable& _remappingtable)
    : ResponsePort(_name),
      remappingtable(_remappingtable),
      needRetry(false),
      blockedPacket(nullptr)
      {}

AddrRangeList 
RemappingTable::PsSidePort::getAddrRanges() const{
    return remappingtable.getAddrRanges();
}

void
RemappingTable::PsSidePort::sendPacket(PacketPtr pkt){
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");
    DPRINTF(RemappingTable, " try RemappingTable::PsSidePort::sendPacket  \n");
    if (!sendTimingResp(pkt)) {

        blockedPacket = pkt;
    } 
}
//当阻塞时告诉Ps让它重新发request包
void
RemappingTable::PsSidePort::trySendRetry(){
    if (needRetry && blockedPacket == nullptr) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(RemappingTable, "RemappingTable module sending Ps retry req for %d\n", id);
        sendRetryReq();//重新发更新包，response，让request重发req
    }else{
         DPRINTF(RemappingTable, "RT trySendRetry sending Ps retry req for %d failed because needRetry %d blockedPacket == nullptr %d  \n", id,needRetry,blockedPacket == nullptr);
    }
}

Tick 
RemappingTable::PsSidePort::recvAtomic(PacketPtr pkt){
    return remappingtable.handleAtomic(pkt);
}

void 
RemappingTable::PsSidePort::recvFunctional(PacketPtr pkt){
    return remappingtable.handleFunctional(pkt);
}

bool 
RemappingTable::PsSidePort::recvTimingReq(PacketPtr pkt){
    if(!remappingtable.handlePsReq(pkt)){//
        needRetry = true;
        DPRINTF(RemappingTable, "RemappingTable::PsSidePort::recvTimingReq fail %#x  \n",pkt->getAddr());
        return false;
    }
    DPRINTF(RemappingTable, "RemappingTable::PsSidePort::recvTimingReq complete  \n");
    return true;
}

//

//发给pageswaper的response,更新完毕后remapping如何回应
//把包继续发给dp，然后，修改枚举类型，对于更新类型的包过滤
void 
RemappingTable::PsSidePort::recvRespRetry(){

    assert(blockedPacket != nullptr);

    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    DPRINTF(RemappingTable, "RemappingTable module recv Ps resp retry...\n");

    sendPacket(pkt);

    trySendRetry();
}

RemappingTable::DpSidePort::DpSidePort(const std::string& _name,
                                     RemappingTable& _remappingtable)
    : RequestPort(_name),
      remappingtable(_remappingtable),
      needRetry(false),
      blockedPacket(nullptr)
      {}

//尝试通过dp port 下发数据包，
void 
RemappingTable::DpSidePort::sendPacket(PacketPtr pkt){
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {//dp先收到
        blockedPacket = pkt;
    } else {
        DPRINTF(RemappingTable, "RemappingTable module sending req to dp for addr %#x\n", pkt->getAddr());
    }
}

// 接受从下层dispatcher发送的resp
bool 
RemappingTable::DpSidePort::recvTimingResp(PacketPtr pkt){
    return remappingtable.handleDpResponse(pkt);
}

//该函数会告知remappingtable模块可以尝试重新往下转发req了，由dp调用，在dp处理完包之后，
//要求rt重发包
/* 面临问题，要发送是哪个包， */
void 
RemappingTable::DpSidePort::recvReqRetry(){
    // We should have a blocked packet if this function is called.
    
    assert(blockedPacket != nullptr);
    DPRINTF(RemappingTable, "RemappingTable module recvReqRetry  from dp \n");
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    
    sendPacket(pkt);
    if(remappingtable.isblockedpkt&&remappingtable.mempkt==nullptr){//正在处理阻塞包
        remappingtable.handleBlockedPkt();
    }
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
    // schedule(event, adjust_latency);
}

void RemappingTable::processEvent() {
    DPRINTF(RemappingTable, "RemappingTable EventProcess bus_side_blocked  %d "
    "ps_side_blocked %d  dp_side_blocked %d ps_request_blocked %d  isblockedpkt %d\n",
        bus_side_blocked,ps_side_blocked,dp_side_blocked,ps_request_blocked,isblockedpkt);
    // schedule(event, curTick() + adjust_latency);
}


} // namespace memory
} // namespace gem5