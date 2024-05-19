#include "mem/mycache.hh"
#include <memory>
#include <list>
#include <cassert>

MemoryModel::MemoryModel(){}

/* 通过init()方法设置内存模型的基础配置。 */
void MemoryModel::init(){
    setnums = CACHE1_SIZE/associativity;//组数，
    buffersetnums = BUFFER_SIZE/associativity;
    sets.resize(setnums);
    buffersets.resize(buffersetnums);
    resultout.open("design1_footprint.log", std::ios::out);
}

void MemoryModel::evict(bool isFirstLevelMiss, std::_List_iterator<Cache1Entry> Cache1Entry1, long long level1Index) {
    //std::cout<< "23" << std::endl;
    std::list<Cache1Entry>& cache1 = sets[level1Index%setnums];//组号
    if (isFirstLevelMiss) {//第一级缺失
        //std::cout<< "24" << std::endl;

        // 当第一级缺失时，如果当前已满，则选择逐出整个第一级相关的第二级条目
        if (cache1.size() >= associativity) {//当前已经存满了
            //std::cout<< "25" << std::endl;
            auto evictIt = cache1.begin();
            
            /*auto evictIt2 = cache1.begin();
            while(evictIt2 != cache1.end()){
                if(evictIt->cache2size > evictIt2->cache2size){
                    evictIt = evictIt2;
                }
                evictIt2++;
            }*/
            cacheEvicts++;//统计逐出数

            auto it = evictIt->cache2.begin();//
            while (it != evictIt->cache2.end()) {
                if(it->accessed){//如果被访问过
                    evictIt->cache2size = (evictIt->cache2size) - 1;
                }
                //被访问了且缓存中不存在相关的表项，
                if(it->accessed && bufferbooltable[(it->index)-(minAddress>>12)] == false){
                    std::list<BufferEntry>& buffer = buffersets[(it->index)%buffersetnums];
                    auto newbufferentry1 = new BufferEntry(it->index);
                    newbufferentry1->result = it->result;
                    buffer.push_back(*newbufferentry1);  // 将逐出的条目添加到缓冲区
                    /* 出现delete */
                    delete newbufferentry1;
                    bufferbooltable[(it->index)-(minAddress>>12)] = true;
                    bufferindextable[(it->index)-(minAddress>>12)] = --(buffer.end());
                    if (buffer.size() > associativity) {//超出时逐出
                        bufferbooltable[(buffer.begin()->index)-(minAddress>>12)] = false;
                        buffer.pop_front();
                        bufferEvicts++;
                    }
                }
                cache2booltable[(it->index)-(minAddress>>12)] = false;
                it = evictIt->cache2.erase(it);
                cacheEvicts++;
            }
            cache1booltable[(evictIt->index)-(minAddress>>15)] = false;
            cache1.erase(evictIt);
        }
    } else {
        // 当第二级缺失时只逐出一个第二级条目
        //std::cout<< "22" << std::endl;
        if (Cache1Entry1 != cache1.end()) {//第一级存在
            //判断第二级是否满了
            if (Cache1Entry1->cache2.size() >= CACHE2_SIZE) {//第二级满
                auto lruIt = Cache1Entry1->cache2.begin();
                if (lruIt != Cache1Entry1->cache2.end()) {
                    if(lruIt->accessed){
                        Cache1Entry1->cache2size = (Cache1Entry1->cache2size) - 1;
                        if(bufferbooltable[(lruIt->index)-(minAddress>>12)] == false){
                            std::list<BufferEntry>& buffer = buffersets[(lruIt->index)%buffersetnums];
                            auto newbufferentry1 = new BufferEntry(lruIt->index);
                            newbufferentry1->result = lruIt->result;
                            buffer.push_back(*newbufferentry1);  // 将逐出的条目添加到缓冲区
                            /* delete  */
                            delete newbufferentry1;
                            bufferbooltable[(lruIt->index)-(minAddress>>12)] = true;
                            bufferindextable[(lruIt->index)-(minAddress>>12)] = --(buffer.end());
                            if (buffer.size() > associativity) {
                                bufferbooltable[(buffer.begin()->index)-(minAddress>>12)] = false;
                                buffer.pop_front();
                                bufferEvicts++;
                            }
                        }
                    }
                    cache2booltable[(lruIt->index)-(minAddress>>12)] = false;
                    Cache1Entry1->cache2.erase(lruIt);  // 从第二级缓存中删除该条目
                    cacheEvicts++;
                }
            }
        }
    }
}

