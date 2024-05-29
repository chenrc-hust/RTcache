
#include "mem/simple_mem.hh"

#include "base/random.hh"
#include "base/trace.hh"
#include "debug/Drain.hh"
#include "sim/sim_exit.hh"
#include "mem/mycache.hh"




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
    swap_(p.swap_),
    FPGA_read_PCM(p.FPGA_read_PCM),FPGA_write_PCM(p.FPGA_write_PCM),myclimberobj(1048576,p.swap_),mytwlobj(1048576,p.swap_),mm(),
    stats(this)
{
    for(int i=0;i<1048576;i++)
    {
        pageWriteCounts[i]=0;
        pageRemap.push_back(i);//把地址初始化为自身
        
    }    

    cerr<<"swap_"<<swap_<<endl;
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
    return -1;
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
    
    Addr remapping_index;//映射后地址
    Addr remapping_index_test;//映射后地址
    Addr page_index = pkt_addr >>12; //原来的页号
    //Addr llc_index = pkt_addr >>8 ; //LLC号
    //页面超出范围时报错
    panic_if(page_index>=1048576,
             "Should only smaller than 1048576 , "
             " %#llx\n", pkt->getAddr());

    //总读写操作次数
    stats.total_num++;
    /* 简单来说，当CPU发出一个内存访问请求时，这个请求被封装成一个数据包发送给内存控制器。
    这个数据包首先经历一个“头部延迟”，即数据包的头部信息（比如地址、操作类型等）在网络中的传输时间。
    一旦头部信息到达内存控制器，内存控制器就已经可以开始处理这个请求了。但是，如果这个操作是一个写操作，
    内存控制器还需要将数据包中的数据（即负载）“解包”出来，这是因为数据通常是以序列化的形式在网络中传输的，内存控制器需要将其转换回可用的格式，然后才能执行写入操作。 */
    // technically the packet only reaches us after the header delay,
    // and since this is a memory controller we also need to
    // deserialise the payload before performing any write operation
    Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;
    Tick read_receive_delay = 8238;
    Tick write_receive_delay = 12328;
    //cerr<<"receive_delay "<<receive_delay<<"is read"<<pkt_read<<endl;
    //cerr<<"no error the111re!!!!"<<endl;
    // update the release time according to the bandwidth limit, and
    // do so with respect to the time it takes to finish this request
    // rather than long term as it is the short term data rate that is
    // limited for any real memory

    // calculate an appropriate tick to release to not exceed
    // the bandwidth limit
    /* 
这段注释解释的是内存控制器如何管理内存带宽，以确保不超过设定的带宽限制。
这里提到的“release time”指的是内存控制器处理完当前请求并准备好处理下一个请求的时间。这个处理过程需要考虑内存的带宽限制，
即在单位时间内内存控制器能处理的数据量有一个上限。 */
    Tick duration =  pkt->getSize() * bandwidth;

    Tick FPGA_MEN = bool_fpag_mem ? FPGA_read_PCM : 0;
    Tick time_temp = 0;
    bool hit_flag=false;//缓存相关
    /* 最后读写数据的延迟 */
    if(pkt_read) {
        time_temp+= (read_receive_delay  + FPGA_read_PCM );
    }
    else {
        time_temp+= (write_receive_delay + FPGA_write_PCM) ;
    }
    uint64_t* returnValue;
    //getLatency()==73.3ns ddr4
    //Tick FPGA_MEN=0; //映射表访问时间
    /* 访问rtcache延迟+访问映射表延迟+访问数据延迟+磨损均衡延迟 */
    if(bool_cache){//有rtcache
       //cerr<<"error in readInputFile"<<endl;
        panic_if(pkt_addr >= 1048576LL * 1024 * 4,
                    "Should only smaller than 1048576 , "
                    " %ld\n", remapping_index);
        //cerr<<pkt_addr<<endl;
        hit_flag=mm.readInputFile(pkt_addr,pkt_read);//返回的是页号，不修改映射关系
        //cerr<<"No error in readInputFile"<<endl;
        remapping_index=mm.queryRemap(page_index);//有rtcache时
        remapping_index_test = pageRemap[page_index];
        //cerr<<remapping_index<<" "<<remapping_index_test<<endl;
        panic_if(remapping_index!=remapping_index_test,
                    "remapping_index Should =  remapping_index_test , "
                    " %ld\n", remapping_index);
        assert(remapping_index==remapping_index_test);
        if(hit_flag){//命中时去cache读即可
            stats.hits++;
            time_temp += getLatency_cache() ;
        }else{//未命中时FPGA_read_PCM
            stats.misses++;  
            time_temp += ((FPGA_MEN == 0 ) ? 0 : FPGA_read_PCM);
        }

        /* 统计磨损均衡信息 */
        if(what_twl==0){//无磨损均衡

        }else if(what_twl==3){
            //达到交换门槛
            
            if(allwritecount>=swap_){
            //cerr<<"swap_time"<<endl;
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
            /* 更新 pageWriteCounts总的来说，当你修改迭代器指向的对象时，确实是在修改容器中的元素。
            这是因为迭代器提供了对容器中元素的直接访问。因此，通过迭代器进行的修改会反映在容器本身上。*/

            //更新统计表
            stats.pagelife[min_it->first]+=1;stats.pagelife[max_it->first]+=1;
            /*  pageWriteCounts是twl=3时监控寿命的数组*/
            pageWriteCounts[min_it->first]+=1;pageWriteCounts[max_it->first]+=1;//页面实际写入次数
            Addr temp_1 = mm.queryfront(min_it->first);
            Addr temp_2 = mm.queryfront(max_it->first);
            mm.swapMapping(temp_1,temp_2);//修改映射关系
            /* 测试两个映射表是否相同 */
            Addr temp_1_pageRemap = queryfront(min_it->first);
            Addr temp_2_pageRemap = queryfront(max_it->first);
            swap(pageRemap[temp_1_pageRemap],pageRemap[temp_2_pageRemap]);
            stats.total_num_swap1++;//读32，写32
            
            allwritecount=0;
            /* 写没有receive延迟 */
            time_temp += (read_receive_delay*34 + FPGA_read_PCM*2 
                     +FPGA_write_PCM*2+ FPGA_read_PCM*32+FPGA_write_PCM*32);
            }
        }else{//选择twl climber 磨损均衡
            /* 返回的地址是待交换的地址，还是*/

            if(what_twl==1){
                returnValue = mytwlobj.access2(page_index,pkt_read);//判断交换情况，传入
               //cerr<<"what_twl 1"<<endl; 
            }
            else{
                returnValue = myclimberobj.access2(page_index,pkt_read);//判断交换情况
               //cerr<<"what_twl 2"<<endl; 
            } 
            
            if(returnValue[0] == 0){}
            else if(returnValue[0]==1){
                stats.total_num_swap1++;
                //printf("%ld  %ld =1\n",returnValue[1],returnValue[2]);
               //cerr<<"swap1"<<endl;
                Addr temp_1 = mm.queryRemap(returnValue[1]);
                Addr temp_2 = mm.queryRemap(returnValue[2]);
                
                mm.swapMapping(returnValue[1],returnValue[2]);//

                //地址没有问题，
               //cerr<<"error -1"<<endl;
                //统计页面的写入次数
                /* 测试两个映射表是否相同 */
                swap(pageRemap[returnValue[1]],pageRemap[returnValue[2]]);
               //cerr<<"swap1"<<endl;
                stats.pagelife[temp_1]+=1;stats.pagelife[temp_2]+=1;
               //cerr<<"error 1"<<endl;       
                time_temp += ( read_receive_delay*34 
                            + write_receive_delay*34+ FPGA_read_PCM*32+FPGA_write_PCM*32);
            }/* 测试是否第二种交换导致 */
            else if(returnValue[0]==2){
                stats.total_num_swap2++;
                //cerr<<returnValue[1]<<" "<<returnValue[2]<<" "<<returnValue[3]<<endl;
                Addr temp_1 = mm.queryRemap(returnValue[1]);
                Addr temp_2 = mm.queryRemap(returnValue[2]);
                Addr temp_3 = mm.queryRemap(returnValue[3]);    

                mm.swapMapping(returnValue[1],returnValue[3]);
                mm.swapMapping(returnValue[2],returnValue[3]);
            
                //统计页面1的写入次数
                /* 测试两个映射表是否相同 */
                swap(pageRemap[returnValue[1]],pageRemap[returnValue[3]]);
                swap(pageRemap[returnValue[2]],pageRemap[returnValue[3]]);

                stats.pagelife[temp_1]+=2;stats.pagelife[temp_3]+=1;
                stats.pagelife[temp_2]+=1;
                time_temp += ( read_receive_delay*51 + FPGA_read_PCM*3+FPGA_write_PCM*3+ FPGA_read_PCM*48+FPGA_write_PCM*48);
            }else{
                printf("%ld\n",returnValue[0]);
            }
            //cerr<<"error there"<<endl;
        }
    }else{//没有rtcache的情况  访问映射表延迟+访问数据延迟+磨损均衡延迟    
        time_temp += ((FPGA_MEN == 0 ) ? 0 : FPGA_read_PCM);//没有rtcache的情况下都视为未命中
        remapping_index=pageRemap[page_index];//没有rtcache时的映射表
        /* 统计磨损均衡信息 */
        if(what_twl==0){//无磨损均衡
        
        }else if(what_twl==3){
            //达到交换门槛
            if(allwritecount>=swap_){
            //cerr<<"swap_time"<<endl;
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
            pageWriteCounts[min_it->first]+=1;pageWriteCounts[max_it->first]+=1;//页面实际写入次数
            //更新统计表
            stats.pagelife[min_it->first]+=1;stats.pagelife[max_it->first]+=1;
            /*  pageWriteCounts是twl=3时监控寿命的数组*/

            Addr temp_1 = queryfront(min_it->first);
            Addr temp_2 = queryfront(max_it->first);
            swap(pageRemap[temp_1],pageRemap[temp_2]);
            stats.total_num_swap1++;
            allwritecount=0;
            time_temp +=(read_receive_delay*34  + FPGA_read_PCM*2 
                     +FPGA_write_PCM*2+ FPGA_read_PCM*32+FPGA_write_PCM*32);
            }
        }else{//选择twl 磨损均衡
            /* 返回的地址是待交换的地址，还是*/
            
            if(what_twl==1)returnValue = mytwlobj.access2(page_index,pkt_read);//判断交换情况，传入
            else returnValue = myclimberobj.access2(page_index,pkt_read);//判断交换情况
            
            if(returnValue[0] == 0){
                //cerr<<"what_twl "<<what_twl<<"no swap"<<endl;
            }else if(returnValue[0]==1){
                stats.total_num_swap1++;

                //cerr<<"error -1"<<endl;
                /* 没有rtcache的情况 */
                /* 读写粒度是256B */
                Addr temp_1 = pageRemap[returnValue[1]];
                Addr temp_2 = pageRemap[returnValue[2]];
                //地址没有问题，
                //统计页面的写入次数
                stats.pagelife[temp_1]+=1;stats.pagelife[temp_2]+=1;
                //cerr<<"error 1"<<endl;       
                swap(pageRemap[returnValue[1]],pageRemap[returnValue[2]]);
                //页面交换开销计算应该是根据读写粒度修改
                time_temp +=(read_receive_delay*34  + FPGA_read_PCM*2 
                        +FPGA_write_PCM*2+ FPGA_read_PCM*32+FPGA_write_PCM*32);
            }else if(returnValue[0]==2){
                stats.total_num_swap2++;
                //cerr<<returnValue[1]<<" "<<returnValue[2]<<" "<<returnValue[3]<<endl;

                Addr temp_1 = pageRemap[returnValue[1]];
                Addr temp_2 = pageRemap[returnValue[2]];             
                Addr temp_3 = pageRemap[returnValue[3]];         
                //统计页面1的写入次数
                /* 没有rtcache的情况 */
                swap(pageRemap[returnValue[1]],pageRemap[returnValue[3]]);
                swap(pageRemap[returnValue[2]],pageRemap[returnValue[3]]);
                stats.pagelife[temp_1]+=2;stats.pagelife[temp_3]+=1;
                stats.pagelife[temp_2]+=1;
                
                time_temp +=  (read_receive_delay*51 
                        + FPGA_read_PCM*3+FPGA_write_PCM*3+ FPGA_read_PCM*48+FPGA_write_PCM*48);
            }else{
                printf("%ld\n",returnValue[0]);
            }
            //delete returnValue; 
            //cerr<<"error there"<<endl;
        }
    }    
    /* 统计读写 */
    //cerr<<"error 1 1048576 "<<endl;   
    if(!pkt_read){
        panic_if(remapping_index>=1048576,
            "Should only smaller than 1048576 , "
            " %ld\n", remapping_index);
        assert(remapping_index<1048576);
        stats.pagelife[remapping_index]++;
        pageWriteCounts[remapping_index]++;//页面实际写入次数
        allwritecount++;//对应twl=3
    }else{
        stats.total_num_read++;
    }       
    stats.total_num_time+=time_temp;
    duration+=time_temp;
    
           
    
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
    /*  */
    recvAtomic(pkt);
    //如果请求需要响应，recvAtomic方法会将数据包状态改为响应状态。
    //下述代码是处理需要响应的pkt，比如传递读取后的数据
    // turn packet around to go back to requestor if response expected
    if (needsResponse) {
        // recvAtomic() should already have turned packet into
        // atomic response
        /* 只有读包需要响应 */
       //cerr<<pkt_read<<" pkt->read"<<endl;
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
       //cerr<<"packqueue error"<<endl;
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
/* 目前设置的为0 */
Tick
SimpleMemory::getLatency() const
{
    return latency +
        (latency_var ? random_mt.random<Tick>(0, latency_var) : 0);
}
/* 命中rtcache时的延迟 */
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
    int i = 0;
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
        ADD_STAT(hits, statistics::units::Count::get(), "Number of hits"),
        ADD_STAT(misses, statistics::units::Count::get(), "Number of misses"),
        ADD_STAT(total_num, statistics::units::Count::get(), "Number of total_num"),
        ADD_STAT(total_num_read, statistics::units::Count::get(), "Number of total_num_read"),
        ADD_STAT(total_num_swap1, statistics::units::Count::get(), "Number of total_num_swap1"),
        ADD_STAT(total_num_swap2, statistics::units::Count::get(), "Number of total_num_swap2"),
        ADD_STAT(hitRatio, statistics::units::Ratio::get(),
                "The ratio of hits to the total accesses to mycache",
                hits / (hits + misses)),
        ADD_STAT(pagelife, statistics::units::Count::get(), "pagelife"),
        ADD_STAT(total_num_time, statistics::units::Count::get(), "Number of total_num_time")
        //ADD_STAT(LLCwrite, statistics::units::Count::get(), "llcwrite")
{
}
void 
SimpleMemory::mycachehitStats::regStats(){
    
    using namespace statistics;
    
    statistics::Group::regStats();
    //AbstractMemory::regStats();
    
    pagelife
        .init(1048576)
        .flags(total)
        ;
    for (int i = 0; i < 1048576; i++) {
        pagelife.subname(i, csprintf("pagenum-%i", i));
    }/* 
    for (int i = 0; i < 1048576*16; i++) {
        LLCwrite.subname(i, csprintf("llcnum-%i", i));
    }     */
}
} // namespace memory
} // namespace gem5