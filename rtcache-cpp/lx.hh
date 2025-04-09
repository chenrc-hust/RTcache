#ifndef __LI_XIANG_HH__
#define __LI_XIANG_HH__

#include <vector>
#include <iostream>
#include <algorithm>

class lx
{
public:
    std::vector<uint32_t> CounterMap; // 访问计数 记录dram页面以及对应的热度
    // uint64_t page_1_ago;              // 上一轮写入最多的
    // uint64_t page_2_ago;              // 上一轮写入最少的
    int wearlevel_threshold; // 页面磨损均衡阈值
    // int wearlevel_time = 0;
    int write_count;
    std::vector<uint64_t> VA_PA, PA_VA; //
    uint64_t MaxDrampage;               //
    uint64_t *ans;
    lx(uint64_t MaxDrampagenum, int swap_time) : wearlevel_threshold(swap_time), MaxDrampage(MaxDrampagenum)
    {

        for (uint64_t i = 0; i < MaxDrampage; i++)
        {
            VA_PA.emplace_back(i);
            PA_VA.emplace_back(i);
        }
        CounterMap.resize(MaxDrampage + 1, 0);
        ans = (uint64_t *)malloc(20 * sizeof(uint64_t));
        std::cout << "CounterMap complete" << std::endl;
    }
    uint64_t *access(uint64_t va, bool isWrite)
    {
        ans[0] = 0;

        if (!isWrite)
            return ans;

        CounterMap[VA_PA[va]]++; //
        write_count++;
        if (write_count != 0 && write_count % wearlevel_threshold == 0)
        {
            uint64_t min_value = std::numeric_limits<uint64_t>::max();
            uint64_t max_value = std::numeric_limits<uint64_t>::min();
            uint64_t max_ = 0, min_ = 0;
            for (uint64_t i = 0; i < MaxDrampage; i++)
            {
                if (CounterMap[i] < min_value)
                {
                    min_value = CounterMap[i];
                    min_ = i;
                }
                else if (CounterMap[i] > max_value)
                {
                    max_value = CounterMap[i];
                    max_ = i;
                }
            }
            if (max_ != min_)
            {
                CounterMap[max_] += 16;
                CounterMap[min_] += 16;
                uint64_t va_1 = PA_VA[max_];         // p1对应的虚拟地址
                uint64_t va_2 = PA_VA[min_];         // p2对应的虚拟地址
                std::swap(VA_PA[va_1], VA_PA[va_2]); // 交换两个虚拟地址对应的物理地址。
                std::swap(PA_VA[max_], PA_VA[min_]); // 交换两个pa对应的va
                ans[0] = 1;
                ans[1] = va_1;
                ans[2] = va_2;
                // return ans;
            }
        }
        return ans;
    }

    void findMinMax()
    {
        uint32_t min_value = 0;
        uint32_t max_value = 0;
        if (CounterMap.empty())
        {
            std::cerr << "CounterMap is empty!" << std::endl;
            return;
        }

        auto min_max_pair = std::minmax_element(CounterMap.begin(), CounterMap.end());
        min_value = *min_max_pair.first;
        max_value = *min_max_pair.second;
        std::cout << "Minimum value: " << min_value << std::endl;
        std::cout << "Maximum value: " << max_value << std::endl;
    }
};

#endif //__LI_XIANG_HH__