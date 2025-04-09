/* 读文件，根据读写访问rtcache、8way、内存中的映射表 */
/* 第一列是 R 或 W ,第二列是地址, 机器是256G的 ， 要做相应的调整*/
/* 磨损均衡直接在这个文件内实现 */

#include "hit_test.hh"

// using namespace std;
using namespace boost::bimaps;


// 封装交换函数
template <typename K, typename V>
void swap_values(bimap<K, V> &bm, K key1, K key2)
{
    if (bm.left.find(key1) != bm.left.end() && bm.left.find(key2) != bm.left.end())
    {
        // 获取当前值
        V value1 = bm.left.at(key1);
        V value2 = bm.left.at(key2);

        // 删除原有的映射
        bm.left.erase(key1);
        bm.left.erase(key2);

        // 插入新的映射
        bm.insert({key1, value2}); // 将value2插入到key1
        bm.insert({key2, value1}); // 将value1插入到key2
    }
    else
    {
        cout << "One of the keys does not exist in the bimap." << endl;
    }
}
/* 磨损均衡操作 */
HitTest::HitTest(int _cache_type, int _wl_type, int _swap_time, long long int _hbm_size,
                 int _rt_group_size, bool _pre, bool _rt_dt) : cache_type(_cache_type), wl_type(_wl_type), swap_time(_swap_time), hbm_size(_hbm_size),
                                                               rt_group_size(_rt_group_size), pre(_pre), rt_dt(_rt_dt)
{
    
    
    // 对于没有指定参数的默认设置为0
    MaxDrampage = 1ULL * 4 * 1024 * 1024 * 1024; // 4GB对齐最大地址
    MaxDrampage /= PageSize;                     // 最大页数

    long long total_size = MaxDrampage * 4096;
    int total_length = bitLength(total_size - 1);
    std::cout << "total_length " << total_length << std::endl;
    std::cout << "total_size " << total_size << std::endl;
    std::cout << "hbm_size " << hbm_size << std::endl;
    std::cout << "wl_type " << wl_type << std::endl;
    std::cout << "swap_time " << swap_time << std::endl;
    std::cout << "cache_type " << cache_type << std::endl;
    std::cout << "rt_group_size " << rt_group_size << std::endl;
    std::cout << "pre " << pre << std::endl;
    std::cout << "rt_dt " << rt_dt << std::endl;
    if (cache_type != 2)
    {
        for (uint32_t i = 0; i < MaxDrampage; i++)
        {
            Remappingtable.insert({i, i});
        }
    }
    if (wl_type == 1)
        twl = new twlobj(MaxDrampage, swap_time);
    else if (wl_type == 2) // 表明是climber
        climber = new climberobj(MaxDrampage, swap_time);
    else if (wl_type == 0) // 理想
        lixiang = new lx(MaxDrampage, swap_time);

    if (cache_type == 1) // RTcache,初始化，
    {
        long long rt_size = hbm_size * 3 / 4;
        std::cout << "rt_size " << rt_size << std::endl;
        // long long sc_size = hbm_size - rt_size; // 3:1

        int Tag_max = total_length - 12; // 4KB为粒度
        int Tag_real = 0;
        int rt_count = 0;
        int DA = total_length - 12;
        int RE_byte;
        for (int x = Tag_max; x >= 0; x--)
        {                                                                      // 设RE组号为x
            int RE_count = 1 + Tag_max - x + 3 + (1 + 1 + DA) * rt_group_size; // 一个RE的大小
            RE_byte = RE_count % 8 == 0 ? RE_count / 8 : (RE_count / 8) + 1;
            /* 8路 */
            if (RE_byte * 8 * (1LL << x) < rt_size) // 8是 8路
            {                                       // 实际上的组不一定这么多

                std::cout << ++x << std::endl;
                std::cout << "size " << RE_byte * 8 * (1LL << x) << std::endl;
                // if (RE_byte * 8 * (1LL << x) != rt_size)
                // {

                // }
                Tag_real = Tag_max - x;
                RE_count = 4 + Tag_real + (2 + DA) * rt_group_size;
                RE_byte = (RE_count % 8 == 0) ? RE_count / 8 : (RE_count / 8) + 1;
                rt_count = rt_size / RE_byte;
                rt_count = rt_count / 8 * 8;
                std::cout << Tag_real << " " << rt_count << " " << RE_byte << " RE_count " << RE_count << std::endl;
                break;
            }
        } // Tag_real 最终长度，rt_count最终数量
        int RC_count = 1 + 3 + Tag_real + DA;
        int RC_byte = RC_count % 8 == 0 ? RC_count / 8 : (RC_count / 8) + 1;
        int sc_count = (hbm_size - rt_count * RE_byte) / RC_byte / 8 * 8;
        std::cout << RC_count << " " << RC_byte << " " << sc_count << std::endl;
        // rt,sc 大小都有了，
        rtcache = new rt(8, sc_count, rt_count, MaxDrampage, static_cast<int>(log2(rt_group_size)), pre); // 增加参数rt预取个数
                                                                                                              // return 0;
    }
    else if (cache_type == 2) // SB tlb
    {
        int Tag_max = total_length - 12; // 4KB为粒度
        int Tag_real;
        int size_count = 0; // 实际条目数
        int DA = total_length - 12;
        for (int x = Tag_max; x >= 0; x--)
        {                                                                            // 设way组号为x
            int way_count = 1 + Tag_max - x + 3 + DA + 8 + 3;                        // 一个RE的大小
            int way_byte = way_count % 8 == 0 ? way_count / 8 : (way_count / 8) + 1; // 对齐到8的整数倍
            if (way_byte * 8 * (1LL << x) < hbm_size)                                // 8路
            {
                std::cout << ++x << std::endl; // 实际上的组不一定这么多
                std::cout << "size " << way_byte * 8 * (1LL << x) << std::endl;

                Tag_real = Tag_max - x;
                way_count = 4 + Tag_real + 1 + DA + 8 + 3;
                way_byte = (way_count % 8 == 0) ? way_count / 8 : (way_count / 8) + 1;
                size_count = hbm_size / way_byte;
                size_count = size_count / 8 * 8;
                std::cout << Tag_real << " " << size_count << " " << way_byte << " tlb_count " << way_count << std::endl;
                break;
            }
        } // Tag_real 最终长度，rt_count最终数量
        sbcache = new SB_TLB(8, size_count, MaxDrampage, 1, pre);
    }
    else  if (cache_type == 3) // 8set
    {
        int Tag_max = total_length - 12; // 4KB为粒度
        int Tag_real;
        int size_count = 0; // 实际条目数
        int DA = total_length - 12;
        for (int x = Tag_max; x >= 0; x--)
        { // 设way组号为x
            // DA = total_length - (Tag_max - x);                     // DA长度
            int way_count = 1 + Tag_max - x + 3 + DA; // 一个RE的大小
            int way_byte = way_count % 8 == 0 ? way_count / 8 : (way_count / 8) + 1;
            /* 8路 */
            if (way_byte * 8 * (1LL << x) < hbm_size) // 8路
            {
                std::cout << ++x << std::endl; // 实际上的组不一定这么多
                std::cout << "size " << way_byte * 8 * (1LL << x) << std::endl;

                Tag_real = Tag_max - x;
                way_count = 4 + Tag_real + 1 + DA;
                way_byte = (way_count % 8 == 0) ? way_count / 8 : (way_count / 8) + 1;
                size_count = hbm_size / way_byte;
                size_count = size_count / 8 * 8;
                std::cout << Tag_real << " " << size_count << " " << way_byte << " RE_count " << way_count << std::endl;
                break;
            }
        } // Tag_real 最终长度，rt_count最终数量
        cache = new _way_cache(8, size_count, MaxDrampage, pre, 8);
    }
    // gem5::registerExitCallback([this]() { HitTest::~HitTest(); });
    std::cout << "model init complete" << std::endl;
}

