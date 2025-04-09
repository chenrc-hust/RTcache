#ifndef _NO_CACHE_HH__
#define _NO_CACHE_HH__
#include <iostream>
#include <unordered_map>
#include <vector>
#include <list>
#include <algorithm>
#include <cmath>
#include <random>

class no_cache
{
public:
    struct AddrRange
    {
        uint64_t start;
        uint64_t end;
    };
    const int seed = 12345;
    const uint64_t PageSize = 4096;
    // 页大小
    std::vector<uint64_t> VA_PA, PA_VA;                                         // ，
                                                                                //  指向组相联cache
    std::unordered_map<uint64_t, std::vector<AddrRange>> continuous_blocks_map; // 存储不同大小连续块的信息

    uint64_t MaxDrampage; // dram最大页号

    no_cache(uint64_t MaxDrampage_) : MaxDrampage(MaxDrampage_)
    {
        for (uint64_t i = 0; i < MaxDrampage; i++)
        {
            VA_PA.emplace_back(i);
            PA_VA.emplace_back(i);
        }
        /* std::vector<uint64_t> indices(MaxDrampage);
        for (uint64_t i = 0; i < MaxDrampage; ++i)
        {
            indices[i] = i;
        }
        // 使用固定种子初始化随机数生成器
        std::mt19937 generator(seed);
        // 打乱索引数组
        std::shuffle(indices.begin(), indices.end(), generator);
        // 创建临时数组用于存储打乱后的 VA_PA 和 PA_VA
        std::vector<uint64_t> shuffled_VA_PA(MaxDrampage);
        std::vector<uint64_t> shuffled_PA_VA(MaxDrampage);

        // 重新排列 VA_PA 和 PA_VA
        for (uint64_t i = 0; i < MaxDrampage; ++i)
        {
            shuffled_VA_PA[i] = VA_PA[indices[i]];
            shuffled_PA_VA[indices[i]] = PA_VA[i];
        }
        // 更新 VA_PA 和 PA_VA
        VA_PA = std::move(shuffled_VA_PA);
        PA_VA = std::move(shuffled_PA_VA); */
    }
    inline uint64_t abs_diff(uint64_t a, uint64_t b) {
            return (a > b) ? (a - b) : (b - a);
        }

    uint64_t countNonContiguousAddresses(const std::vector<uint64_t> &VA_PA) {
        uint64_t nonContiguousCount = 0;

        for (size_t i = 0; i < VA_PA.size(); ++i) {
            if ((i == 0 && abs_diff(VA_PA[i], VA_PA[i + 1]) != 1) || 
                (i == VA_PA.size() - 1 && abs_diff(VA_PA[i], VA_PA[i - 1]) != 1) || 
                ((i > 0 && abs_diff(VA_PA[i], VA_PA[i - 1]) != 1) && (i < VA_PA.size() - 1 && abs_diff(VA_PA[i], VA_PA[i + 1]) != 1))) {
                ++nonContiguousCount;
            }
        }

        return nonContiguousCount;
    }

    std::pair<bool, uint64_t> get(uint64_t VA);
    void update(uint64_t page1, uint64_t page2,int type);
    void compute_continuous_blocks();
    void print();
    void time_print();
    
};

#endif //_NO_CACHE_HH__