#include "SB_TLB.hh"

bool SB_TLB::cache::hascache(uint64_t va)
{
    return Cache_.find(va) != Cache_.end();
}
void SB_TLB::cache::put(uint64_t va, const PE *pe_)
{
    uint64_t set_index = va % set_num;
    auto &list_ = LRU[set_index];
    if (list_.size() >= _SB_TLB_.associate)
    { // 假如 满了
        uint64_t old_ = list_.back();
        list_.pop_back();
        Cache_.erase(old_);
    }
    list_.push_front(va);
    Cache_[va] = {pe_, list_.begin()}; // 加入开头
}

void SB_TLB::cache::delete_(uint64_t va)
{
    uint64_t set_index = va % set_num; //
    auto &list_ = LRU[set_index];      // 找到对应的set
    auto &it = Cache_[va].second;      // 对应的迭代器
    list_.erase(it);                   // 删除
    Cache_.erase(va);                  // 从map中删除
}
void SB_TLB::cache::delete_group(uint64_t va)
{
    /* 找到和va相关的，有效的表项删除， */
    uint64_t base_512 = va & _SB_TLB_.mask_9;
    uint64_t base_64 = va & _SB_TLB_.mask_6;
    uint64_t base_8 = va & _SB_TLB_.mask_3;
    uint64_t mod_512 = (va % (1ULL << 9)) >> 6; // 取前3位
    uint64_t mod_64 = (va % (1ULL << 6)) >> 3;  // 取中间3位
    uint64_t mod_8 = va % (1ULL << 3);
    const std::vector<PE> &PEtable = _SB_TLB_.PEtable;

    if (hascache(base_512) && (PEtable[base_512].size == 3 || (PEtable[base_512].size == 2 && (1 << mod_512) & PEtable[base_512].valid))) // 满级或者有效位为1
    {
        delete_(base_512);
    }

    if (hascache(base_64) && (PEtable[base_64].size == 2 || (PEtable[base_64].size == 1 && (1 << mod_64) & PEtable[base_64].valid)))
    {
        delete_(base_64);
    }
    if (hascache(base_8) && (PEtable[base_8].size == 1 || ((1 << mod_8) & PEtable[base_8].valid)))
    {
        delete_(base_8);
    }
    if (hascache(va))
    {
        delete_(va);
    }
}

void SB_TLB::cache::put_front(uint64_t va)
{ // lru 控制，放置到最前端。
    auto &it = Cache_[va].second;
    uint64_t set_index = va % set_num; // 这里的后几位属于
    auto &list_ = LRU[set_index];
    list_.splice(list_.begin(), list_, it); // 移动对应的表项到开头
}

