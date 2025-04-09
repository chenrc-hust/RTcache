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

