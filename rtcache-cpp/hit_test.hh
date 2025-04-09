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
#include <boost/program_options.hpp>
#include <boost/bimap.hpp>
#include "_way_cache.hh"
#include "rtcache.hh"
#include "no_cache.hh"
#include "SB_TLB.hh"
#include "twl.h"
#include "climber.h"
#include "lx.hh"

class rt;
class _way_cache;
class no_cache;
class SB_TLB;
class twlobj;
class climberobj;
class lx;

twlobj *twl = nullptr;//指向twl磨损均衡

climberobj *climber= nullptr;

lx *lixiang= nullptr;//指向理想磨损均衡

rt *rtcache= nullptr;// 指向rtcache模块

_way_cache *cache= nullptr;
no_cache *nocache= nullptr;
SB_TLB *sbcache= nullptr;

bool is_no_cache; // 有没有cache

bool is_RTcache;  // 是否是rtcache

bool is_SBcache;//是否 优化的tlb

int  read_flag;
const uint64_t PageSize = 4096;
std::string physical_path;
long long Suoxiao = 1;//倍数

uint64_t MaxDrampage;
// int wearlevel_threshold = 5000; // 页面热度的迁移阈值
int wearlevel_time = 0;
// int write_count;
// std::vector<uint32_t> CounterMap; // 访问计数 记录dram页面以及对应的热度
// uint64_t page_1_ago;              // 上一轮写入最多的
// uint64_t page_2_ago;              // 上一轮写入最少的
// std::vector<int>Remappingtable;//模拟内存的重映射表 
boost::bimaps::bimap<uint32_t,uint32_t>Remappingtable;
int print_threshold = 1000000;//写入次数输出阈值

// int wear_level_type = 0;//默认理想型
// void init(); // 初始化