std::pair<bool, uint64_t> SB_TLB::cache::get_group(uint64_t va)
{
    /* 找到和va相关的，有效的表项删除， */
    uint64_t base_512 = va & _SB_TLB_.mask_9;
    uint64_t base_64 = va & _SB_TLB_.mask_6;
    uint64_t base_8 = va & _SB_TLB_.mask_3;
    uint64_t mod_512 = (va % (1ULL << 9)) >> 6; // 取中间3位
    uint64_t mod_64 = (va % (1ULL << 6)) >> 3;  // 取中间3位
    uint64_t mod_8 = va % (1ULL << 3);
    const std::vector<PE> &PEtable = _SB_TLB_.PEtable;
    _SB_TLB_.total_access++;
    if (hascache(base_512) && (PEtable[base_512].size == 3 || (PEtable[base_512].size == 2 && (1 << mod_512) & PEtable[base_512].valid))) // 满级或者有效位为1
    {                                                                                                                                     // 512 hit
        put_front(base_512);
        // std::cout << "hit base_512" << std::endl;
        return {true, PEtable[base_512].PFN + (va % 512)};
    }
    else if (hascache(base_64) && (PEtable[base_64].size == 2 || (PEtable[base_64].size == 1 && (1 << mod_64) & PEtable[base_64].valid)))
    { // 64 hit
        put_front(base_64);
        // std::cout << "hit base_64" << std::endl;

        return {true, PEtable[base_64].PFN + (va % 64)};
    }
    else if (hascache(base_8) && (PEtable[base_8].size == 1 || ((1 << mod_8) & PEtable[base_8].valid)))
    { // 8 hit
        put_front(base_8);
        // std::cout << "hit base_8" << std::endl;

        return {true, PEtable[base_8].PFN + (va % 8)};
    }
    else if (hascache(va)) // 1hit
    {
        put_front(va);
        // std::cout << "hit va" << std::endl;

        return {true, PEtable[va].PFN};
    }
    else // 未命中的情况下要放入最大的页面组
    {    // 都没有查到，miss
        _SB_TLB_.miss++;
        // std::cout << "miss" << std::endl;
        if (pre_)
        {
            uint64_t base = va >> 3;
            for (int i = 0; i < 8; i++)//预取八个
            {
                uint64_t pre_addr = (base << 3) + i;
                if (!hascache(pre_addr)&&pre_addr!=va)
                {
                    put(pre_addr, &PEtable[pre_addr]);
                }
            }
        }
        if ((PEtable[base_512].size == 3 || (PEtable[base_512].size == 2 && (1 << mod_512) & PEtable[base_512].valid))) // 满级或者有效位为1
        {                                                                                                               // 512 hit
            put(base_512, &PEtable[base_512]);
            // std::cout << "put base_512" << std::endl;
            return {false, PEtable[base_512].PFN + (va % 512)};
        }
        else if ((PEtable[base_64].size == 2 || (PEtable[base_64].size == 1 && (1 << mod_64) & PEtable[base_64].valid)))
        { // 64 hit
            put(base_64, &PEtable[base_64]);
            // std::cout << "put base_64" << std::endl;

            return {false, PEtable[base_64].PFN + (va % 64)};
        }
        else if ((PEtable[base_8].size == 1 || ((1 << mod_8) & PEtable[base_8].valid)))
        { // 8 hit
            put(base_8, &PEtable[base_8]);
            // std::cout << "put base_8" << std::endl;

            return {false, PEtable[base_8].PFN + (va % 8)};
        }
        else
        {
            put(va, &PEtable[va]); //
            // std::cout << "put va" << std::endl;
            return {false, PEtable[va].PFN};
        }
    }
}
void SB_TLB::build()
{
    // std::cout << "SB_TLB_start build" << this << std::endl;
    // 初始化 PEtable
    PEtable.resize(dram_size);

    // 常量定义
    const int numEntries = 8;

    // 从非基页开始 页号后3位非000的处理
    for (size_t i = 0; i < dram_size; i++)
    {
        PEtable[i].PFN = VA_PA[i];
        PEtable[i].size = 0;
        PEtable[i].valid = 0;
    }

    // 页号后3位000  32KB处理
    for (size_t i = 0; i < dram_size; i += numEntries)
    {
        uint64_t base_addr = VA_PA[i];
        int valid_ = 1;
        for (int j = 1; j < numEntries; j++)
        {
            if (base_addr + j == VA_PA[i + j])
            {
                valid_ |= (1 << j);
            }
        }
        if (valid_ == (1 << numEntries) - 1)
        {
            PEtable[i].size = 1;
            PEtable[i].valid = 1; // 表示当前的有效个数
        }
        else
        {
            PEtable[i].size = 0;
            PEtable[i].valid = valid_;
        }
    }

    // 页号后6位非000，256KB处理
    for (size_t i = 0; i < dram_size; i += numEntries * numEntries)
    {
        if (PEtable[i].size == 0)
            continue;
        uint64_t base_addr = VA_PA[i];
        int valid_ = 1;
        for (int j = 1; j < numEntries; j++)
        {
            if (base_addr + j * numEntries == VA_PA[i + j * numEntries] && PEtable[i + j * numEntries].size)
            {
                valid_ |= (1 << j);
            }
        }
        if (valid_ == (1 << numEntries) - 1)
        {
            PEtable[i].size = 2;
            PEtable[i].valid = 1;
        }
        else
        {
            PEtable[i].size = 1; // 降级
            PEtable[i].valid = valid_;
        }
    }

    // 页号后9位非000 2MB处理
    for (size_t i = 0; i < dram_size; i += numEntries * numEntries * numEntries)
    {
        if (PEtable[i].size <= 1)
            continue;
        uint64_t base_addr = VA_PA[i];
        int valid_ = 1;
        for (int j = 1; j < numEntries; j++)
        {
            if (base_addr + j * numEntries * numEntries == VA_PA[i + j * numEntries * numEntries] && PEtable[i + j * numEntries * numEntries].size)
            {
                valid_ |= (1 << j);
            }
        }
        if (valid_ == (1 << numEntries) - 1)
        {
            PEtable[i].size = 3;
            PEtable[i].valid = 1;
        }
        else
        {
            PEtable[i].size = 2;
            PEtable[i].valid = valid_;
        }
    }
}

