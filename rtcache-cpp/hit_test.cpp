/* 读文件，根据读写访问rtcache、8way、内存中的映射表 */
/* 第一列是 R 或 W ,第二列是地址, 机器是256G的 ， 要做相应的调整*/
/* 磨损均衡直接在这个文件内实现 */

#include "hit_test.hh"

// using namespace std;
namespace po = boost::program_options;
using namespace boost::bimaps;

struct TraceEntry
{
    char operation; // 'R' or 'W'
    uint64_t address;
};
inline int bitLength(uint64_t n)
{ // 计算长度
    int length = 0;
    while (n > 0)
    {
        length++;
        n >>= 1; // 右移一位
    }
    return length;
}
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

/* 需要输入参数，选择使用nocache ，rtcache，8way */
/* nocache 总内存地址大小以K为单位 以及待读取trace文件的路径  */
/*  */
/*  1 1 路径  */
/* 参数如下 */
/* c  w  size  random  pre  swap  print path */
/* size 单位修改为B */
int main(int argc, char **argv)
{
    // 检查参数数量
    po::options_description desc("Allowed options");
    desc.add_options()("cache-type", po::value<int>()->default_value(1), "set cache type")("wl-type", po::value<int>()->default_value(0), "set wl-type")("swap-time", po::value<int>()->default_value(2048), "swap-time")("hbm-size", po::value<int>()->default_value(8192), "set hbm size")("rt-group", po::value<int>()->default_value(8), "set rt-group option")("pre", po::value<int>()->default_value(0), "set pre value")("random-start", po::value<int>()->default_value(1), "set random value")("rt-dt", po::value<int>()->default_value(0), "set rt-dt value")("ratio", po::value<double>()->default_value(0.75), "set ratio value")("input-file", po::value<std::string>()->default_value(""), "set path value");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    int cache_type = vm["cache-type"].as<int>(); // cache类型
    int wl_type = vm["wl-type"].as<int>();       // wl 类型

    // bool increase = true;
    int swap_time = vm["swap-time"].as<int>();
    long long int hbm_size = vm["hbm-size"].as<int>();
    int random_start = vm["random-start"].as<int>(); // 默认不随机

    int rt_pre = vm["rt-group"].as<int>();
    bool set_pre = vm["pre"].as<int>() ? true : false; // 默认有预取
    bool dongtai = vm["rt-dt"].as<int>() ? true : false;

    double ratio = vm["ratio"].as<double>();
    // bool rt_change = false; // 是否rt发生调整
    int print_other = 1;
    // if (cache_type == 1 || cache_type == 2)
    //     print_other = stoi(argv[i++]); // 是否输出额外信息 也就是timeprint，目前应该都不用，
    std::string physical_path = vm["input-file"].as<std::string>();

    std::string extension;

    size_t pos = physical_path.rfind('.');
    if (pos != std::string::npos)
    {
        extension = physical_path.substr(pos + 1);
    }
    else
    {
        std::cerr << "No file extension found: " << physical_path << std::endl;
        return 0;
    }

    MaxDrampage = 1ULL * 256 * 1024 * 1024 * 1024; // 256GB对齐最大地址
    MaxDrampage /= PageSize;                       // 最大页数
    // 初始化重映射表
    //  Remappingtable.resize(MaxDrampage);
    if (cache_type != 2)
    {
        for (uint32_t i = 0; i < MaxDrampage; i++)
        {
            Remappingtable.insert({i, i});
        }
    }
    //
    if (wl_type == 1) // twl磨损均衡
        twl = new twlobj(MaxDrampage, swap_time);
    else if (wl_type == 2) // 表明是climber
        climber = new climberobj(MaxDrampage, swap_time);
    else if (wl_type == 0) // 理想
        lixiang = new lx(MaxDrampage, swap_time);
    // 需要在这里重新初始化
    std::cout << physical_path << " " << MaxDrampage << std::endl;
    if (cache_type == 0)
    {
        nocache = new no_cache(MaxDrampage);
    }
    else if (cache_type == 1) // RTcache,初始化，
    {
        long long total_size = MaxDrampage * 4096;
        int total_length = bitLength(total_size - 1);
        std::cout << "total_length " << total_length << std::endl;
        std::cout << "total_size " << total_size << std::endl;
        // 3 : 1 ,1:1 1:3 1:7  7:1
        std::cout << "hbm_size " << hbm_size << std::endl;
        long long rt_size = hbm_size * ratio;
        std::cout << "rt_size " << rt_size << std::endl;
        // long long sc_size = hbm_size - rt_size; // 3:1

        int Tag_max = total_length - 12; // 4KB为粒度
        int Tag_real = 0;
        int rt_count = 0; 
        int DA = total_length - 12;
        int RE_byte;
        int rt_group_length = static_cast<int>(log2(rt_pre));
        for (int x = Tag_max; x >= 0; x--)
        {                                                               // 设RE组号为x
            int RE_count = 1 + Tag_max - x - rt_group_length + 3 + (1 + 1 + DA) * rt_pre; // 一个RE的大小   
            // 应当再减去grouplength 的长度，比如8个一组就是3位，
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
                RE_count = 4 + Tag_real - rt_group_length + (2 + DA) * rt_pre;
                RE_byte = (RE_count % 8 == 0) ? RE_count / 8 : (RE_count / 8) + 1;
                rt_count = rt_size / RE_byte;
                rt_count = rt_count / 8 * 8;
                std::cout << Tag_real << " " << rt_count << " " << RE_byte << " RE_count " << RE_count << std::endl;
                break;
            }
        } // Tag_real 最终长度，rt_count最终数量
        //rc 应该增加相应的位数
        int RC_count = 1 + 3 + Tag_real + DA + rt_group_length ;
        int RC_byte = RC_count % 8 == 0 ? RC_count / 8 : (RC_count / 8) + 1;
        int sc_count = (hbm_size - rt_count * RE_byte) / RC_byte / 8 * 8;
        std::cout << RC_count << " " << RC_byte << " " << sc_count << std::endl;
        // rt,sc 大小都有了，
        rtcache = new rt(8, sc_count, rt_count, MaxDrampage, static_cast<int>(log2(rt_pre)), set_pre); // 增加参数rt预取个数
                                                                                                       // return 0;
    }
    else if (cache_type == 2) // SB tlb
    {
        long long total_size = MaxDrampage * 4096;
        int total_length = bitLength(total_size - 1);
        std::cout << "total_length " << total_length << std::endl;
        std::cout << "total_size " << total_size << std::endl;
        // long long hbm_size = (total_size) / 1024 / 1024; // 单位B

        // if (increase)
        //     hbm_size *= BeiShu;
        // else
        //     hbm_size /= BeiShu;

        std::cout << "hbm_size " << hbm_size << std::endl;
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

        sbcache = new SB_TLB(8, size_count, MaxDrampage, random_start, set_pre); // 测试一下预取8个的情况
    }
    else // 8set
    {
        long long total_size = MaxDrampage * 4096;
        int total_length = bitLength(total_size - 1);
        std::cout << "total_length " << total_length << std::endl;
        std::cout << "total_size " << total_size << std::endl;
        // long long hbm_size = (total_size) / 1024 / 1024; // 单位B

        // if (increase)
        //     hbm_size *= BeiShu;
        // else
        //     hbm_size /= BeiShu;

        std::cout << "hbm_size " << hbm_size << std::endl;
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

        cache = new _way_cache(8, size_count, MaxDrampage, set_pre, 8); // 测试一下预取8个的情况
    }

    std::cout << "model init complete" << std::endl;

    int round_num = 0;

    uint64_t read_count = 0;
    uint64_t total_count = 0;
    uint64_t hit_count = 0;

    std::unordered_set<uint64_t> unique_pages;

    int fd = open(physical_path.c_str(), O_RDONLY);
    if (fd == -1)
    {
        std::cerr << "Failed to open file: " << physical_path << std::endl;
        return 1;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1)
    {
        std::cerr << "Failed to get file status: " << physical_path << std::endl;
        close(fd);
        return 1;
    }

    size_t fileSize = sb.st_size;
    char *data = static_cast<char *>(mmap(nullptr, fileSize, PROT_READ, MAP_SHARED, fd, 0));
    if (data == MAP_FAILED)
    {
        std::cerr << "Failed to map file: " << physical_path << std::endl;
        close(fd);
        return 1;
    }

    size_t offset = 0;

    while (offset < fileSize)
    {

        const char *lineEnd = static_cast<const char *>(memchr(data + offset, '\n', fileSize - offset));
        // cout<<lineEnd<<endl;
        if (!lineEnd)
            break;

        std::string line(data + offset, lineEnd - (data + offset));
        std::istringstream iss(line);
        TraceEntry entry;
        std::string addr_str;

        iss >> entry.operation >> addr_str;

        entry.address = std::stoull(addr_str, nullptr, 16);
        // cout<< entry.operation<< " "<<entry.address<<endl;
        // std::cout << "Operation: " << entry.operation << ", Address: 0x"
        //           << std::hex << entry.address << std::dec << std::endl;

        offset += (lineEnd - (data + offset)) + 1;

        uint64_t dram_num = entry.address / PageSize;

        assert(dram_num <= MaxDrampage);
        // {
        //     dram_num %= MaxDrampage;
        // }

        total_count++;

        if (entry.operation == 'R')
            read_count++;

        unique_pages.insert(dram_num);

        std::pair<bool, uint64_t> remap; // 可以取消了，因为内存中保存重映射表，

        bool hit_flag = true;
        if (cache_type == 0)
        {
            // remap = nocache->get(dram_num);
            hit_flag = false;
        }
        else if (cache_type == 1)
        {
            hit_flag = rtcache->get(dram_num);
        }
        else if (cache_type == 2)
        {
            remap = sbcache->get(dram_num);
            hit_flag = remap.first;
        }
        else
        {
            hit_flag = cache->get(dram_num);
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
            dram_num = remap.second;
        } // 检查两者维护的表是否相同。
        else
        {
            dram_num = Remappingtable.left.at(dram_num); //
        }

        if (wl_type != 3)
        {
            uint64_t *ans;
            if (wl_type == 1)
                ans = twl->access(dram_num, entry.operation == 'W');
            else if (wl_type == 2)
                ans = climber->access(dram_num, entry.operation == 'W');
            else if (wl_type == 0)
                ans = lixiang->access(dram_num, entry.operation == 'W');
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
                    sbcache->update(ans[1], ans[2], 1); // sb因为要维护物理地址的连续性，所以不能取消内置的映射表
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
                else
                {
                    cache->update(ans_1, ans_3);
                    cache->update(ans_2, ans_3);
                }
                wearlevel_time += 2;
            }
        }

        if (print_other && total_count != 0 && total_count % print_threshold == 0) // 1000000次检测一次
        {
            std::cout << "Round_num: " << round_num << "  wearlevel_time: " << wearlevel_time << std::endl;

            if (cache_type == 1)
            {
                round_num++;
                // rtcache->time_print();
                if (dongtai && rtcache->evict_num > 1000000) // 直接delete原来的结构，重新new一个
                {
                    double down_level = rtcache->check_ratio();
                    int old_rt_pre = rt_pre; // 保存旧值
                    std::cout << "down_level percent: " << down_level << std::endl;
                    if (down_level < 0.1)
                    { // down_level 大于50%
                        rt_pre = std::max(2, rt_pre / 2);
                    }
                    else if (down_level > 0.8)
                    { // down_level 小于90%
                        rt_pre = std::min(32, rt_pre * 2);
                    }
                    if (rt_pre != old_rt_pre)
                    {
                        // 执行后续操作
                        // rt_change = true;
                        delete rtcache;
                        long long total_size = MaxDrampage * 4096;
                        int total_length = bitLength(total_size - 1);
                        std::cout << "total_length " << total_length << std::endl;
                        std::cout << "total_size " << total_size << std::endl;
                        std::cout << "hbm_size " << hbm_size << std::endl;
                        long long rt_size = hbm_size * ratio;
                        std::cout << "rt_size " << rt_size << std::endl;
                        // long long sc_size = hbm_size - rt_size; // 3:1

                        int Tag_max = total_length - 12; // 4KB为粒度
                        int Tag_real = 0;
                        int rt_count = 0;
                        int DA = total_length - 12;
                        int RE_byte;
                        for (int x = Tag_max; x >= 0; x--)
                        {                                                               // 设RE组号为x
                            int RE_count = 1 + Tag_max - x + 3 + (1 + 1 + DA) * rt_pre; // 一个RE的大小
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
                                RE_count = 4 + Tag_real + (2 + DA) * rt_pre;
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
                        rtcache = new rt(8, sc_count, rt_count, MaxDrampage, static_cast<int>(log2(rt_pre)), set_pre); // 增加参数rt预取个数
                                                                                                                       // return 0;
                    }
                }
                std::cout << "Now level: " << rt_pre << "  rtcache->evict_num:  " << rtcache->evict_num << std::endl;
            }
            else if (cache_type == 2)
            {
                round_num++;
                // sbcache->time_print();
                std::cout << "Footprint: " << unique_pages.size() << std::endl;
            }
            else if (cache_type == 0)
            {
                round_num++;
                // nocache->time_print();
                // std::cout << "Footprint: " << unique_pages.size() << std::endl;
            }
        }
    }
    munmap(data, fileSize);
    close(fd);

    if (cache_type == 0)
    {
        nocache->print();
    }
    else if (cache_type == 1)
    {
        rtcache->print();
    }
    else if (cache_type == 2)
    {
        sbcache->print();
    }
    else
    {
        cache->print();
    }

    if (wl_type == 0)
    {
        lixiang->findMinMax();
    }

    if (cache_type == 0)
    {
        delete nocache;
    }
    else if (cache_type == 1)
    {
        delete rtcache;
    }
    else if (cache_type == 2)
    {
        delete sbcache;
    }
    else
    {
        delete cache;
    }

    if (wl_type == 1)
        delete twl;
    else if (wl_type == 2) // 表明是climber
        delete climber;
    else if (wl_type == 0) // 理想
        delete lixiang;

    std::cout << "Footprint: " << unique_pages.size() << std::endl;
    std::cout << "read_count: " << read_count << std::endl;
    std::cout << "total_count: " << total_count << std::endl;
    std::cout << "wear_leavl : " << wearlevel_time << std::endl;
    std::cout << "hit_rate : " << double(hit_count) / total_count << std::endl;
    return 0;
}
