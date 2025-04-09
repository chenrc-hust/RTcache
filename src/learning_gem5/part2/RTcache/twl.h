/*
 * @Description:
 * @Version: 1.0
 * @Autor: cswhb
 * @Date: 2020-10-20 21:28:07
 * @LastEditors: cswhb
 * @LastEditTime: 2020-10-25 23:33:30
 */
#include <cstdlib>
#include <cstdio>
// #include <math.h>
#include <algorithm>
#include <ctime>
#include <functional>
#include <stdint.h>
using namespace std;
typedef struct lifenode1
{
    uint64_t addr;
    double life;
} lifenode2;
typedef struct countnode1
{
    uint64_t addr;
    uint64_t life;
} countnode2;
class twlobj
{
public:
    unsigned pageshift = 12;
    uint64_t interinterval;
    uint64_t swapthreshold;
    uint64_t maxpagenums;
    uint64_t attacktype;
    uint64_t areanums;
    // unsigned no = no;
    uint64_t *life2sorted;
    double *p;

    double minlifetime;
    double maxlifetime;
    lifenode2 *lifelist;
    lifenode2 *lifelist2;
    lifenode2 *sortedlist;
    // lifenode2* visitcount;
    uint64_t *pairlist;
    uint64_t *intermaptable;
    uint64_t *reversemaptable;
    bool *isswap;
    uint64_t *swapvisitcount;
    // self.hot_record = [0 for m in range(self.maxpagenums)]
    uint64_t swaptimes;
    uint64_t interswaptimes;
    uint64_t *interswapcount;
    uint64_t returnstat;
    // uint64_t returnaddr1;
    uint64_t returnaddr1;
    uint64_t returnaddr2;
    uint64_t returnaddr3;
    uint64_t returnaddr4;
    int dotwl;

    uint64_t totalcount;
    uint64_t remaptimes;
    uint64_t totaltime;
    uint64_t *ans;
    double gaussrand(double mu, double sigma);
    twlobj(uint64_t areasize,int swap_time);
    uint64_t getrandom(uint64_t start, uint64_t end);
    uint64_t getpairaddr(uint64_t *interswapcount, lifenode2 *clifelist, uint64_t areasize, uint64_t addr_temp, uint64_t *intermaptable, uint64_t *reversemaptable, bool sourceaddr, bool iswrite);
    bool swaparbiter(double life1, double life2);
    uint64_t *access(uint64_t addr_temp, bool iswrite);
};