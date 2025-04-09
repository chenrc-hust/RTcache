#ifndef __WAY_CACHE_HH__
#define __WAY_CACHE_HH__

#include <queue>
#include <iostream>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <fstream>
#include <random>
#include <algorithm>

// std::ofstream waycout_("test2.out");

class _way_cache
{ // 缓存中的映射表
public:
    struct AddrRange
    {
        uint64_t start;
        uint64_t end;
    };
    const int seed = 12345;
    int associate;         // 组大小
    int tag_length;        // tag_位数
    int dram_size;         // 内存总页数'
    uint64_t total_access; // 总请求，
    uint64_t hit;
    bool pre_;   // 是否预取
    int pre_num; // 预取个数
    class cache
    {
    public:
        _way_cache &_way_cache_;
        uint64_t size;    // 总容量
        uint64_t set_num; // 组大小
        std::vector<std::list<uint64_t>> LRU;
        std::unordered_map<uint64_t, std::list<uint64_t>::iterator> Cache_;
        cache(_way_cache &_way_Cache_, uint64_t size_) : _way_cache_(_way_Cache_), size(size_),
                                                         set_num(size / _way_cache_.associate)
        {
            LRU.resize(set_num);
        }

        bool hascache(uint64_t va);

        void delete_(uint64_t va);

        void get(uint64_t va);

        void put(uint64_t va);
    };

    _way_cache(int as, int num_, int dram_size_, bool pre, int pre_n);

    ~_way_cache();
    cache Cache;

    std::vector<uint64_t> VA_PA, PA_VA;                                         // 内存中的映射表
    std::unordered_map<uint32_t, std::vector<AddrRange>> continuous_blocks_map; // 存储不同大小连续块的信息

    bool get(uint64_t VA);

    void update(uint64_t page1, uint64_t page2);

    void print();


    // void wear_print(); // 磨损均衡分布
};

#endif //__WAY_CACHE_HH__