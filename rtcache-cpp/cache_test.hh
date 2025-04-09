#include "hit_test.hh"

class hit_test;

namespace gem5 {

int cache_type;//0 no,1 rt ,2 tlb, 3 way
const Tick cxl_latency;
int wl_type;// 0 , 1 twl, 2 climber
bool flag_first;

const uint64_t PageSize;//页大小
const uint64_t MaxDRAMPage ;//dram最大页数
//只需要DRAM  =4G ，crc  hbm = 256m
const uint64_t HBMsize;//


/* 统一接口  by crc 24-7-18*/
hit_test * cache_model;


std::pair<bool, uint64_t> get(uint64_t VA){
    return cache_model->get(VA);
};//查找表项    

} // namespace gem5