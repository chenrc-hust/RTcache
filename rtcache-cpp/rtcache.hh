#ifndef __MEM_MY_CACHE_HH__
#define __MEM_MY_CACHE_HH__

// 缓存部分
#include <iostream>
#include <vector>
#include <unordered_map>
#include <list>
#include <algorithm>
#include <limits.h>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <stack>
#include <cassert>
#include <fstream>
#include <bitset>
#include <random>
/* 有效位也得设计上方便统计数据 */
/* 操作接口输入输出都是地址 */
/* int rt_size = 32; // rt一个大小32B,这个是按照256GB的地址总共38位计算的，总容量变化后也发生变化
int sc_size = 6;  // sc一个大小6B
 */
class rt
{
public:
    // 位数用来算容量，
    const int seed = 12345;
    int associate;       // 组大小
    int tag_length;      // tag_位数,在组数不是正好的
    int re_index_length; // re_index位数
    int pre_length;      // 预取长度
    int dram_size;       // 内存总页数

    int RE_num; // 一个RE内部的个数

    // uint64_t two_level_hit = 0;
    // uint64_t sc_hit = 0;
    // uint64_t max_hit = 0;
    // 定义一个结构体来存储数组元素及其原始索引
    bool pre_; // 是否支持预取
    struct IndexedValue
    {
        uint64_t value;
        size_t index;
    };
    struct AddrRange
    {
        uint64_t start;
        uint64_t end;
    };
    /* 区分整体的数目和有没有预取 */
    class RegionEntry
    { // 整体，
    public:
        rt &rtsys;            // 映射系统
        uint64_t TagAndIndex; // tag+index 后的前缀
        // std::vector<bool> PE; // 一个表项，以及有没有访问过

        std::vector<bool> PE;   // 用来表示是否访问过
        std::vector<bool> able; // 用来表示是否可以访问


        RegionEntry(rt &rtsys_, uint64_t va /* , const std::vector<uint64_t> &remap */) : rtsys(rtsys_), TagAndIndex(va / (1 << rtsys.RE_num)),
                                                                                          PE(1 << rtsys.RE_num), able(1 << rtsys.RE_num)
        { // 初始化表,
            if (rtsys.pre_)
            {
                uint64_t preTagAndIndex = va %  (1 << rtsys.RE_num) / (1<<rtsys.pre_length);
                // std::fill(able.begin(), able.end(), true); // 如果允许预取则设置为全1
                for (int i = 0; i < (1 << rtsys.pre_length); i++)
                {

                    able[(preTagAndIndex<<rtsys.pre_length)+i] = true; // 设置有效
                }
            
            }
            
            set(va); // 第一次生成的地址应该设置为访问过
        }

        bool none()
        {
            for (bool bit : able)
            {
                if (bit)
                {
                    return false; // 如果找到一个为 true 的元素，则返回 false
                }
            }
            return true; // 如果所有元素都为 false，则返回 true
        }