std::pair<bool, uint64_t> SB_TLB::get(uint64_t va)
{
    return Cache.get_group(va);
}

SB_TLB::SB_TLB(int as, int num_, int dram_size_, int random_start,bool pre) : associate(as), dram_size(dram_size_), Cache(*this, num_,pre)
{
    // std::cout << "SB_TLB_start " << this << std::endl;
    for (uint64_t i = 0; i < dram_size; i++)
    {
        VA_PA.emplace_back(i);
        PA_VA.emplace_back(i);
    }
    if (random_start)
    {
        std::vector<uint64_t> indices(dram_size);
        for (uint64_t i = 0; i < dram_size; ++i)
        {
            indices[i] = i;
        }
        // 使用固定种子初始化随机数生成器
        std::mt19937 generator(seed);
        // 打乱索引数组
        std::shuffle(indices.begin(), indices.end(), generator);
        // 创建临时数组用于存储打乱后的 VA_PA 和 PA_VA
        std::vector<uint64_t> shuffled_VA_PA(dram_size);
        std::vector<uint64_t> shuffled_PA_VA(dram_size);

        // 重新排列 VA_PA 和 PA_VA
        for (uint64_t i = 0; i < dram_size; ++i)
        {
            shuffled_VA_PA[i] = VA_PA[indices[i]];
            shuffled_PA_VA[indices[i]] = PA_VA[i];
        }
        // 更新 VA_PA 和 PA_VA
        VA_PA = std::move(shuffled_VA_PA);
        PA_VA = std::move(shuffled_PA_VA);
    }
    total_access = 0;
    miss = 0;
    build();
}
/* 由于需要交换的两个页面可能在同一组内，同一级别需要同时进行，第二个交换的如果在同一个组会重新计算一遍 */
void SB_TLB::update_PEtable(uint64_t va)
{

    uint64_t base_512 = va & mask_9;
    uint64_t base_64 = va & mask_6;
    uint64_t base_8 = va & mask_3; // 对应32KB的基地址
    // 从size = 0 开始
    // // std::cout << base_512 << " " << base_64 << " " << base_8 << std::endl;
    PEtable[va].PFN = VA_PA[va];
    PEtable[va].size = 0;
    PEtable[va].valid = 0;

    // uint64_t base_addr = VA_PA[va]; // 物理基地址

    // size = 1;
    int valid_ = 1;
    // 页号后3位000  32KB处理
    uint64_t base_addr = VA_PA[base_8];
    for (size_t i = 1; i < numEntries; i++)
    {

        if (base_addr + i == VA_PA[i + base_8])
        {
            valid_ |= (1 << i);
        }
    }
    // std::cout << valid_ << std::endl;
    if (valid_ == (1 << numEntries) - 1)
    {
        PEtable[base_8].size = 1;
        PEtable[base_8].valid = 1; // 表示当前的有效个数
    }
    else
    {
        PEtable[base_8].size = 0;
        PEtable[base_8].valid = valid_;
    }

    // 页号后6位非000，256KB处理，条件是base64已经是一个合并好的32KB组
    valid_ = 1;
    base_addr = VA_PA[base_64];
    if (PEtable[base_64].size)
    {
        for (size_t i = 1; i < numEntries; i++)
        {
            if ((base_addr + i * numEntries == VA_PA[base_64 + i * numEntries]) && PEtable[base_64 + i * numEntries].size) // 对应的8个必须连续
            {
                valid_ |= (1 << i);
            }
        }
        if (valid_ == (1 << numEntries) - 1)
        {
            PEtable[base_64].size = 2;
            PEtable[base_64].valid = 1;
        }
        else
        {
            PEtable[base_64].size = 1; // 降级
            PEtable[base_64].valid = valid_;
        }
        // std::cout << valid_ << std::endl;
    }

    valid_ = 1;
    // 页号后9位非000 2MB处理
    base_addr = VA_PA[base_512];
    if (PEtable[base_512].size > 1)
    {
        for (size_t i = 1; i < numEntries; i++)
        {
            if (base_addr + i * numEntries * numEntries == VA_PA[base_512 + i * numEntries * numEntries] && PEtable[base_512 + i * numEntries * numEntries].size > 1)
            {
                valid_ |= (1 << i);
            }
        }

        if (valid_ == (1 << numEntries) - 1)
        {
            PEtable[base_512].size = 3;
            PEtable[base_512].valid = 1;
        }
        else
        {
            PEtable[base_512].size = 2;
            PEtable[base_512].valid = valid_;
        }
    }

    // std::cout << valid_ << std::endl;
}

