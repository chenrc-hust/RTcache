
#include "mem/simple_mem.hh"

#include "base/random.hh"
#include "base/trace.hh"
#include "debug/Drain.hh"
#include "sim/sim_exit.hh"
#include "mem/mycache.hh"


//剩余部分代码
int allwritecount=0;

namespace gem5
{

namespace memory
{

SimpleMemory::SimpleMemory(const SimpleMemoryParams &p) :
    AbstractMemory(p),
    port(name() + ".port", *this), latency(p.latency),cache_latency(p.cache_latency),
    latency_var(p.latency_var),bandwidth(p.bandwidth), isBusy(false),
    retryReq(false), retryResp(false),
    releaseEvent([this]{ release(); }, name()),
    dequeueEvent([this]{ dequeue(); }, name()),
    CACHE1_SIZE_1(p.CACHE1_SIZE_1),
    BUFFER_SIZE_1(p.BUFFER_SIZE_1),
    bool_cache(p.bool_cache),
    what_twl(p.what_twl),
    bool_fpag_mem(p.bool_fpag_mem),
    FPGA_read_PCM(p.FPGA_read_PCM),FPGA_write_PCM(p.FPGA_write_PCM),myclimberobj(1048576),mytwlobj(1048576),mm(),
    stats(this)
{
    for(int i=0;i<1048576;i++)
    {
        pageWriteCounts[i]=0;
        pageRemap.push_back(i);//把地址初始化为自身
    }    
    
}

void SimpleMemory::printAddrRanges() const {
    // 调用 AbstractMemory 类的 getAddrRange 函数
    AddrRange range = getAddrRange();
    std::cout << "Memory range: ["
              << std::hex << "0x" << range.start() // 确保使用start() 和 end()
              << ", 0x" << range.end() << "]" << std::dec << std::endl;
}


void
SimpleMemory::init()
{
    mm.BUFFER_SIZE=BUFFER_SIZE_1;
    mm.CACHE1_SIZE=CACHE1_SIZE_1;
    
    mm.init();
    mm.findfootprint();
    printf("%lld %lld %d %d %d %ld %ld\n",mm.BUFFER_SIZE,mm.CACHE1_SIZE,what_twl,bool_fpag_mem,bool_cache,FPGA_write_PCM,FPGA_read_PCM);
    
    AbstractMemory::init();
    
    // allow unconnected memories as this is used in several ruby
    // systems at the moment
    if (port.isConnected()) {
        port.sendRangeChange();
    }
    // 打印地址范围
    printAddrRanges();

}

Tick
SimpleMemory::recvAtomic(PacketPtr pkt)
{
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    access(pkt);
    return getLatency();
}

Tick
SimpleMemory::recvAtomicBackdoor(PacketPtr pkt, MemBackdoorPtr &_backdoor)
{
    Tick latency = recvAtomic(pkt);
    getBackdoor(_backdoor);
    return latency;
}

void
SimpleMemory::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());

    functionalAccess(pkt);

    bool done = false;
    auto p = packetQueue.begin();
    // potentially update the packets in our packet queue as well
    while (!done && p != packetQueue.end()) {
        done = pkt->trySatisfyFunctional(p->pkt);
        ++p;
    }

    pkt->popLabel();
}