/* access1()方法模拟内存访问，并根据地址是否已缓存决定是直接返回结果还是执行缓存逐出和预取。 */
bool MemoryModel::access1(long long  physicalAddress, bool isRead) {
    //writeCounts[(physicalAddress>>12)-(minAddress>>12)]++;
    //std::cerr<<"MemoryModel::access1"<<std::endl;
    if(!isRead){
        totalWrite++;
        isswap = 1;
        counterindextable[(physicalAddress>>12)-(minAddress>>12)]->number = counterindextable[(physicalAddress>>12)-(minAddress>>12)]->number + 1;
        /* find max write */
        if(maxWriteCount < counterindextable[(physicalAddress>>12)-(minAddress>>12)]->number){
            maxWriteCount = counterindextable[(physicalAddress>>12)-(minAddress>>12)]->number;
            maxWriteAddr = (physicalAddress>>12)-(minAddress>>12);
        }
        /* find min write */
        if(minWriteCount > counterindextable[(physicalAddress>>12)-(minAddress>>12)]->number){
            minWriteCount = counterindextable[(physicalAddress>>12)-(minAddress>>12)]->number;
            minWriteAddr = (physicalAddress>>12)-(minAddress>>12);
        }
        /* 使用counters.splice(counters.end(), counters, 
        counterindextable[(physicalAddress>>12)-(minAddress>>12)])将对应的计数器移动到counters列表的末尾。
        splice操作是将某个元素从列表中的一个位置移动到另一个位置，
        这里的目的可能是为了保持计数器的活跃度或访问顺序，使得最近被写入的地址对应的计数器位于列表的末尾。 */
        counters.splice(counters.end(), counters, counterindextable[(physicalAddress>>12)-(minAddress>>12)]);
    }

    ++totalAccesses;
    ///std::cerr<< "1" << std::endl;
    /*if(totalAccesses%1000000 == 0){
        std::cout<< totalAccesses << std::endl;
    }*/
    long long level1Index = (physicalAddress >> 15);
    long long level2Index = (physicalAddress >> 12);
    std::list<Cache1Entry>& cache1 = sets[(level1Index)%setnums];
    auto it1 = cache1indextable[level1Index-(minAddress>>15)];
    if(cache1booltable[level1Index-(minAddress>>15)] == true){
        level1Hits++;
        /*  update the cache  */
        cache1.splice(cache1.end(), cache1, it1);
        
        auto it2 = cache2indextable[level2Index-(minAddress>>12)];
        if(cache2booltable[level2Index-(minAddress>>12)] == true){
            //std::cerr<< "11" << std::endl;
            level2Hits++;
            totalHits++;
            if(it2->accessed == false){
                //std::cerr<< "t2->accessed6" << std::endl;
                cache1indextable[level1Index-(minAddress>>15)]->cache2size = (cache1indextable[level1Index-(minAddress>>15)]->cache2size) + 1;
            
            }
            it2->accessed = true;
            //std::cerr<<it2->result<< std::endl;
            //gem5 has encountered a segmentation fault!
            assert(it1 != cache1.end());
            assert(it2 != it1->cache2.end());

            it1->cache2.splice(it1->cache2.end(), it1->cache2, it2);
            //std::cerr<< "4" << std::endl;
            //return it2->result;
            //resultaddr=it2->result;
            return true;
        } else {
            //std::cout<< "12" << std::endl;
            std::list<BufferEntry>& buffer = buffersets[(level2Index)%buffersetnums];
            auto it3 = bufferindextable[level2Index-(minAddress>>12)];
            //std::cout<< "5" << std::endl;
            //if (it3 != buffer.end()){
            if(bufferbooltable[level2Index-(minAddress>>12)] == true){
                //std::cout<< "13" << std::endl;
                bufferHits++;
                totalHits++;
                buffer.splice(buffer.end(), buffer, it3);
                //std::cout<< "6" << std::endl;
                //return it3->result;
                //resultaddr=it3->result;
                return true;
                //return level1Table[level1Index] * LEVEL2_TABLE_SIZE + level2Table[level1Index][level2Index];
            }
            //std::cout<< "7" << std::endl;
            evict(false, it1, level1Index);
        }
    } else {
        //::cout<< "14" << std::endl;
        std::list<BufferEntry>& buffer = buffersets[(level2Index)%buffersetnums];
        //std::cout<< "8" << std::endl;
        auto it3 = bufferindextable[level2Index-(minAddress>>12)];
        if(bufferbooltable[level2Index-(minAddress>>12)] == true){
            //std::cout<< "15" << std::endl;
            bufferHits++;
            totalHits++;
            buffer.splice(buffer.end(), buffer, it3);
            //return it3->result;
            //resultaddr=it3->result;
            return true;
        }
        evict(true, cache1.end(), level1Index);
    }
    //std::cout<< "17" << std::endl;
    if(cache1booltable[level1Index-(minAddress>>15)] == false){
        auto newentry1 = new Cache1Entry(level1Index);
        //cache1.push_back(level1Index);
        cache1.push_back(*newentry1);
        delete newentry1;
        //std::cout<< "18" << std::endl;
        cache1booltable[level1Index-(minAddress>>15)] = true;
        cache1indextable[level1Index-(minAddress>>15)] = --(cache1.end());
        prefetch(cache1indextable[level1Index-(minAddress>>15)], level2Index);
        if(cache1.size()>associativity){
            std::cout<<"linenums:"<<totalAccesses<<" cache1 size ERROR!"<<std::endl;
            exit(0);
        }
    }
    if(cache2booltable[level2Index-(minAddress>>12)] == false){
        auto newentry2 = new CacheEntry(level2Index,true);
        newentry2->result = remaptable[level2Index-(minAddress>>12)];
        cache1indextable[level1Index-(minAddress>>15)]->cache2.push_back(*newentry2);
        cache1indextable[level1Index-(minAddress>>15)]->cache2size=(cache1indextable[level1Index-(minAddress>>15)]->cache2size)+1;
        //std::cerr<< "20" << std::endl;
        delete newentry2;
        cache2booltable[level2Index-(minAddress>>12)] = true;
        cache2indextable[level2Index-(minAddress>>12)] = --(cache1indextable[level1Index-(minAddress>>15)]->cache2.end());
        if(cache1indextable[level1Index-(minAddress>>15)]->cache2.size()>CACHE2_SIZE){
            std::cout<<"linenums:"<<totalAccesses<<" cache2 size ERROR!"<<std::endl;
            exit(0);
        }
    }
    //std::cerr<< "21" << std::endl;
    //return cache2indextable[level2Index-(minAddress>>12)]->result;
    //resultaddr=cache2indextable[level2Index-(minAddress>>12)]->result;
    return false;
    //return level1Table[level1Index] * LEVEL2_TABLE_SIZE + level2Table[level1Index][level2Index];
}