        /* 访问之后设置 */
        void set(uint64_t va)
        {
            uint64_t index = va & ((1 << rtsys.RE_num) - 1); // 计算位索引
            // std::cout<<index<<std::endl;
            PE[index] = true;//设置访问位
            able[index] = true;//有效位
            // PE.set(va & ((1 << rtsys.pre_length) - 1));
            // able.set(va & ((1 << rtsys.pre_length) - 1));
        }
        /* 添加表项 ,已有RE的情况下 */
        void put(uint64_t va /* , const std::vector<uint64_t> &remap  */)
        {
            if (rtsys.pre_)
            {
                uint64_t preTagAndIndex = va %  (1 << rtsys.RE_num) / (1<<rtsys.pre_length);
                // std::fill(able.begin(), able.end(), true); // 如果允许预取则设置为全1
                for (int i = 0; i < (1 << rtsys.pre_length); i++)
                {
                    // std::cout << (TagAndIndex << rtsys.pre_length) + i << " "
                    // << remap[(TagAndIndex << rtsys.pre_length) + i] << std::endl;
                    able[(preTagAndIndex<<rtsys.pre_length)+i] = true; // 设置有效
                }
                // std::cout<<va<<std::endl;
                // for(auto i:PE)std::cout<<i<<" "<<preTagAndIndex<<std::endl;
            }
            set(va); // 第一次生成的地址应该设置为访问过
        }
        /* 弹出access的表项 ,只有在满的时候弹出，update不弹出*/
        void outAccess(std::stack<uint64_t> &tmp)
        {
            int count = 0;
            int dis = 0;
            // able.reset(num);//设置不可访问
            for (int i = 0; i < (1 << rtsys.RE_num); i++)
            {
                if (PE[i])
                {
                    count++;
                    if (able[i])
                    { // 插入被访问过的地址,且able

                        dis += (1 << i);
                        // if (i != num)
                        // {
                        tmp.emplace((TagAndIndex << rtsys.RE_num) + i);

                        // }
                    }
                }
            }
            /*  */
            // std::cout<<count<<" "<<dis<<" "<<rtsys.leveldown.size()<<std::endl;

            if (dis != 0)
            {
                rtsys.evict_num++; // 逐出一次
                // if (rtsys.leveldown[dis])
                rtsys.down_num += count; // 降级数比列
            }

            // rtsys.evict_tmp.emplace_back(count, dis);
            /* 暂存逐出统计 */
        }
        /* 返回访问过且有效的个数， */
        int count_ac()
        {
            int count = 0;
            for (int i = 0; i < (1 << rtsys.RE_num); i++)
            {
                if (PE[i] /* && able[i] */)
                { // 插入被访问过的地址,且able
                    count++;
                    // }
                }
            }
            return count;
        }
        // /* 函数，在查询时，输出pair，表示当前被访问的个数和分布 */
        // std::pair<int, int> query()
        // {
        //     int count = 0; //
        //     int dis = 0;   //
        //     int i = 0;
        //     for (auto it : PE)
        //     {
        //         if (it.first&&able.test(i))
        //         {
        //             count++;
        //             dis += (1 << i);
        //         }
        //         i++;
        //     }
        //     return {count, dis};
        // }
    };

    // 逐出缓存，8路组相联，
    class ScariCache
    {
    public:
        rt &rtsys;     // 映射系统
        uint64_t size; // 总容量
        uint64_t set_num;
        std::vector<std::list<uint64_t>> LRU;   // 加速lru过程，将每个组都用vector存储，list中按lru存储对应的值
        std::unordered_map<uint64_t, bool> vst; // 统计lru中表项有没有访问过
        // std::unordered_map<uint64_t, std::pair<uint64_t, std::list<uint64_t>::iterator>> SCcache; // 第一个值是va，第二个值是pa，第三个iterator是对应va在list中的迭代器方便插入删除
        std::unordered_map<uint64_t, std::list<uint64_t>::iterator> SCcache;
        ScariCache(rt &rtsys_, int size_) : rtsys(rtsys_), size(size_), set_num(size /rtsys.associate)
        {
            // std::cout << size_ << std::endl;
            std::cout << "ScariCache set_num "<< set_num << std::endl;
            LRU.resize(set_num);    
            std::cout << "ScariCache size "<< size << std::endl;
            /* for (uint64_t i = 0; i < set_num; i++)
                LRU.emplace_back(std::list<uint64_t>()); // 初始化逐出cache */
        }

        bool hascache(uint64_t va); //

        void delete_sc(uint64_t va); // 查找并删除单一的一个项

        void delete_re(uint64_t va); // 查找并删除va对应的一个RE内的所有地址

        void get(uint64_t va);

        void put(uint64_t va);

        void put_out_access(std::stack<uint64_t> &tmp)
        { // 将访问过的表项加入逐出
            while (!tmp.empty())
            {
                put(tmp.top());
                tmp.pop();
            }
        }
    };

    class ReTableCache
    {
    public:
        rt &rtsys;        // 映射系统
        uint64_t size;    // 总容量
        uint64_t set_num; // 组数
        /* rtcache */
        /* 八路组向量一次最多比较8个， */
        std::vector<std::list<RegionEntry>> LRU; // 加速lru过程，将每个组都用vector存储，list中按lru存储对应的值
        // 第一个值是va的re索引tag+re_index，第二个值是pa的region索引，第三个iterator是对应va在list中的迭代器方便插入删除
        /* 这应该是整个的地址 */
        std::unordered_map<uint64_t, std::list<RegionEntry>::iterator> RTcache;

