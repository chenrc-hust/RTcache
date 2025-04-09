#include "rtcache.hh"
#include <memory>

// std::ofstream cout_("test1.out");
// 从sc中逐出,负责当一个RE块要被加入rtcache时，检查sc中是否有对应的项,不预取时,只检查一个
// 不检查可能会出现两个重复的内容
// 但是查缓存时，先查两级，再查sc，
// 如果再逐出，此时会出现重复，
void rt::ScariCache::delete_re(uint64_t va)
{
    // std::cout << " rt::ScariCache::delete_re " << std::endl;
    uint64_t base_addr = (va >> rtsys.pre_length) << rtsys.pre_length;
    rtsys.rt_sc_out++;
    if (rtsys.pre_)
    { // 预取时,检查8个
        for (int i = 0; i < (1 << rtsys.pre_length); i++)
        {
            uint64_t addr = base_addr + i;
            delete_sc(addr);
        }
    }
    else
        delete_sc(va); // 不预取时逐出单个
}

// 从sc中逐出,负责当一个表项被重映射，检查sc中是否有对应的项
void rt::ScariCache::delete_sc(uint64_t va)
{
    // std::cout << " rt::ScariCache::delete_sc " << std::endl;
    auto it = SCcache.find(va);
    if (it != SCcache.end())
    {
        rtsys.rt_in_sc_out++;
        auto &list_ = LRU[va % set_num];
        vst[va] = false;
        list_.erase(it->second); // 优化为一次查找
        SCcache.erase(va);       // 从map中删除
    }
}

// sc中查询
bool rt::ScariCache::hascache(uint64_t va)
{
    // std::cout << " rt::ScariCache::hascache " << std::endl;
    return SCcache.find(va) != SCcache.end();
}

// sc中get
void rt::ScariCache::get(uint64_t va)
{
    // std::cout << " rt::ScariCache::get " << std::endl;
    auto &it = SCcache[va];
    uint64_t set_index = va % set_num; // 这里的后几位属于
    if (!vst[va])
    {
        vst[va] = true; // 设置为访问过
        rtsys.evict_hit++;
    }
    auto &list_ = LRU[set_index];

    list_.splice(list_.begin(), list_, it); // 移动对应的PE到开头
    // return SCcache[va].first;               // 返回的对应的值
}

// 加入sc或者更新
void rt::ScariCache::put(uint64_t va)
{

    // // 11-4
    // if (hascache(va))
    // {
    //     get(va); // 如果已经存在之中，那就直接get一下，就可以了。
    // }
    // else
    // {
        // std::cout << " rt::ScariCache::put" << std::endl;
        uint64_t set_index = va % set_num; // 获取组号

        auto &list_ = LRU[set_index];
        if (list_.size() >= (size/set_num))
        {                                 // 需要逐出
            // std::cout << " rt::ScariCache:: overflow" << std::endl;
            uint64_t old_ = list_.back(); // 返回最后的数据
            vst[old_] = false;            // 逐出的需要被设置为未访问
            list_.pop_back();             // 删除
            SCcache.erase(old_);          // 从map中删除
        }
        list_.push_front(va);
        SCcache[va] = {list_.begin()};
        vst[va] = false; // 设置为未访问
    // }
}

bool rt::ReTableCache::hascache(uint64_t va)
{
    // std::cout << " rt::ReTableCache::hascache" << std::endl;
    uint64_t TagAndIndex = va / (1 << rtsys.RE_num); // 除整体的部分

    /* 获得对应的实体 */

    auto it = RTcache.find(TagAndIndex);

    if (it == RTcache.end())
    { // 不存在RE
        return false;
    }
    else
    {
        int num = va % (1 << rtsys.RE_num); // 对应序号
        return it->second->able[num];       // 检查able
    }

    // std::cout<<"TagAndIndex "<<TagAndIndex<<std::endl;
    // return RTcache.find(TagAndIndex) != RTcache.end();
}
/* 逐出一整个re对应的pe */
/* 极端情况下，逐出的两个在一个*/
/* 只需要disable一个,然后检查是否全disable */
void rt::ReTableCache::delete_rt(uint64_t va)
{
    /*  */
    // std::cout << " rt::ReTableCache::delete_rt" << std::endl;
    if (hascache(va)) // 如果存在
    {                 //
        // std::cout<<"ReTableCach"<<std::endl;
        uint64_t TagAndIndex = va / (1 << rtsys.RE_num); // 除整体的部分
        uint64_t set_index = TagAndIndex % set_num;      //
        // std::cout<<"set_index "<<set_index<<std::endl;
        auto &list_ = LRU[set_index];    // 找到对应的set
        auto &it = RTcache[TagAndIndex]; // 对应的迭代器

        int num = (va % (1 << rtsys.RE_num));
        // std::cout<<"ReTableCach"<<std::endl;
        it->able[num] = false;
        // it->outAccess(tmp, (va & ((1 << rtsys.pre_length) - 1)));
        // std::cout<<"ReTableCach "<<it->TagAndIndex<<" "<<list_.size()<<std::endl;
        if (it->none())
        {                    // 都为0
            list_.erase(it); // 删除
            // std::cout<<"ReTableCach"<<std::endl;
            RTcache.erase(TagAndIndex); // 从map中删除
        }
    }
}

