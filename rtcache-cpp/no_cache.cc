#include "no_cache.hh"

std::pair<bool, uint64_t> no_cache::get(uint64_t VA)
{
    return {false, VA_PA[VA]};
}
void no_cache::update(uint64_t page1, uint64_t page2,int type) // 交换物理映射
{
    if (page1 == page2)
        return;
    /* type = 1,表示接受的是两个VA ，= 0 是两个PA*/
    // std::cout << " rt::update" << std::endl;
    if(type==1){
        page1 = VA_PA[page1];//
        page2 = VA_PA[page2];
    }
    uint64_t va_1 = PA_VA[page1]; // p1对应的虚拟地址
    uint64_t va_2 = PA_VA[page2]; // p2对应的虚拟地址
    // std::cout<<va_1<<" "<<VA_PA[va_1]<<" "<<VA_PA[va_2]<<std::endl;
    std::swap(VA_PA[va_1], VA_PA[va_2]);   // 交换两个虚拟地址对应的物理地址。
    std::swap(PA_VA[page1], PA_VA[page2]); // 交换两个pa对应的va
                                           // cout_<<va_1<<" "<<VA_PA[va_1]<<" "<<VA_PA[va_2]<<std::endl;
                                           /* 在逐出内查找 */
}
void no_cache::compute_continuous_blocks()
{
    uint64_t start_vpage = 0;
    std::cout << "calculate start " << std::endl;
    uint64_t start_ppage;
    std::vector<uint64_t> & virtual_to_physical = VA_PA;

    start_ppage = get(start_vpage).second;

    std::cout << "calculate start" << std::endl;

    uint64_t current_vpage = start_vpage;
    uint64_t current_ppage = start_ppage;
    uint64_t block_size = 1;
    std::cout << "calculate data out" << std::endl;
    for (uint64_t i = 0; i < MaxDrampage; ++i)
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
void no_cache::print()
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

void no_cache::time_print(){
    std::cout<<countNonContiguousAddresses(VA_PA)<<std::endl;
}