void
SimpleMemory::recvMemBackdoorReq(const MemBackdoorReq &req,
        MemBackdoorPtr &_backdoor)
{
    getBackdoor(_backdoor);
}
Addr
SimpleMemory::queryfront(Addr addr_)
{
    // 查找映射后地址对应的前地址
    for(Addr i = 0; i<(1048576);i++){
        if(pageRemap[i]==addr_)
            return i;
    }
}
bool
SimpleMemory::recvTimingReq(PacketPtr pkt)
{
    //cerr<<"no error 11 there!!!!"<<endl;
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");
  
    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see read and writes at memory controller, "
             "saw %s to %#llx\n", pkt->cmdString(), pkt->getAddr());
    
    // we should not get a new request after committing to retry the
    // current one, but unfortunately the CPU violates this rule, so
    // simply ignore it for now
    if (retryReq)
        return false;

    // if we are busy with a read or write, remember that we have to
    // retry
    if (isBusy) {
        retryReq = true;
        //cerr<<"isBusy"<<endl;
        return false;
    }

    bool pkt_read = pkt->isRead();//读写判断
    Addr pkt_addr = pkt->getAddr();//获取原始地址
    uint64_t* returnValue;
    Addr remapping_index;//映射后地址
    Addr page_index = pkt_addr >>12; //原来的页号
    //Addr llc_index = pkt_addr >>8 ; //LLC号
    //页面超出范围时报错
    panic_if(page_index>=1048576,
             "Should only smaller than 1048576 , "
             " %#llx\n", pkt->getAddr());

    //总读写操作次数
    stats.total_num++;
    // technically the packet only reaches us after the header delay,
    // and since this is a memory controller we also need to
    // deserialise the payload before performing any write operation
    Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;
    //cerr<<"no error the111re!!!!"<<endl;
    // update the release time according to the bandwidth limit, and
    // do so with respect to the time it takes to finish this request
    // rather than long term as it is the short term data rate that is
    // limited for any real memory

    // calculate an appropriate tick to release to not exceed
    // the bandwidth limit
    Tick duration = pkt->getSize() * bandwidth;

    Tick FPGA_MEN = bool_fpag_mem ? FPGA_read_PCM : 0;
    
    bool hit_flag=false;//缓存相关
    /* 最后读写数据的延迟 */
    if(pkt_read) duration+= receive_delay  + FPGA_read_PCM ;
    else {
        duration+= receive_delay + FPGA_write_PCM ;
    }
    /* 访问rtcache延迟+访问映射表延迟+访问数据延迟+磨损均衡延迟 */
    if(bool_cache){//有rtcache
        //cerr<<"error in readInputFile"<<endl;
        panic_if(pkt_addr >= 1048576LL * 1024 * 4,
                    "Should only smaller than 1048576 , "
                    " %ld\n", remapping_index);
        //cerr<<pkt_addr<<endl;
        hit_flag=mm.readInputFile(pkt_addr,pkt_read);//返回的是页号
        //cerr<<"No error in readInputFile"<<endl;
        remapping_index=mm.queryRemap(page_index);//有rtcache时
        cerr<<remapping_index<<endl;
        if(hit_flag){//命中时去cache读即可
            stats.hits++;
            duration += getLatency_cache() ;
        }else{//未命中时FPGA_read_PCM
            stats.misses++;  
            duration += (FPGA_MEN == 0 ) ? 0 : FPGA_read_PCM;
        }

        /* 统计磨损均衡信息 */
        if(what_twl==0){//无磨损均衡

        }else if(what_twl==3){
            //达到交换门槛
            if(allwritecount>=128*16){
            auto min_it = pageWriteCounts.begin();//
            auto max_it = pageWriteCounts.begin();

            // 遍历map来找到具有最小和最大写入次数的页面
            for (auto it = pageWriteCounts.begin(); it != pageWriteCounts.end(); ++it) {
                if (it->second < min_it->second) {
                    min_it = it;
                }
                if (it->second > max_it->second) {
                    max_it = it;
                }
            }
            //这里的min_it 和 max_it都是物理地址
            /* 更新 pageWriteCounts*/
            min_it->first+=16;
            max_it->first+=16;
            //更新统计表
            stats.pagewrite[min_it->first]+=16;stats.pagewrite[max_it->first]+=16;
            Addr temp_1 = mm.queryfront(min_it->first);
            Addr temp_2 = mm.queryfront(max_it->first);
            mm.swapMapping(temp_1,temp_2);
            stats.total_num_swap1++;
            allwritecount=0;
            duration += receive_delay*512 + FPGA_read_PCM*2+ FPGA_write_PCM*2 + FPGA_read_PCM*256+FPGA_write_PCM*256;
            }
        }else{//选择twl 磨损均衡
            /* 返回的地址是待交换的地址，还是*/
            if(what_twl==1)returnValue = mytwlobj.access2(page_index,pkt_read);//判断交换情况，传入
            else returnValue = myclimberobj.access2(page_index,pkt_read);//判断交换情况
            if(returnValue[0] == 0){}
            else if(returnValue[0]==1){
                stats.total_num_swap1++;
                //printf("%ld  %ld =1\n",returnValue[1],returnValue[2]);
                cerr<<returnValue[1]<<" "<<returnValue[2]<<" "<<page_index<<endl;
                //cerr<<"error -1"<<endl;
                mm.swapMapping(returnValue[1],returnValue[2]);//
                Addr temp_1 = mm.queryRemap(returnValue[1]);
                Addr temp_2 = mm.queryRemap(returnValue[2]);
                //地址没有问题，
                //统计页面的写入次数
                stats.pagewrite[temp_1]+=16;stats.pagewrite[temp_2]+=16;
                //cerr<<"error 1"<<endl;       
                duration += receive_delay*512 + FPGA_read_PCM*2 +FPGA_write_PCM*2+ FPGA_read_PCM*256+FPGA_write_PCM*256;
            }else if(returnValue[0]==2){
                stats.total_num_swap2++;
                //cerr<<returnValue[1]<<" "<<returnValue[2]<<" "<<returnValue[3]<<endl;
                mm.swapMapping(returnValue[1],returnValue[2]);
                mm.swapMapping(returnValue[2],returnValue[3]);
                Addr temp_1 = mm.queryRemap(returnValue[1]);
                Addr temp_2 = mm.queryRemap(returnValue[2]);
                Addr temp_3 = mm.queryRemap(returnValue[3]);                
                //统计页面1的写入次数
                stats.pagewrite[temp_1]+=16;stats.pagewrite[temp_2]+=32;
                stats.pagewrite[temp_3]+=16;
                duration += receive_delay*768 + FPGA_read_PCM*3+FPGA_write_PCM*3+ FPGA_read_PCM*384+FPGA_write_PCM*384;
            }else{
                printf("%ld\n",returnValue[0]);
            }
        }
    }else{//访问映射表延迟+访问数据延迟+磨损均衡延迟    
        duration += (FPGA_MEN == 0 ) ? 0 : FPGA_read_PCM;//没有rtcache的情况下都视为未命中
        remapping_index=pageRemap[page_index];//没有rtcache时的映射表
        /* 统计磨损均衡信息 */
        if(what_twl==0){//无磨损均衡
        
        }else if(what_twl==3){
            //达到交换门槛
            if(allwritecount>=128*16){
            auto min_it = pageWriteCounts.begin();//
            auto max_it = pageWriteCounts.begin();

            // 遍历map来找到具有最小和最大写入次数的页面
            for (auto it = pageWriteCounts.begin(); it != pageWriteCounts.end(); ++it) {
                if (it->second < min_it->second) {
                    min_it = it;
                }
                if (it->second > max_it->second) {
                    max_it = it;
                }
            }
            //这里的min_it 和 max_it都是物理地址
            /* 更新 pageWriteCounts*/
            min_it->first+=16;
            max_it->first+=16;
            //更新统计表
            stats.pagewrite[min_it->first]+=16;stats.pagewrite[max_it->first]+=16;
            Addr temp_1 = queryfront(min_it->first);
            Addr temp_2 = queryfront(max_it->first);
            mm.swapMapping(temp_1,temp_2);
            stats.total_num_swap1++;
            allwritecount=0;
            duration += receive_delay*512 + FPGA_read_PCM*2+ FPGA_write_PCM*2 + FPGA_read_PCM*256+FPGA_write_PCM*256;
            }
        }else{//选择twl 磨损均衡
            /* 返回的地址是待交换的地址，还是*/
            if(what_twl==1)returnValue = mytwlobj.access2(page_index,pkt_read);//判断交换情况，传入
            else returnValue = myclimberobj.access2(page_index,pkt_read);//判断交换情况
            
            if(returnValue[0] == 0){}
            else if(returnValue[0]==1){
                stats.total_num_swap1++;
                //printf("%ld  %ld =1\n",returnValue[1],returnValue[2]);
                cerr<<returnValue[1]<<" "<<returnValue[2]<<" "<<page_index<<endl;
                //cerr<<"error -1"<<endl;
                mm.swapMapping(returnValue[1],returnValue[2]);//
                Addr temp_1 = pageRemap[returnValue[1]];
                Addr temp_2 = pageRemap[returnValue[2]];
                //地址没有问题，
                //统计页面的写入次数
                stats.pagewrite[temp_1]+=16;stats.pagewrite[temp_2]+=16;
                //cerr<<"error 1"<<endl;       
                duration += receive_delay*512 + FPGA_read_PCM*2 +FPGA_write_PCM*2+ FPGA_read_PCM*256+FPGA_write_PCM*256;
            }else if(returnValue[0]==2){
                stats.total_num_swap2++;
                //cerr<<returnValue[1]<<" "<<returnValue[2]<<" "<<returnValue[3]<<endl;
                mm.swapMapping(returnValue[1],returnValue[2]);
                mm.swapMapping(returnValue[2],returnValue[3]);
                Addr temp_1 = pageRemap[returnValue[1]];
                Addr temp_2 = pageRemap[returnValue[2]];             
                Addr temp_3 = pageRemap[returnValue[3]];         
                //统计页面1的写入次数
                stats.pagewrite[temp_1]+=16;stats.pagewrite[temp_2]+=32;
                stats.pagewrite[temp_3]+=16;
                duration += receive_delay*768 + FPGA_read_PCM*3+FPGA_write_PCM*3+ FPGA_read_PCM*384+FPGA_write_PCM*384;
            }else{
                printf("%ld\n",returnValue[0]);
            }
        }
    }    
    /* 统计读写 */
    if(!pkt_read){
        panic_if(remapping_index>=1048576,
            "Should only smaller than 1048576 , "
            " %ld\n", remapping_index);
        assert(remapping_index<1048576);
        stats.pagewrite[remapping_index]++;
    }       
    

           
    //getLatency()==73.3ns ddr4
    //Tick FPGA_MEN=0; //映射表访问时间
 
    
    if(what_twl==0){//无磨损均衡
        if(pkt_read) duration+= receive_delay  + FPGA_read_PCM ;
        else {
            duration+= receive_delay + FPGA_write_PCM ;
        }        
        if(bool_cache){
            if(hit_flag)duration += getLatency_cache() ;
            else duration += (FPGA_MEN == 0 ) ? 0 : FPGA_read_PCM;
            
            if(!pkt_read){
                panic_if(remapping_index>=1048576,
                    "Should only smaller than 1048576 , "
                    " %ld\n", remapping_index);
                assert(remapping_index<1048576);

                stats.pagewrite[remapping_index]++;
            }
            //printf("%ld\n",pkt_read);
        }else{
            duration += (FPGA_MEN == 0 ) ? 0 : FPGA_read_PCM;
            if(!pkt_read){//record when write 
                stats.pagewrite[remapping_index]++;
            }
        } 
    }else if(what_twl==1){//选择twl 磨损均衡
        //printf("%ld\n",11);
        assert(page_index<1048576);
        if(pkt_read) duration+= receive_delay  + FPGA_read_PCM ;
        else duration+= receive_delay + FPGA_write_PCM ;  
        //printf("%ld %ld\n",pkt->isRead(),pkt->isWrite());

        if(bool_cache){
            if(hit_flag)duration += getLatency_cache() ;
            else duration += (FPGA_MEN == 0 ) ? 0 : FPGA_read_PCM;
            //printf("%ld\n",pkt_read);
        }else duration += (FPGA_MEN == 0 ) ? 0 : FPGA_read_PCM;

        if(!pkt_read){
            assert(remapping_index<1048576);
            stats.pagewrite[remapping_index]++;
        }

        returnValue = mytwlobj.access2(page_index,pkt_read);//判断交换情况

        if(returnValue[0] == 0) {}
        else if(returnValue[0]==1) {
            stats.total_num_swap1++;
            //printf("%ld  %ld =1\n",returnValue[1],returnValue[2]);
            cerr<<returnValue[1]<<" "<<returnValue[2]<<" "<<page_index<<endl;
            //cerr<<"error -1"<<endl;
            mm.swapMapping(returnValue[1],returnValue[2]);
            //mm.swapMapping(1,2);  
            //cerr<<"error 0"<<endl;
            panic_if(returnValue[1]>=1048576||returnValue[2]>=1048576,
                "Should only smaller than 1048576 , "
                " %ld %ld\n",returnValue[1],returnValue[2]);
            //地址没有问题，   

            //统计页面的写入次数
            stats.pagewrite[returnValue[1]]+=16;stats.pagewrite[returnValue[2]]+=16;
            //cerr<<"error 1"<<endl;       
            duration += receive_delay*512 + FPGA_read_PCM*2 +FPGA_write_PCM*2+ FPGA_read_PCM*256+FPGA_write_PCM*256;
        }else if(returnValue[0]==2){
            stats.total_num_swap2++;

            //cerr<<returnValue[1]<<" "<<returnValue[2]<<" "<<returnValue[3]<<endl;
            panic_if(returnValue[1]>=1048576||returnValue[2]>=1048576||returnValue[3]>=1048576,
                "Should only smaller than 1048576 , "
                " %ld  %ld  %ld\n",returnValue[1],returnValue[2],returnValue[3]);  
            mm.swapMapping(returnValue[1],returnValue[2]);
            mm.swapMapping(returnValue[2],returnValue[3]);
            //统计页面1的写入次数
            stats.pagewrite[returnValue[1]]+=16;stats.pagewrite[returnValue[2]]+=32;
            stats.pagewrite[returnValue[3]]+=16;
         
            duration += receive_delay*768 + FPGA_read_PCM*3+FPGA_write_PCM*3+ FPGA_read_PCM*384+FPGA_write_PCM*384;
        }else{
            printf("%ld\n",returnValue[0]);
        }

    }else if(what_twl==3){
        if (!pkt_read) {   
            allwritecount++;
        }
        //基础延迟

        if(pkt_read) duration+= receive_delay  + FPGA_read_PCM ;
        else duration+= receive_delay + FPGA_write_PCM ;  
        //根据cache判断附加延迟
        if(bool_cache){
            if(hit_flag)duration += getLatency_cache() ;
            else duration += (FPGA_MEN == 0 ) ? 0 : FPGA_read_PCM;
        }else duration += (FPGA_MEN == 0 ) ? 0 : FPGA_read_PCM;

        if(!pkt_read){
                stats.pagewrite[remapping_index]++;
                pageWriteCounts[remapping_index]++;
                cerr<<remapping_index<<endl;
        }
        //达到交换门槛
        if(allwritecount>=128*16){
        auto min_it = pageWriteCounts.begin();
        auto max_it = pageWriteCounts.begin();

        // 遍历map来找到具有最小和最大写入次数的页面
        for (auto it = pageWriteCounts.begin(); it != pageWriteCounts.end(); ++it) {
            if (it->second < min_it->second) {
                min_it = it;
            }
            if (it->second > max_it->second) {
                max_it = it;
            }
        }
        //统计页面1的写入次数
        stats.pagewrite[min_it->second]+=16;stats.pagewrite[max_it->second]+=16;
        mm.swapMapping(min_it->second,max_it->second);
        allwritecount=0;
        duration += receive_delay*512 + FPGA_read_PCM*2+ FPGA_write_PCM*2 + FPGA_read_PCM*256+FPGA_write_PCM*256;
        }
    }//twl==2shi
    else{
        //printf("%ld\n",11);
        assert(page_index<1048576);
        if(pkt_read) duration+= receive_delay  + FPGA_read_PCM ;
        else duration+= receive_delay + FPGA_write_PCM ;  

        if(bool_cache){
            if(hit_flag)duration += getLatency_cache() ;
            else duration += (FPGA_MEN == 0 ) ? 0 : FPGA_read_PCM;      
            //printf("%ld\n",pkt_read);
        }else duration += (FPGA_MEN == 0 ) ? 0 : FPGA_read_PCM;

        if(!pkt_read){
                assert(remapping_index<1048576);
                stats.pagewrite[remapping_index]++;
            }
        if(page_index>=1048576)printf("%ld\n",page_index);
        returnValue = myclimberobj.access2(page_index,pkt_read);//判断交换情况

        if(returnValue[0] == 0) {}
        else if(returnValue[0]==1) {
            stats.total_num_swap1++;
            mm.swapMapping(returnValue[1],returnValue[2]);
            panic_if(returnValue[1]>=1048576,
                "Should only smaller than 1048576 , "
                " %ld\n",returnValue[1]);            
            assert(returnValue[1]<1048576);
                //统计页面的写入次数
            stats.pagewrite[returnValue[1]]+=16;stats.pagewrite[returnValue[2]]+=16;
    
            duration += receive_delay*512 + FPGA_read_PCM*2 +FPGA_write_PCM*2+ FPGA_read_PCM*256+FPGA_write_PCM*256;
        }else if(returnValue[0]==2){
            stats.total_num_swap2++;
            mm.swapMapping(returnValue[1],returnValue[2]);
            mm.swapMapping(returnValue[2],returnValue[3]);
            //统计页面1的写入次数
            stats.pagewrite[returnValue[1]]+=16;stats.pagewrite[returnValue[2]]+=32;
            stats.pagewrite[returnValue[3]]+=16;        
            duration += receive_delay*768 + FPGA_read_PCM*3+FPGA_write_PCM*3+ FPGA_read_PCM*384+FPGA_write_PCM*384;
        }else{
            printf("%ld\n",returnValue[0]);
        }
    }
    //delete returnValue; 
    //cerr<<"error there"<<endl;
    // up add.by crc24.3.11
    
    // only consider ourselves busy if there is any need to wait
    // to avoid extra events being scheduled for (infinitely) fast
    // memories
    //duration = pkt->getSize() * bandwidth;

    if (duration != 0) {
        schedule(releaseEvent, curTick() + duration);
        isBusy = true;
    }
    //cerr<<"no 1 error there!!!!"<<endl;
    // go ahead and deal with the packet and put the response in the
    // queue if there is one
    bool needsResponse = pkt->needsResponse();
    recvAtomic(pkt);
    //如果请求需要响应，recvAtomic方法会将数据包状态改为响应状态。
    //下述代码是处理需要响应的pkt
    // turn packet around to go back to requestor if response expected
    if (needsResponse) {
        // recvAtomic() should already have turned packet into
        // atomic response

        assert(pkt->isResponse());

        Tick when_to_send = curTick() + receive_delay + getLatency();
        
        // typically this should be added at the end, so start the
        // insertion sort with the last element, also make sure not to
        // re-order in front of some existing packet with the same
        // address, the latter is important as this memory effectively
        // hands out exclusive copies (shared is not asserted)
        auto i = packetQueue.end();
        --i;
        while (i != packetQueue.begin() && when_to_send < i->tick &&
               !i->pkt->matchAddr(pkt))
            --i;

        // emplace inserts the element before the position pointed to by
        // the iterator, so advance it one step
        packetQueue.emplace(++i, pkt, when_to_send);
        //cerr<<"error there?"<<endl;
        if (!retryResp && !dequeueEvent.scheduled())
            schedule(dequeueEvent, packetQueue.back().tick);
        //cerr<<" 1  no error there!!!!"<<endl;
    } else {
        //cerr<<"error there!!!"<<endl;
        pendingDelete.reset(pkt);
        //cerr<<"error there!!!!"<<endl;
    }
    //cerr<<"no error there!!!!"<<endl;
    return true;
}