void rt::ReTableCache::get(uint64_t va) // 移动到最前面，设置访问位
{
    // std::cout << " rt::ReTableCache::get" << std::endl;
    uint64_t TagAndIndex = va / (1 << rtsys.RE_num); //
    auto &it = RTcache[TagAndIndex];
    uint64_t set_index = TagAndIndex % set_num;
    auto &list_ = LRU[set_index];
    list_.splice(list_.begin(), list_, it); // 移动对应的RE到开头
    it->set(va);                            // 设置访问位
    // return it->get(va);                     // 返回对应的值
}
/* 需要修改，因为可能已经存在 */
/* 只有shou */
void rt::ReTableCache::put(uint64_t va /* , const std::vector<uint64_t> &remap */)
{
    // std::cout << " rt::ReTableCache::put" << std::endl;
    uint64_t TagAndIndex = va / (1 << rtsys.RE_num); // 先除掉后面的组内序号
    /* 获得对应的实体 */
    auto it = RTcache.find(TagAndIndex);

    if (it == RTcache.end())
    { // 不存在RE
        // std::cout<<" TagAndIndex "<<TagAndIndex<<std::endl;
        uint64_t set_index = TagAndIndex % set_num; // 组号
        // std::cout << "& list_ " << set_index << std::endl;
        auto &list_ = LRU[set_index];
        if (list_.size() >= rtsys.associate)
        {
            // std::cout << "set_index " << set_index << std::endl;
            // RegionEntry * ZuiShao = nullptr;
            auto i = list_.rbegin();
            int min_time = i->count_ac();
            for (auto j = i; j != list_.rend(); j++)
            {
                if (min_time > j->count_ac())
                {
                    i = j;
                    min_time = j->count_ac();
                }
                else
                {
                    break;
                }
            }
            i->outAccess(tmp);
            uint64_t old_ = i->TagAndIndex; //
            list_.erase(std::next(i).base());
            RTcache.erase(old_);
            // list_.back().outAccess(tmp);              // 弹出被访问过的到tmp中
            // uint64_t old_ = list_.back().TagAndIndex; //
            // list_.pop_back();
            // RTcache.erase(old_); // 从map中删除
        }
        list_.push_front(RegionEntry(rtsys, va /* , remap */));
        // std::cout << "list_.push_front" << set_index << std::endl;
        RTcache[TagAndIndex] = list_.begin(); // 指向起始元素
        // std::cout<<"ReTableCach "<<RTcache[TagAndIndex]->TagAndIndex<<" "<<list_.size()<<std::endl;
    }
    else
    {                        // 存在
        it->second->put(va); // 直接添加
        // int num = va% (1 << rtsys.pre_length);//对应序号
        // return it->able.test(num);//检查able
    }
}

/* 传入参数是，相联度，逐出大小（大小/sc_size），rt大小（大小/rt_size），总内存大小（页数），预取大小（2的倍数） */
/*  25-3-6 预取应该都是8 ， */
rt::rt(int as, int sc_num_, int rt_num, int dram_size_, int re_size, bool pre_flag) : associate(as), dram_size(dram_size_),
                                                                                      SC(*this, sc_num_), RT(*this, rt_num)
{
    int total_length = bitLength(dram_size - 1); // 页号总长度,不是字节地址
    // pre_length = std::min(3, re_size);           // 一个re结构大小，二的幂表示，=3 为8个
    pre_length = 3;
    RE_num = re_size;                            // 位数
    /* 通过位数来判断是否预取 */
    pre_ = pre_flag;
    re_index_length = bitLength((rt_num / associate) - 1);
    tag_length = total_length - RE_num - re_index_length;
    // calculateLeveldownNumbers(leveldown,pre_length);//计算降级数
    // away_AC_num.resize(9, 0);
    // away_Dis_num.resize(256, 0);

    total_count = 0; // 统计总共遍历了多少个re
    away_count = 0;  // 逐出了多少个
    down_num = 0;
    total_access = 0; // 总请求，
    rt_hit = 0;
    sc_hit = 0;
    evict_hit = 0;
    evict_num = 0;
    // missFile.open("/home/chen/miss_sc", ios::out); // Overwrite the file instead of appending
    if (sc_num_ == 0)
        without_sc = true;
    std::cout << total_length << " " << RE_num << " " << re_index_length << " " << tag_length << std::endl;
}