void MemoryModel::prefetch(std::_List_iterator<Cache1Entry>& Cache1Entry1, long long level2Index) {
    long long preindex = 0;
    for (long long i = 1; i <= PREFETCH_COUNT; ++i) {
        preindex = ((level2Index+i)%LEVEL2_TABLE_SIZE) + (((level2Index+i)/LEVEL2_TABLE_SIZE)*LEVEL2_TABLE_SIZE);
        if(preindex>(maxAddress>>12)||preindex<(minAddress>>12)){
            continue;
        }
        auto newentry2 = new CacheEntry(preindex,false);
        newentry2->result = remaptable[preindex-(minAddress>>12)];
        Cache1Entry1->cache2.push_back(*newentry2);
        delete newentry2;
        cache2booltable[preindex-(minAddress>>12)] = true;
        cache2indextable[preindex-(minAddress>>12)] = --(Cache1Entry1->cache2.end());
    }
}




void MemoryModel::printStats() {
std::ofstream fout;
    fout.open("design1.log", std::ios::out);
    fout << "总命中率: " << (static_cast<double>(totalHits) / totalAccesses) << std::endl;
    fout << "第一级命中率: " << (static_cast<double>(level1Hits) / totalAccesses) << std::endl;
    fout << "第二级命中率: " << (static_cast<double>(level2Hits) / totalAccesses) << std::endl;
    fout << "Buffer命中率: " << (static_cast<double>(bufferHits) / totalAccesses) << std::endl;
    fout << "缓存空间逐出次数: " << cacheEvicts << std::endl;
    fout << "缓冲空间逐出次数: " << bufferEvicts << std::endl;
    fout.close();
}
void MemoryModel::disableentry(long long level1Index, long long level2Index){
    std::list<Cache1Entry>& cache1 = sets[(level1Index)%setnums];
    auto it1 = cache1indextable[level1Index-(minAddress>>15)];
    if (cache1booltable[level1Index-(minAddress>>15)] == true) {
        auto it2 = cache2indextable[level2Index-(minAddress>>12)];
        if (cache2booltable[level2Index-(minAddress>>12)] == true) {
            if(it2->accessed){
                it1->cache2size = (it1->cache2size) + 1;
            }
            it1->cache2.erase(it2);
            cache2booltable[level2Index-(minAddress>>12)] = false;
            if(it1->cache2.size()==0){
                cache1.erase(it1);
                cache1booltable[level1Index-(minAddress>>15)] = false;
            }
        }
    }
//清除buffer对应表项
    std::list<BufferEntry>& buffer = buffersets[(level2Index)%buffersetnums];
    auto it3 = bufferindextable[level2Index-(minAddress>>12)];
    if(bufferbooltable[level2Index-(minAddress>>12)] == true){
        bufferbooltable[level2Index-(minAddress>>12)] = false;
        buffer.erase(it3);
    }
}
void MemoryModel::swapMapping(long long minWriteAddr1,  long long maxWriteAddr1) {
    // 交换这两个地址在映射表中的映射
    //std::cerr<<"temp "<<std::endl;
    long long  temp = remaptable[minWriteAddr1];
    remaptable[minWriteAddr1] = remaptable[maxWriteAddr1];
    remaptable[maxWriteAddr1] = temp;
    //交换开销
    //std::cerr<<"counterindextable 1"<<std::endl;
    counterindextable[minWriteAddr1]->number = counterindextable[minWriteAddr1]->number + 1;
    counterindextable[maxWriteAddr1]->number = counterindextable[maxWriteAddr1]->number + 1;
    counters.splice(counters.end(), counters, counterindextable[minWriteAddr1]);
    counters.splice(counters.end(), counters, counterindextable[maxWriteAddr1]);
    //disable cache对应表项
    //std::cerr<<"level1Index1 2"<<std::endl;
    long long level1Index1 = ((minWriteAddr1+(minAddress>>12)) >> 3);
    long long level2Index1 = (minWriteAddr1+(minAddress>>12));
    long long level1Index2 = ((maxWriteAddr1+(minAddress>>12)) >> 3);
    long long level2Index2 = (maxWriteAddr1+(minAddress>>12));
    disableentry(level1Index1, level2Index1);
    disableentry(level1Index2, level2Index2);
    //std::cerr<<"disableentry 2"<<std::endl;
}
long long MemoryModel::queryRemap(long long Addr1) {
    // 查找映射后的地址
    //std::cerr<<"temp "<<std::endl;
    return remaptable[Addr1];
}
long long MemoryModel::queryfront(long long Addr1) {
    // 查找映射后地址对应的前地址
    //std::cerr<<"temp "<<std::endl;
    for(long long i = 0; i<(maxAddress>>12);i++){
        if(remaptable[i]==Addr1){
            return i;
        }
    }
    return -1;
}
//查找地址上下限
void MemoryModel::findfootprint(){
    minAddress = 0;//从0开始
    //maxAddress = 536870912*8;//512MB
    maxAddress = 4294967296 ;//4GB
    //minAddress = 7216730112;
    //maxAddress = 551895363520;
    //minAddress = 39417807168;
    //maxAddress = 551895515520;
    /*换trace不能跳
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Could not open the file!" << std::endl;
        return;
    }
    std::string line;
    std::cout<<"read tracefile start"<<std::endl;
    while (std::getline(file, line)) {
        std::istringstream iss(line);

        long long skip1, skip2, address,skip3, operation;
        
        iss >> skip1 >> skip2 >> address >> skip3 >> operation;
        if(address > maxAddress){
            maxAddress = address;
        }
        if(address < minAddress){
            minAddress = address;
        }
    }
    file.close();
    */
    std::cout<<"initialization start"<<std::endl;
    long long footprint = maxAddress - minAddress + 1;
    std::cout<< "maxAddress: "<< maxAddress << "; minAddress: "<< minAddress <<std::endl;
    std::cout<< "footprint: "<< footprint / 1024 /1024 << "MB"<< std::endl;
    std::cout<<"cache1indextable initialization start"<<std::endl;
    cache1indextable.resize((footprint>>15)+2);
    std::cout<<"cache2indextable initialization start"<<std::endl;
    cache2indextable.resize((footprint>>12)+2);
    std::cout<<"bufferindextable initialization start"<<std::endl;
    bufferindextable.resize((footprint>>12)+2);
    std::cout<<"cache1booltable initialization start"<<std::endl;
    cache1booltable.resize((footprint>>15)+2, 0);
    std::cout<<"cache2booltable initialization start"<<std::endl;
    cache2booltable.resize((footprint>>12)+2, 0);
    std::cout<<"bufferbooltable initialization start"<<std::endl;
    bufferbooltable.resize((footprint>>12)+2, 0);
    std::cout<<"remaptable and countertable initialization start"<<std::endl;
    remaptable.resize((footprint>>12)+2);     
    counterindextable.resize((footprint>>12)+2);
    for(int i = 0; i < remaptable.size();i++){
        remaptable[i] = i+(minAddress>>12);
        auto newcounter = new CounterEntry(i);
        counters.push_back(*newcounter);
        delete newcounter;
        counterindextable[i] = --(counters.end());
        //std::cout<< "20" << std::endl;
    }
    std::cout<<"writeCounts initialization start"<<std::endl;
    //writeCounts.resize(footprint>>12,0);

    std::cout<<sets.size()<<std::endl;
}
bool MemoryModel::readInputFile(long long  physicalAddress, bool isRead) {
        bool resultaccess = access1(physicalAddress, isRead);//返回访问是否命中结果
        //resultout << resultaddr << std::endl;
        //std::cerr<<physicalAddress<<std::endl;
        if(totalWrite%SWAP_THRESHOLD == 0 && isswap == 1){
            isswap = 0;
            //swapMapping();
        }
        return resultaccess;
}   