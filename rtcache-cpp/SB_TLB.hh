#include <iostream>
#include <unordered_map>
#include <vector>
#include <list>
#include <algorithm>
#include <cmath>
#include <random>
class SB_TLB
{
private:
    uint64_t mask_9 = ~((1ULL << 9) - 1); // 屏蔽后9位
    uint64_t mask_6 = ~((1ULL << 6) - 1);
    uint64_t mask_3 = ~((1ULL << 3) - 1);
    const int seed = 12345;
public:
    struct AddrRange
    {
        uint64_t start;
        uint64_t end;
    };
    int associate;         // 组大小
    int tag_length;        // tag_位数
    int dram_size;         // 内存总页数'
    uint64_t total_access; // 总请求，
    uint64_t miss;
    const int numEntries = 8; // 8个为一组
    class PE
    { // 映射表表项，
    public:
        uint64_t PFN;
        int valid;
        int size;
        // PE(uint64_t page_, int val_, int size_) : PFN(page_), valid(val_), size(size_){};
    };
    class cache
    {
    public:
        SB_TLB &_SB_TLB_;
        uint64_t size;                                                                             // 总容量
        uint64_t set_num;        
        bool pre_;                                                                  // 组大小
        std::vector<std::list<uint64_t>> LRU;                                                      // 存放的是虚拟地址，
        std::unordered_map<uint64_t, std::pair<const PE *, std::list<uint64_t>::iterator>> Cache_; // 虚拟地址对应的PE指针
        cache(SB_TLB &SB_TLB_, uint64_t size_,bool pre) : _SB_TLB_(SB_TLB_), size(size_),
                                                 set_num(size / _SB_TLB_.associate),pre_(pre)
        {
            LRU.resize(set_num);
        }

        bool hascache(uint64_t va);

        void put(uint64_t va, const PE *pe_); // 放置的时候也是放置最大的

        void delete_(uint64_t va);

        void delete_group(uint64_t va);

        void put_front(uint64_t va);

        std::pair<bool, uint64_t> get_group(uint64_t va);
    };

    cache Cache;
    void build();
    /* 设计一个函数，检查8个4KB的连续性， */
    /* 实际上只有32KB，256KB，2MB的开始, */
    /* 之前加入过的小的都不影响 */
    std::vector<PE> PEtable;
    std::vector<uint64_t> VA_PA, PA_VA;                                         // 内存中的映射表
    std::unordered_map<uint32_t, std::vector<AddrRange>> continuous_blocks_map; // 存储不同大小连续块的信息
    SB_TLB(int as, int num_, int dram_size_,int random_start,bool pre);                                   // 构造函数，指定程序是随机开始，还是顺序开始，指定程序的相连度，
    
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
    void update_PEtable(uint64_t va);
    void update(uint64_t page1, uint64_t page2,int type);

    void print();

    void compute_continuous_blocks();

    void wear_print(); // 磨损均衡分布
    void time_print();
};