/* 查询接口，返回一个bool ，第一个元素表示在缓存中命中，*/
bool rt::get(uint64_t va)
{
    // std::cout << " std::pair<bool,uint64_t> rt::get" << std::endl;
    uint64_t TagAndIndex = va >> RE_num; // 除预取的部分
    total_access++;
    if (RT.hascache(va)) // 先在两级里查找
    {                    // rt命中
        RT.get(va);
        rt_hit++;
        // std::cout<<"hit in rt va "<<va<<" pa "<<pa<<std::endl;
        return true;
    }
    else if (!without_sc && SC.hascache(va)) // 然后在sc里查找
    {                                        // sc命中
        sc_hit++;
        SC.get(va);
        // std::cout<<"hit in sc va "<<va<<" pa "<<pa<<std::endl;
        return true;
    }
    else
    { // 都没命中
      // if (pre_)
      // {
      //     for (int i = 0; i < (1<<pre_length); i++)
      //     {

        //         uint64_t pre_addr =((TagAndIndex + i)<<pre_length)+(va%(1<<pre_length));
        //         SC.delete_re(pre_addr);          // 删除sc中表项
        //         RT.put(pre_addr);
        //     }
        // }else{
        // 11-4 这里不需要逐出了，
        if(!without_sc)SC.delete_re(va);   //从牺牲缓存中找出，并删除，这里是不是应该再加入两级，
        // 不加入的话，在不预取的情况下会影响，
        RT.put(va); //
        // }             // 从dram中拷贝
        if (!without_sc)
            SC.put_out_access(RT.tmp); // 检查是否有被逐出的表项
                                       // std::cout<<"not hit  va "<<va<<" pa "<< VA_PA[va]<<std::endl;
        return false;
    }
}
/* 传递的都是虚拟地址  */
void rt::update(uint64_t va_1, uint64_t va_2)
{ // 更新两个键值对的关系

    /* 万一两个同属一一个，进去之后有一个被逐出了，另一个就错误了 */
    if (RT.hascache(va_1)) //
    {
        RT.delete_rt(va_1); //
        /* 要把逐出的访问过的表项加入sc */
        // SC.put_out_access(RT.tmp);
        liangji_zhuchu++;
    }
    else if (!without_sc && SC.hascache(va_1))
    {
        SC.delete_sc(va_1);
        sc_zhuchu++;
    }
    /*  */
    if (RT.hascache(va_2))
    { /* 首先检查rt中有没有对应的re */
        RT.delete_rt(va_2);
        liangji_zhuchu++;
        /* 要把逐出的访问过的表项加入sc */
        // SC.put_out_access(RT.tmp);
    }
    else if (!without_sc && SC.hascache(va_2))
    { /* 然后检查在不在sc */
        SC.delete_sc(va_2);
        sc_zhuchu++;
    }

    /* 如果逐出内有，则rt内必然没有 */
}

