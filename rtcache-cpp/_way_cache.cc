#include "_way_cache.hh"

// std::ofstream cout_("test2.out");

bool _way_cache::cache::hascache(uint64_t va)
{
    // std::cout << " _way_cache::cache::hascache " /* << (Cache.find(va) != Cache.end()) */ <<std::endl;
    return Cache_.find(va) != Cache_.end();
}

void _way_cache::cache::delete_(uint64_t va)
{
    // std::cout << " _way_cache::cache::delete_ " << std::endl;
    if (hascache(va))
    {                                      //
        uint64_t set_index = va % set_num; //
        auto &list_ = LRU[set_index];      // 找到对应的set
        auto &it = Cache_[va];      // 对应的迭代器
        list_.erase(it);                   // 删除
        Cache_.erase(va);                  // 从map中删除
    }
}

void _way_cache::cache::get(uint64_t va)
{
    // std::cout << " rt::ScariCache::get " << std::endl;
    auto &it = Cache_[va];
    uint64_t set_index = va % set_num; // 这里的后几位属于
    auto &list_ = LRU[set_index];
    list_.splice(list_.begin(), list_, it); // 移动对应的表项到开头
    // return Cache_[va].first;                // 返回的对应的值
}

void _way_cache::cache::put(uint64_t va)
{
    // std::cout << " rt::ScariCache::put" << std::endl;
    uint64_t set_index = va % set_num; //
    auto &list_ = LRU[set_index];
    if (list_.size() >= _way_cache_.associate)
    {                                 // 需要逐出
        uint64_t old_ = list_.back(); // 返回最后的数据
        list_.pop_back();             // 删除
        Cache_.erase(old_);           // 从map中删除
    }
    list_.push_front(va);
    Cache_[va] = list_.begin();
}

/* 没有构造编译器不提示 */
/* 吸取教训，未初始化的变量很有可能造成段错误 */

_way_cache::_way_cache(int as, int num_, int dram_size_, bool pre, int pre_n) : associate(as), dram_size(dram_size_), Cache(*this, num_), pre_(pre), pre_num(pre_n)
{
    // std::cout << "_way_cache_start" << this << std::endl;
    // for (uint64_t i = 0; i < dram_size; i++)
    // {
    //     VA_PA.emplace_back(i);
    //     PA_VA.emplace_back(i);
    // }
    // if(random_start){//
    //     std::vector<uint64_t> indices(dram_size);
    //     for (uint64_t i = 0; i < dram_size; ++i)
    //     {
    //         indices[i] = i;
    //     }
    //     // 使用固定种子初始化随机数生成器
    //     std::mt19937 generator(seed);
    //     // 打乱索引数组
    //     std::shuffle(indices.begin(), indices.end(), generator);
    //     // 创建临时数组用于存储打乱后的 VA_PA 和 PA_VA
    //     std::vector<uint64_t> shuffled_VA_PA(dram_size);
    //     std::vector<uint64_t> shuffled_PA_VA(dram_size);

    //     // 重新排列 VA_PA 和 PA_VA
    //     for (uint64_t i = 0; i < dram_size; ++i)
    //     {
    //         shuffled_VA_PA[i] = VA_PA[indices[i]];
    //         shuffled_PA_VA[indices[i]] = PA_VA[i];
    //     }
    //     // 更新 VA_PA 和 PA_VA
    //     VA_PA = std::move(shuffled_VA_PA);
    //     PA_VA = std::move(shuffled_PA_VA); 
    // }
    total_access = 0;
    hit = 0;
}

_way_cache::~_way_cache()
{
    // std::cout << "~_way_cache" << this << std::endl;
}

bool _way_cache::get(uint64_t VA)
{
    // std::cout << " std::pair<bool,uint64_t> rt::get" << std::endl;
    uint64_t pa;
    total_access++;
    if (Cache.hascache(VA))
    { // cache命中
        Cache.get(VA);
        hit++;
        return true;
    }
    else
    {
        /* 加入预取机制 */
        if (pre_)
        {
            uint64_t base = VA >> 3;
            for (int i = 0; i < pre_num; i++)
            {
                uint64_t pre_addr = (base << 3) + i;
                if (!Cache.hascache(pre_addr))
                {
                    Cache.put(pre_addr);
                }
            }
        }
        else
        {
            Cache.put(VA); // 从dram中拷贝
        }
        return false;
    }
}

void _way_cache::update(uint64_t page1, uint64_t page2)
{
    // // std::cout << " rt::update" << std::endl;
    // if (page1 == page2)
    //     return;
    // if (type == 1)
    // {
    //     page1 = VA_PA[page1]; //
    //     page2 = VA_PA[page2];
    // }
    // uint64_t va_1 = PA_VA[page1]; // p1对应的虚拟地址
    // uint64_t va_2 = PA_VA[page2]; // p2对应的虚拟地址
    // // std::cout<<va_1<<" "<<VA_PA[va_1]<<" "<<VA_PA[va_2]<<std::endl;
    // std::swap(VA_PA[va_1], VA_PA[va_2]);   // 交换两个虚拟地址对应的物理地址。
    // std::swap(PA_VA[page1], PA_VA[page2]); // 交换两个pa对应的va
    // // cout_<<va_1<<" "<<VA_PA[va_1]<<" "<<VA_PA[va_2]<<std::endl;
    // /* 在逐出内查找 */

    // /* 万一两个同属一一个，进去之后有一个被逐出了，另一个就错误了 */
    if (Cache.hascache(page1))
    {
        Cache.delete_(page1);
    }
    /*  */
    if (Cache.hascache(page2))
    { /* 首先检查rt中有没有对应的re */
        Cache.delete_(page2);
    }
    /* 如果逐出内有，则rt内必然没有 */
}
void _way_cache::print()
{
    std::cerr << "8way" << std::endl;
    std::cout << "total access: " << total_access << " hit_rate " << double(hit) / total_access << std::endl;
}