std::pair<bool, int> HitTest::access(bool isWrite, uint64_t address)
{
    total_count++;
    uint64_t page_num = address / PageSize;
    assert(page_num <= MaxDrampage);
    bool hit_flag = true;
    int wearlevel_time = 0;
    std::pair<bool, uint64_t> answer;
    if (cache_type == 0)//nocache 永远不命中
    {
        hit_flag = false;
    }
    else if (cache_type == 1)
    {
        hit_flag = rtcache->get(page_num);
    }
    else if (cache_type == 2)
    {
        answer = sbcache->get(page_num);
        hit_flag = answer.first;
    }
    else
    {
        hit_flag = cache->get(page_num);
    }

        if (hit_flag)
            hit_count++;
        /* twl 和cache关系
            cache中存储了实际的重映射关系，如果命中了就不需要再访问映射表，但还是要访存
            所以先由cache访问，然后再给地址到twl，如果twl返回要进行磨损均衡，
            那么给 cache一个update ，twl返回的是映射前的地址
            所以我给cache的应该是update(VA_PA[])
            根据当前的wear 不同选择不同的update方式吧
         */

        // dram_num 对应实际访问的页面
        //  dram_num = Remappingtable.left.at(dram_num);//
        if (cache_type == 2) // 使用tlb维护的地址
        {
            // assert(dram_num ==remap.second);
            page_num = answer.second;
        } // 检查两者维护的表是否相同。
        else
        {
            page_num = Remappingtable.left.at(page_num); //
        }

    if (wl_type != 3)
    {
        uint64_t *ans = nullptr;
        if (wl_type == 1)
            ans = twl->access(page_num, isWrite);
        else if (wl_type == 2)
            ans = climber->access(page_num, isWrite);
        else if (wl_type == 0)
            ans = lixiang->access(page_num, isWrite);
        uint32_t ans_1, ans_2, ans_3;
            if (ans[0] == 1) //
            {

                if (cache_type != 2)
                {
                    ans_1 = Remappingtable.right.at(ans[1]);
                    ans_2 = Remappingtable.right.at(ans[2]);
                    // 更新内存中的映射表，
                    swap_values(Remappingtable, ans_1, ans_2);
                }
                if (cache_type == 0)
                {

                    // nocache->update(ans[1], ans[2], 1);
                }
                else if (cache_type == 1)
            {
                    rtcache->update(ans_1, ans_2);
            }
            else if (cache_type == 2)
            {
                sbcache->update(ans[1], ans[2], 1);
            }
                else
            {
                    cache->update(ans_1, ans_2);
            }
            wearlevel_time++;
        }
        else if (ans[0] == 2)
        {
                if (cache_type != 2)
                {
                    ans_1 = Remappingtable.right.at(ans[1]);
                    ans_2 = Remappingtable.right.at(ans[2]);
                    ans_3 = Remappingtable.right.at(ans[3]);
                    swap_values(Remappingtable, ans_1, ans_2);
                    swap_values(Remappingtable, ans_2, ans_3);
                }
                if (cache_type == 0)
                {
                    // nocache->update(ans[1], ans[3], 1);
                    // nocache->update(ans[2], ans[3], 1);
                }
                else if (cache_type == 1)
                {
                    rtcache->update(ans_1, ans_3);
                    rtcache->update(ans_2, ans_3);
            }
            else if (cache_type == 2)
            {
                sbcache->update(ans[1], ans[3], 1);
                sbcache->update(ans[2], ans[3], 1);
            }
            else if (cache_type == 3)
            {
                    cache->update(ans_1, ans_3);
                    cache->update(ans_2, ans_3);
            }
            wearlevel_time += 2;
        }
    }

    if (total_count != 0 && total_count % print_threshold == 0) // 1000000次检测一次
    {
        std::cout << "Round_num: " << round_num << std::endl;

        if (cache_type == 1)
        {
            round_num++;
            if (rt_dt && rtcache->evict_num > 1000000) // 直接delete原来的结构，重新new一个
            {
                double down_level = rtcache->check_ratio();
                int old_rt_group_size = rt_group_size; // 保存旧值
                std::cout << "down_level percent: " << down_level << std::endl;
                if (down_level < 0.1)
                { // down_level 大于50%
                    rt_group_size = std::max(2, rt_group_size / 2);
                }
                else if (down_level > 0.8)
                { // down_level 小于90%
                    rt_group_size = std::min(32, rt_group_size * 2);
                }
                if (rt_group_size != old_rt_group_size)
                {
                    // 执行后续操作
                    // rt_change = true;
                    delete rtcache;
                    long long total_size = MaxDrampage * 4096;
                    int total_length = bitLength(total_size - 1);
                    std::cout << "total_length " << total_length << std::endl;
                    std::cout << "total_size " << total_size << std::endl;
                    std::cout << "hbm_size " << hbm_size << std::endl;
                    long long rt_size = hbm_size * 3 / 4;
                    std::cout << "rt_size " << rt_size << std::endl;
                    // long long sc_size = hbm_size - rt_size; // 3:1

                    int Tag_max = total_length - 12; // 4KB为粒度
                    int Tag_real = 0;
                    int rt_count = 0;
                    int DA = total_length - 12;
                    int RE_byte;
                    for (int x = Tag_max; x >= 0; x--)
                    {                                                               // 设RE组号为x
                        int RE_count = 1 + Tag_max - x + 3 + (1 + 1 + DA) * rt_group_size; // 一个RE的大小
                        RE_byte = RE_count % 8 == 0 ? RE_count / 8 : (RE_count / 8) + 1;
                        /* 8路 */
                        if (RE_byte * 8 * (1LL << x) < rt_size) // 8是 8路
                        {                                       // 实际上的组不一定这么多

                            std::cout << ++x << std::endl;
                            std::cout << "size " << RE_byte * 8 * (1LL << x) << std::endl;
                            // if (RE_byte * 8 * (1LL << x) != rt_size)
                            // {

                            // }
                            Tag_real = Tag_max - x;
                            RE_count = 4 + Tag_real + (2 + DA) * rt_group_size;
                            RE_byte = (RE_count % 8 == 0) ? RE_count / 8 : (RE_count / 8) + 1;
                            rt_count = rt_size / RE_byte;
                            rt_count = rt_count / 8 * 8;
                            std::cout << Tag_real << " " << rt_count << " " << RE_byte << " RE_count " << RE_count << std::endl;
                            break;
                        }
                    } // Tag_real 最终长度，rt_count最终数量
                    int RC_count = 1 + 3 + Tag_real + DA;
                    int RC_byte = RC_count % 8 == 0 ? RC_count / 8 : (RC_count / 8) + 1;
                    int sc_count = (hbm_size - rt_count * RE_byte) / RC_byte / 8 * 8;
                    std::cout << RC_count << " " << RC_byte << " " << sc_count << std::endl;
                    // rt,sc 大小都有了，
                    rtcache = new rt(8, sc_count, rt_count, MaxDrampage, static_cast<int>(log2(rt_group_size)), pre); // 增加参数rt预取个数
                                                                                                                   // return 0;
                }
            }
            std::cout << "Now level: " << rt_group_size << "  rtcache->evict_num:  " << rtcache->evict_num << std::endl;
        }
    }
    return {hit_flag,wearlevel_time};
}

HitTest::~HitTest(){
    if (cache_type == 1)
    {
        rtcache->print();
        delete rtcache;
    }
    else if (cache_type == 2)
    {
        delete sbcache;
    }
    else if(cache_type == 3)
    {
        delete cache;
    }

    if (wl_type == 1)
       delete twl;
    else if (wl_type == 2)
       delete climber;
    else if (wl_type == 0)
       delete lixiang;
    std::cout << "hit_rate : " << double(hit_count) / total_count << std::endl;
    std::cout<<"~HitTest"<<std::endl;

}