void rt::print()
{
    std::cout << "rt hit: " << double(rt_hit) / total_access << " sc_hit : " << double(sc_hit) / (total_access - rt_hit) << std::endl;
    std::cout << "rt_in_sc_out " << rt_in_sc_out << std::endl;
    std::cout << "rt_sc_out " << rt_sc_out << std::endl;
    std::cout << "liangji_zhuchu " << liangji_zhuchu << std::endl;
    std::cout << "sc_zhuchu " << sc_zhuchu << std::endl;
}
void rt::time_print()
{ // 定时输出 ,结束时再调用一次
    std::cout << "time print RTcache" << std::endl;
    /* 输出sc的命中率 */
    uint64_t hit_count = 0;
    u_int64_t sc_count = 0;
    // for (auto it : SC.LRU)
    // {
    //     for (auto jt : it)
    //     {
    //         sc_count++;
    //         if (SC.vst[jt])
    //             hit_count++;
    //     }
    // }
    // max_hit = std::max(hit_count, max_hit);
    // // 输出sc的当前命中数量，最大数量，利用率，命中利用率
    // std::cout << sc_count << " " << SC.size << std::endl;
    // if (sc_count) // 当sc有效条目不为零
    //     std::cout << "hit_count " << hit_count << " max_hit " << max_hit << " sc_use_rate " << double(sc_count) / SC.size
    //               << " hit_use_rate " << double(hit_count) / sc_count << std::endl;
    /* 处理RE部分的代码 */
    std::pair<int, int> re_status;
    // std::vector<uint64_t> AC_tmp(9, 0), Dis_tmp(256, 0); // 临时
    // uint64_t tmp_count = 0;                              // 当前re个数
    // for (auto it : RT.LRU)
    // { // 组外检索
    //     for (auto jt : it)
    //     { // 组内检索
    //         re_status = jt.query();
    //         tmp_count++;
    //         AC_tmp[re_status.first]++;
    //         Dis_tmp[re_status.second]++;
    //     }
    // }
    // std::cout << "tmp_count " << tmp_count << std::endl; // 这次遍历的数量
    //                                                      //  创建一个包含数组元素及其原始索引的向量
    std::vector<IndexedValue> indexed_values;
    // for (size_t i = 0; i < AC_tmp.size(); ++i)
    // {
    //     indexed_values.push_back({AC_tmp[i], i});
    // }

    // // 对向量进行排序
    // std::sort(indexed_values.begin(), indexed_values.end(), [](const IndexedValue &a, const IndexedValue &b)
    //           { return a.value < b.value; });

    // std::cout << "tmp_ac" << std::endl;
    // for (const auto &iv : indexed_values)
    // {
    //     if (iv.value)
    //         std::cout << "Value: " << iv.value << " Percent: " << double(iv.value) / tmp_count << ", Original Index: " << iv.index << std::endl;
    // }
    // indexed_values.clear();
    // // 输出一个二进制的表达式，直观。

    // for (size_t i = 0; i < Dis_tmp.size(); ++i)
    // {
    //     indexed_values.push_back({Dis_tmp[i], i});
    // }

    // // 对向量进行排序
    // std::sort(indexed_values.begin(), indexed_values.end(), [](const IndexedValue &a, const IndexedValue &b)
    //           { return a.value < b.value; });

    // std::cout << "tmp_dis" << std::endl;
    // for (const auto &iv : indexed_values)
    // {
    //     if (iv.value)
    //         std::cout << "Value: " << iv.value << " Percent: " << double(iv.value) / tmp_count << ", Original Index: " << std::bitset<8>(iv.index) << std::endl;
    // }

    // /* 记录这次的结果 */
    // total_count += tmp_count;
    // for (int i = 0; i < 9; i++)
    //     AC_num[i] += AC_tmp[i];
    // for (int i = 0; i < 256; i++)
    //     Dis_num[i] += Dis_tmp[i];

    // /* 输出逐出情况 */

    // std::cout << "time evict print RTcache" << std::endl;
    // std::vector<uint64_t> evict_AC_tmp(255, 0), evict_Dis_tmp(256, 0); // 临时
    // uint64_t evict_tmp_count = 0;
    // for (auto it : evict_tmp)
    // {
    //     evict_tmp_count++;
    //     evict_AC_tmp[it.first]++;
    //     evict_Dis_tmp[it.second]++;
    // }
    // evict_tmp.clear();                                               // 每次删除
    // std::cout << "evict_tmp_count " << evict_tmp_count << std::endl; // 这次遍历的数量
    //                                                                  //  创建一个包含数组元素及其原始索引的向量
    // indexed_values.clear();
    // for (size_t i = 0; i < evict_AC_tmp.size(); ++i)
    // {
    //     indexed_values.push_back({evict_AC_tmp[i], i});
    // }

    // // 对向量进行排序
    // std::sort(indexed_values.begin(), indexed_values.end(), [](const IndexedValue &a, const IndexedValue &b)
    //           { return a.value < b.value; });

    // std::cout << "evict_AC_tmp" << std::endl;
    // for (const auto &iv : indexed_values)
    // {
    //     if (iv.value)
    //         std::cout << "Value: " << iv.value << " Percent: " << double(iv.value) / evict_tmp_count << ", Original Index: " << iv.index << std::endl;
    // }
    // indexed_values.clear();
    // // // 输出一个二进制的表达式，直观。

    // // for (size_t i = 0; i < evict_Dis_tmp.size(); ++i)
    // // {
    // //     indexed_values.push_back({evict_Dis_tmp[i], i});
    // // }

    // // // 对向量进行排序
    // // std::sort(indexed_values.begin(), indexed_values.end(), [](const IndexedValue &a, const IndexedValue &b)
    // //           { return a.value < b.value; });

    // // std::cout << "tmp_dis" << std::endl;
    // // for (const auto &iv : indexed_values)
    // // {
    // //     if (iv.value)
    // //         std::cout << "Value: " << iv.value << " Percent: " << double(iv.value) / evict_tmp_count << ", Original Index: " << std::bitset<8>(iv.index) << std::endl;
    // // }
    // /* 记录这次的结果 */
    // away_count += evict_tmp_count;
    // for (int i = 0; i < 9; i++)
    //     away_AC_num[i] += evict_AC_tmp[i];
    // for (int i = 0; i < 256; i++)
    //     away_Dis_num[i] += evict_Dis_tmp[i];
}