void
SimpleMemory::release()
{
    assert(isBusy);
    isBusy = false;
    if (retryReq) {
        retryReq = false;
        port.sendRetryReq();
    }
}

void
SimpleMemory::dequeue()
{
    assert(!packetQueue.empty());
    DeferredPacket deferred_pkt = packetQueue.front();

    retryResp = !port.sendTimingResp(deferred_pkt.pkt);

    if (!retryResp) {
        packetQueue.pop_front();

        // if the queue is not empty, schedule the next dequeue event,
        // otherwise signal that we are drained if we were asked to do so
        if (!packetQueue.empty()) {
            // if there were packets that got in-between then we
            // already have an event scheduled, so use re-schedule
            reschedule(dequeueEvent,
                       std::max(packetQueue.front().tick, curTick()), true);
        } else if (drainState() == DrainState::Draining) {
            
            DPRINTF(Drain, "Draining of SimpleMemory complete\n");
            signalDrainDone();
            //printStats();
        }
    }
}

Tick
SimpleMemory::getLatency() const
{
    return latency +
        (latency_var ? random_mt.random<Tick>(0, latency_var) : 0);
}

Tick
SimpleMemory::getLatency_cache() const //crc ,day 23
{
    return cache_latency +
        (latency_var ? random_mt.random<Tick>(0, latency_var) : 0);
}