void SB_TLB::update(uint64_t page1, uint64_t page2, int type)
{
    if (page1 == page2)
        return;
    if (type == 1)
    {
        page1 = VA_PA[page1]; //
        page2 = VA_PA[page2];
    }
    uint64_t va_1 = PA_VA[page1]; // p1对应的虚拟地址
    uint64_t va_2 = PA_VA[page2]; // p2对应的虚拟地址

    Cache.delete_group(va_1); // 删除相关的项
    Cache.delete_group(va_2); // 删除相关的项
    // std::cout<<va_1<<" "<<VA_PA[va_1]<<" "<<VA_PA[va_2]<<std::endl;
    std::swap(VA_PA[va_1], VA_PA[va_2]);   // 交换两个虚拟地址对应的物理地址。
    std::swap(PA_VA[page1], PA_VA[page2]); // 交换两个pa对应的va

    /* 开始build流程， */
    // std::cout << "SB_TLB_start update" << this << std::endl;
    update_PEtable(va_1);
    update_PEtable(va_2);
}
void SB_TLB::print()
{
    // for (int i = 0; i < dram_size; i++)
    // {
    //     std::cout << PEtable[i].PFN << " " << PEtable[i].size << " " << PEtable[i].valid << std::endl;
    // }
    std::cout << "TLB" << std::endl;
    std::cout << "total access: " << total_access << " hit_rate " << (double(total_access) - miss) / total_access << std::endl;
}

void SB_TLB::compute_continuous_blocks()
{
    uint64_t start_vpage = 0;
    std::cout << "calculate start " << std::endl;
    uint64_t start_ppage;
    std::vector<uint64_t> &virtual_to_physical = VA_PA;

    start_ppage = virtual_to_physical[start_vpage];

    uint64_t current_vpage = start_vpage;
    uint64_t current_ppage = start_ppage;
    uint64_t block_size = 1;
    std::cout << "calculate data out" << std::endl;
    for (uint64_t i = 0; i < dram_size; ++i)
    {
        if (i == 0)
            continue; // 跳过第0页，因为已经初始化
        if (virtual_to_physical[i] == current_ppage + 1)
        {
            // 物理页连续
            current_ppage = virtual_to_physical[i];
            block_size++;
        }
        else
        {
            // 物理页不连续，记录前一个连续块
            continuous_blocks_map[block_size].push_back({start_vpage, current_vpage});
            // 开始新的连续块
            start_vpage = i;
            start_ppage = virtual_to_physical[i];
            current_ppage = virtual_to_physical[i];
            block_size = 1;
        }
        current_vpage = i;
    }

    // 最后一个块
    continuous_blocks_map[block_size].push_back({start_vpage, current_vpage});
}
void SB_TLB::wear_print()
{
    compute_continuous_blocks();
    std::cout << "calculate over" << std::endl;
    // if(is_RTcache){
    //     rtcache->print();
    // }else if(!no_cache){

    // }
    for (const auto &pair : continuous_blocks_map)
    {
        uint64_t block_size = pair.first;
        const auto &blocks = pair.second;
        std::cout << "Block size: " << block_size << std::endl;
        for (const auto &block : blocks)
        {
            std::cout << "  Start: " << block.start << ", End: " << block.end << std::endl;
        }
    }
    std::cout << " printexitSimLoop" << std::endl;
}
void SB_TLB::time_print()
{
    std::cout << countNonContiguousAddresses(VA_PA) << std::endl;
}
/* int main()
{
    SB_TLB A(8, 8, 1024 * 1024);

    // auto it = A.get(10);
    for (int i = 0; i < 1024 ; i++)
    {
        auto it = A.get(std::max(1024*1024-1,i+1024));
        //std::cout << it.first << " " << it.second << " i " << i << " " << memory_.VA_PA[i] << std::endl;
        A.update(i, 1024 * 1024 - i-2234);
    }
    for (int i = 0; i < 1024 +10000; i++)
    {
        auto it = A.get(std::max(1024*1024-1,i+1024));
        //std::cout << it.first << " " << it.second << " i " << i << " " << memory_.VA_PA[i] << std::endl;
        A.update(i, 1024 * 1024 - i-2234);
    }
    // A.print();
    A.wear_print();
} */