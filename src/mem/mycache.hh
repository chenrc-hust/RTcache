#ifndef __MEM_MY_CACHE_HH__
#define __MEM_MY_CACHE_HH__


//缓存部分
#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <list>
#include <algorithm>
#include <limits.h>
#include <sstream>
#include <cstdint>
#include <cstdlib>
//缓存部分头文件
//剩余部分代码



struct CacheEntry {//代表第二级缓存中的一个条目，包含条目的索引、是否被访问过的标记，以及一个结果值。
    long long index;
    bool accessed;
    long long result;
    CacheEntry(long long index, bool accessed) : index(index), accessed(accessed) {}
};
struct Cache1Entry {//代表第一级缓存中的一个条目，包含索引、结果值、第二级缓存条目的数量，以及一个指向第二级缓存条目列表的指针。
    long long index;
    long long result;
    long long cache2size;
    std::list<CacheEntry> cache2;
    Cache1Entry(long long index) : index(index) {
        cache2size = 0;
    }
};
struct BufferEntry {//代表缓冲区中的一个条目，包含一个索引和结果值。
    long long index;
    long long result;
    BufferEntry(long long index) : index(index) {}
};
struct CounterEntry {//用于追踪每个物理地址的写入次数。
    long long index;
    long long number;
    CounterEntry(long long index) : index(index) {
        number = 0;
    }
};
class MemoryModel {
    private:
    
    std::vector<long long> remaptable;
    std::vector<std::list<Cache1Entry>> sets;//一级缓存表
    std::list<CounterEntry> counters;
    std::vector<std::list<BufferEntry>> buffersets;//缓冲表
    //indextable：加速访问，空间换时间,以页号为索引，存储指向cache对应表项的迭代器；booltable：有效位
    std::vector<std::list<Cache1Entry>::iterator> cache1indextable;
    std::vector<std::list<CacheEntry>::iterator> cache2indextable;
    std::vector<std::list<BufferEntry>::iterator> bufferindextable;
    std::vector<std::list<CounterEntry>::iterator> counterindextable;
    std::vector<bool> cache1booltable;
    std::vector<bool> cache2booltable;
    std::vector<bool> bufferbooltable;
    long long totalAccesses = 0;
    long long totalWrite = 0;
    long long totalHits = 0;
    long long level1Hits = 0;
    long long level2Hits = 0;
    long long bufferHits = 0;
    long long cacheEvicts = 0;
    long long bufferEvicts = 0;
    long long maxAddress;
    long long minAddress;
    long long minWriteCount = 0;
    long long maxWriteCount = 0;
    long long minWriteAddr = -1;
    long long maxWriteAddr = -1;
    bool isswap = 0;
    //std::ofstream resultout;
    //std::unordered_map<long long, long long> writeCounts;  // 用于追踪每个物理地址的写入次数
    std::vector<long long> writeCounts;  // 用于追踪每个物理地址的写入次数
    const int SWAP_THRESHOLD = 16*128;  // 可以根据需求设置这个阈值 
public:
    long long resultaddr;//返回映射后的地址
    //假设256GB，地址长度38位，页大小4KB，页号长度26位，第一级23位，第二级3位，每一个第一级包括4个第二级，使能位1位，accessed1位，23*2+1+(3*2+1+1)*4=79(对齐到12B)
    //buffer页号26，使能位1位，53b，8B
    const long long TOTAL_CAP = 256*1024;//256KB
    //const int LEVEL2_TABLE_SIZE = 8;
    //两级cache和buffer大小3:1
    const long long LEVEL2_TABLE_SIZE = 8;
    long long CACHE1_SIZE ;
    const long long CACHE2_SIZE = 8;
    long long BUFFER_SIZE ;
    const long long associativity = 8; // 4-way set associative 
    const long long PREFETCH_COUNT = 7; // 预取前3条
    long long setnums ;
    long long buffersetnums ;
    std::ofstream resultout;
    MemoryModel();
    void init();//初始化内存模型，设置缓存大小、缓冲区大小等。
    /* 根据是否是第一级缓存缺失来处理缓存的逐出逻辑。
        如果是第一级缺失，则逐出整个第一级相关的第二级条目；
        如果是第二级缺失，则只逐出一个第二级条目。 */
    void evict(bool isFirstLevelMiss, std::_List_iterator<Cache1Entry> Cache1Entry1, long long level1Index) ;
    /* 模拟对内存地址的访问，并处理缓存命中、缓存和缓冲区的替换逻辑。 */
    bool access1(long long  physicalAddress, bool isRead);
    /* 预取逻辑，根据当前访问的地址预取接下来可能会访问的地址。 */
    void prefetch(std::_List_iterator<Cache1Entry>& Cache1Entry1, long long level2Index) ;
    /* 测试结果输出 */
    void printStats();
    /* 禁用指定缓存条目的方法。 */
    void disableentry(long long level1Index, long long level2Index);
    /* 在物理地址之间交换映射，用于模拟写入次数较多的地址与写入次数较少的地址之间的映射交换。 */
    void swapMapping(long long minWriteAddr1,  long long maxWriteAddr1) ;
    /* 查询映射地址 */
    long long queryRemap(long long Addr1);
     /* 前向查询映射地址 */
    long long queryfront(long long Addr1);
    /* 确定内存访问的地址范围，初始化相关的数据结构。 */
    void findfootprint();
    /* 处理从输入文件读取的访问请求，模拟对每个物理地址的读/写操作，并根据条件触发交换映射的逻辑。 */
    bool readInputFile(long long  physicalAddress, bool isRead);
    
};
#endif //__MEM_MY_CACHE_HH__