void
SimpleMemory::recvRespRetry()
{
    assert(retryResp);

    dequeue();
}

Port &
SimpleMemory::getPort(const std::string &if_name, PortID idx)
{
    if (if_name != "port") {
        return AbstractMemory::getPort(if_name, idx);
    } else {
        return port;
    }
}

DrainState
SimpleMemory::drain()
{
    if (!packetQueue.empty()) {
        DPRINTF(Drain, "SimpleMemory Queue has requests, waiting to drain\n");
        return DrainState::Draining;
    } else {
        return DrainState::Drained;
    }
}

SimpleMemory::MemoryPort::MemoryPort(const std::string& _name,
                                     SimpleMemory& _memory)
    : ResponsePort(_name), mem(_memory)
{ }

AddrRangeList
SimpleMemory::MemoryPort::getAddrRanges() const
{
    AddrRangeList ranges;
    ranges.push_back(mem.getAddrRange());
    return ranges;
}

Tick
SimpleMemory::MemoryPort::recvAtomic(PacketPtr pkt)
{
    return mem.recvAtomic(pkt);
}

Tick
SimpleMemory::MemoryPort::recvAtomicBackdoor(
        PacketPtr pkt, MemBackdoorPtr &_backdoor)
{
    return mem.recvAtomicBackdoor(pkt, _backdoor);
}

