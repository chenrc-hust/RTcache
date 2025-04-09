#ifndef _HIT_TEST_HH__
#define _HIT_TEST_HH__
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_set>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
// #include <boost/program_options.hpp>
#include <boost/bimap.hpp>
#include "_way_cache.hh"
#include "rtcache.hh"
// #include "no_cache.hh"
#include "SB_TLB.hh"
#include "twl.h"
#include "climber.h"
#include "lx.hh"
#include "sim/sim_exit.hh"


class rt;
class _way_cache;
// class no_cache;
class SB_TLB;
class twlobj;
class climberobj;
class lx;


class HitTest{
    private:    
        int cache_type;
        int wl_type;
        int swap_time;
        long long int hbm_size;
        int rt_group_size;
        bool pre;
        bool rt_dt;

        twlobj *twl = nullptr;//指向twl磨损均衡

        climberobj *climber= nullptr;

        lx *lixiang= nullptr;//指向理想磨损均衡

        rt *rtcache= nullptr;// 指向rtcache模块

        _way_cache *cache= nullptr;

        SB_TLB *sbcache= nullptr;

        const uint64_t PageSize = 4096;

        uint64_t MaxDrampage;
        int print_threshold = 1000000;//动态调整阈值
        uint64_t round_num =0;//轮次
        uint64_t total_count =0;//总指令数
         uint64_t hit_count =0;//指令命中数
         boost::bimaps::bimap<uint32_t,uint32_t>Remappingtable;
    public:
        //gem5 tlb 默认随机，其他不随机， 第一个参数是cache类型
        //磨损均衡类型 ,hbm大小,rt 组大小，预取 , rt 是否动态调整
        HitTest(int _cache_type,int _wl_type,int _swap_time,long long int _hbm_size,
                int _rt_group_size, bool _pre ,bool _rt_dt
            );
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
        std::pair<bool, int>  access(bool isWrite,uint64_t address);//返回是否命中以及磨损均衡触发次数
        ~HitTest();
};

#endif //_HIT_TEST_HH__
// int wear_level_type = 0;//默认理想型
// void init(); // 初始化