        std::stack<uint64_t> tmp; // 临时存放逐出的键值对
        ReTableCache(rt &rtsys_, uint64_t size_) : rtsys(rtsys_), size(size_), set_num(size / rtsys.associate)
        {
            // std::cout<<size<<std::endl;
            LRU.resize(set_num);
            // for (uint64_t i = 0; i < set_num; i++)
            // {
            //     std::cout << size << std::endl;
            //     LRU.emplace_back(std::list<RegionEntry>());
            // } // 初始化逐出cache
            // std::cout<<size<<std::endl;
        }
        bool hascache(uint64_t va);

        void delete_rt(uint64_t va); // 查找并删除va对应的表项，若存在

        void get(uint64_t va);

        void put(uint64_t va);

        void print(); //
        /* 统计一个 */
    };
    inline bool isPowerOfTwo(unsigned int n)
    {
        if (n == 0)
            return false; // 0 不是2的幂
        return (n & (n - 1)) == 0;
    }

    ScariCache SC;
    ReTableCache RT;
    // ofstream &missFile;
    std::vector<uint64_t> VA_PA, PA_VA;
    // 运行时
    std::vector<uint64_t> AC_num, Dis_num; // AC_num 表示访问计数对应0-8，Dis对应 分布，用8位二进制表示,

    // 逐出时
    std::vector<uint64_t> away_AC_num, away_Dis_num; //
    // std::vector<int> leveldown;                                                 // 计算哪些是降级组合
    std::unordered_map<uint32_t, std::vector<AddrRange>> continuous_blocks_map; // 存储不同大小连续块的信息

    uint64_t total_count; // 统计总共遍历了多少个re
    uint64_t away_count;  // 逐出了多少个RE

    uint64_t total_access; // 总请求，
    uint64_t rt_hit;
    uint64_t sc_hit;
    uint64_t evict_num; // 被逐出的项数
    uint64_t down_num;
    uint64_t evict_hit; // 逐出后的访问情况
    
    uint64_t rt_in_sc_out = 0; // 逐出后的访问情况

    uint64_t rt_sc_out = 0; // 逐出后的访问情况

    uint64_t liangji_zhuchu = 0;//两级的逐出情况
    uint64_t sc_zhuchu = 0;// sc的逐出情况

    bool without_sc = false;//假设 sc 全为空
    /* 暂存被逐出的表项访问情况 */
    std::vector<std::pair<int, int>> evict_tmp;
    rt(int as, int sc_num_, int rt_num, int dram_size_, int re_size, bool pre_flag);

    bool get(uint64_t va);
    // 更新
    void update(uint64_t page1, uint64_t page2);

    inline int bitLength(unsigned int n)
    { // 计算长度
        int length = 0;
        while (n > 0)
        {
            length++;
            n >>= 1; // 右移一位
        }
        return length;
    }

    void print();      // 最终输出函数，
    void time_print(); // 间隔输出函数
    void compute_continuous_blocks();

    void wear_print(); // 磨损均衡分布
    double check_ratio()
    {
        double ratio = double(down_num) / (evict_num * (1 << RE_num));
        down_num = 0; // down_num 修改为命中率，by crc 24.9.18
        evict_num = 0;
        return ratio; //
    }
    // void  calculateLeveldownNumbers(std::unordered_map<long long,int> & leveldown,int n)
    // {
    //     long long totalNumbers = 1LL<<(1 << n);
    //     std::cout<<" totalNumbers"<<totalNumbers<<std::endl;                // 2^n 个数
    //     // leveldown.resize(totalNumbers, 0); // 初始化为0

    //     for ( long long i = 0; i < totalNumbers; ++i)
    //     {
    //          long long leftHalf = i >> (n / 2);              // 取前半部分
    //          long long rightHalf = i & ((1 << (n / 2)) - 1); // 取后半部分

    //         if (leftHalf == 0 || rightHalf == 0)
    //         {
    //             leveldown[i] = 1; // 是降级数
    //         }
    //     }

    //     // return move(leveldown);
    // }
};

#endif //__MEM_MY_CACHE_HH__