void
SimpleMemory::MemoryPort::recvFunctional(PacketPtr pkt)
{
    mem.recvFunctional(pkt);
}

void
SimpleMemory::MemoryPort::recvMemBackdoorReq(const MemBackdoorReq &req,
        MemBackdoorPtr &backdoor)
{
    mem.recvMemBackdoorReq(req, backdoor);
}

bool
SimpleMemory::MemoryPort::recvTimingReq(PacketPtr pkt)
{
    return mem.recvTimingReq(pkt);
}

void
SimpleMemory::MemoryPort::recvRespRetry()
{
    mem.recvRespRetry();
}

//用来打印输出内容的语句
SimpleMemory::mycachehitStats::mycachehitStats(statistics::Group *parent)
      : statistics::Group(parent),
      ADD_STAT(pagewrite, statistics::units::Count::get(), "pagewrite"),
      ADD_STAT(LLCwrite, statistics::units::Count::get(), "llcwrite"),
      ADD_STAT(hits, statistics::units::Count::get(), "Number of hits"),
      ADD_STAT(misses, statistics::units::Count::get(), "Number of misses"),
      ADD_STAT(total_num_read, statistics::units::Count::get(), "Number of total_num_read"),
      ADD_STAT(total_num, statistics::units::Count::get(), "Number of total_num"),
      ADD_STAT(total_num_swap1, statistics::units::Count::get(), "Number of total_num_swap1"),
      ADD_STAT(total_num_swap2, statistics::units::Count::get(), "Number of total_num_swap2"),
      ADD_STAT(hitRatio, statistics::units::Ratio::get(),
               "The ratio of hits to the total accesses to mycache",
               hits / (hits + misses))
{
}
void 
SimpleMemory::mycachehitStats::regStats(){
    
    using namespace statistics;
    
    statistics::Group::regStats();
    //AbstractMemory::regStats();
    
    pagewrite
        .init(1048576)
        .flags(total)
        ;

    LLCwrite
        .init(16777216)
        .flags(total)
        ;
    for (int i = 0; i < 1048576; i++) {
        pagewrite.subname(i, csprintf("pagenum-%i", i));
    }
    for (int i = 0; i < 1048576*16; i++) {
        LLCwrite.subname(i, csprintf("llcnum-%i", i));
    }    
}
} // namespace memory
} // namespace gem5