#include <cstdlib>
#include <cstdio>
// #include <math.h>
#include <algorithm>
#include <ctime>
#include "twl.h"
#include <cmath>
#include <stdexcept>
#include <functional>
#include <limits>
#include <fstream>
#include <string>
#include <iostream>
using namespace std;
#define random(x) rand() % (x)
bool cmptwl(lifenode2 a, lifenode2 b)
{
    return a.life < b.life;
};
// double  twlobj::gaussrand(double mu, double sigma)
//{
//     const double epsilon = std::numeric_limits<double>::min();
//     const double two_pi = 2.0*3.14159265358979323846;//

//    static double z0, z1;
//    static bool generate;
//    generate = !generate;//

//    if (!generate)
//       return z1 * sigma + mu;//

//    double u1, u2;
//    do
//     {
//       u1 = rand() * (1.0 / RAND_MAX);
//       u2 = rand() * (1.0 / RAND_MAX);
//     }
//    while ( u1 <= epsilon );//

//    z0 = sqrt(-2.0 * log(u1)) * cos(two_pi * u2);
//    z1 = sqrt(-2.0 * log(u1)) * sin(two_pi * u2);
//    return z0 * sigma + mu;
//};
twlobj::twlobj(uint64_t areasize,int swap_time)
{
    ////areasize:最大页号，attacktype:攻击类型；no:序号；areashift：相对于页号的粒度移位4MB = 10
    // this->areashift = areashift
    // this->maxpagenums = (areasize >> areashift)
    this->maxpagenums = areasize;
    this->attacktype = 0;
    interinterval = swap_time*4;
    swapthreshold = swap_time;
    printf("maxpagenums:%lu\n", this->maxpagenums);
    printf("gen life distribution begin\n");
    this->areanums = this->maxpagenums >> 10;
    this->minlifetime = 10000000000;
    this->maxlifetime = 0.0;
    this->life2sorted = (uint64_t *)malloc(this->maxpagenums * sizeof(uint64_t));
    // np.random.seed(0)
    printf("gen life distribution begin\n");
    //        p = (double*)malloc(2*areanums*sizeof(double));
    //        for(uint64_t i=0;i<2*areanums;i++){
    //           p[i] = gaussrand(0.3,0.033);
    //        }
    //        //p = np.random.normal(loc = mu, scale = sigma, size = 2*areanums)
    //        sort(p,p+2*areanums,greater<double>());
    printf("1");
    double *x = (double *)malloc(this->maxpagenums * sizeof(double));
    char lifepath[30]; 
    string lifeline;
    uint64_t pagesize = this->maxpagenums / 1024 / 1024 * 4;

    snprintf(lifepath, sizeof(lifepath), "life_%luG.dat", pagesize);
    printf("lifepath:%s\n", lifepath);
    ifstream in(lifepath);
    uint64_t randomkey = (uint64_t)random((int)(this->maxpagenums));
    for (uint64_t i = 0; i < this->maxpagenums; i++)
    {
        getline(in, lifeline);
        x[i ^ randomkey] = atof(lifeline.c_str());
        // x[i] = pow(p[areanums+(i>>10) - 1],-12)*90.345;
    }
    in.close();
    lifelist = (lifenode2 *)malloc(this->maxpagenums * sizeof(lifenode2));
    lifelist2 = (lifenode2 *)malloc(this->maxpagenums * sizeof(lifenode2));
    sortedlist = (lifenode2 *)malloc(this->maxpagenums * sizeof(lifenode2));
    // this->ideallifelist = [0 for y in range(len(x))]
    this->interswapcount = (uint64_t *)malloc(this->maxpagenums * sizeof(uint64_t));
    for (uint64_t i = 0; i < this->maxpagenums; i++)
    {
        this->interswapcount[i] = 0;
        if (this->minlifetime > x[i])
        {
            this->minlifetime = x[i];
        }
        if (this->maxlifetime < x[i])
        {
            this->maxlifetime = x[i];
        }
        this->lifelist[i].addr = i;
        this->lifelist[i].life = x[i]; //////页面i寿命为x[i]
        this->lifelist2[i].addr = i;
        this->lifelist2[i].life = x[i]; //////页面i寿命为x[i]
        this->life2sorted[i] = i;
        this->sortedlist[i].addr = i;
        this->sortedlist[i].life = x[i];
    }
    printf("minlifetime =%f,maxlifetime =%f\n", (minlifetime), (maxlifetime));
    this->pairlist = (uint64_t *)malloc(this->maxpagenums * sizeof(uint64_t));
    printf("sort pages begin\n");
    sort(this->sortedlist, (this->sortedlist) + this->maxpagenums, cmptwl); ////////按照寿命排序后，进行配对，配对公式：
    printf("sort pages end\n");
    printf("pair pages begin\n");
    for (uint64_t i = 0; i < this->maxpagenums; i++)
    {
        this->pairlist[this->sortedlist[i].addr] = i;
    }
    printf("pair pages end\n");
    this->intermaptable = (uint64_t *)malloc(this->maxpagenums * sizeof(uint64_t));   //////记录对间映射
    this->reversemaptable = (uint64_t *)malloc(this->maxpagenums * sizeof(uint64_t)); //////记录反向对间映射
    for (uint64_t i = 0; i < this->maxpagenums; i++)
    {
        this->intermaptable[i] = i;
        this->reversemaptable[i] = i;
    }
    this->isswap = (bool *)malloc(this->maxpagenums * sizeof(bool));                 ////记录该对内部是否是处于交换状态
    this->swapvisitcount = (uint64_t *)malloc(this->maxpagenums * sizeof(uint64_t)); //////////////////////记录每个页从上次交换起被访问的次数
    for (uint64_t i = 0; i < this->maxpagenums; i++)
    {
        this->isswap[i] = 0;
        this->swapvisitcount[i] = 0;
    }
    // this->hot_record = [0 for m in range(this->maxpagenums)]
    this->swaptimes = 0;
    this->interswaptimes = 0;
    this->returnstat = 0;
    this->returnaddr1 = 0;
    this->returnaddr2 = 0;
    this->returnaddr3 = 0;
    this->returnaddr4 = 0;
    this->dotwl = 0;

    // this->no = 0;

    this->totalcount = 0;
    this->remaptimes = 0;
    this->totaltime = 0; ////////循环内次数
    ans = (uint64_t *)malloc(20 * sizeof(uint64_t));
}
uint64_t twlobj::getrandom(uint64_t start, uint64_t end)
{
    srand((int)time(0));
    return start + random(end - start + 1);
};
uint64_t twlobj::getpairaddr(uint64_t *interswapcount, lifenode2 *clifelist, uint64_t areasize, uint64_t addr_temp, uint64_t *intermaptable, uint64_t *reversemaptable, bool sourceaddr, bool iswrite)
{
    // global interinterval
    uint64_t raddr = 0;
    uint64_t pairaddr0 = 0;
    uint64_t addr = 0;
    bool sourceaddr0 = 0;
    if (sourceaddr == 1)
    {
        addr = this->maxpagenums - 1 - addr_temp;
    }
    else
    {
        addr = addr_temp;
    }
    if (iswrite == 0)
    {
        return intermaptable[addr];
    }
    interswapcount[addr] = interswapcount[addr] + 1;
    uint64_t addr_next = 0;
    if (interswapcount[addr] >= interinterval)
    {
        // this->returnaddr4 += 5;
        this->interswaptimes = this->interswaptimes + 1;
        interswapcount[addr] = 0;
        addr_next = getrandom(0, areasize - 1);
        if (intermaptable[addr] >= this->maxpagenums / 2)
        {
            pairaddr0 = this->maxpagenums - 1 - intermaptable[addr];
            sourceaddr0 = 1;
        }
        else
        {
            pairaddr0 = intermaptable[addr];
            sourceaddr0 = 0;
        }
        while (addr_next == intermaptable[addr])
        {
            addr_next = getrandom(0, areasize - 1);
        }
        //////////只写一次
        //                if addr_next >=this->maxpagenums / 2:
        // /                   pairaddr = this->maxpagenums - 1 - addr_next
        //                    sourceaddr2 = 1
        //                else:
        //                    pairaddr = addr_next
        //                    sourceaddr2 = 0
        ////只能和对应位置交换
        // if sourceaddr0 ^ this->isswap[pairaddr0] != sourceaddr2 ^ this->isswap[pairaddr]:
        //     continue

        if ((sourceaddr0 ^ this->isswap[pairaddr0]) == 0)
        {
            this->returnstat = 1;
            this->returnaddr1 = this->sortedlist[intermaptable[addr]].addr;
            clifelist[this->sortedlist[intermaptable[addr]].addr].life = clifelist[this->sortedlist[intermaptable[addr]].addr].life - 1;
        }
        else
        {
            this->returnstat = 1;
            this->returnaddr1 = this->sortedlist[areasize - 1 - intermaptable[addr]].addr;
            clifelist[this->sortedlist[areasize - 1 - intermaptable[addr]].addr].life = clifelist[this->sortedlist[areasize - 1 - intermaptable[addr]].addr].life - 1;
        }
        // 只写一次
        // if sourceaddr2 ^ this->isswap[pairaddr] == 0:
        //     clifelist[this->sortedlist[addr_next][0]][1] = clifelist[this->sortedlist[addr_next][0]][1] - 1
        // else:
        //     clifelist[this->sortedlist[this->maxpagenums - 1 - addr_next][0]][1] = clifelist[this->sortedlist[this->maxpagenums - 1 - addr_next][0]][1] - 1

        // clifelist[areasize * 2 - 1 - addr_next][1] = clifelist[areasize * 2 - 1 - addr_next][1] - 1

        uint64_t temp = intermaptable[addr];
        intermaptable[addr] = addr_next;
        uint64_t reverseaddr = reversemaptable[addr_next];
        interswapcount[reverseaddr] = 0;
        intermaptable[reverseaddr] = temp;
        reversemaptable[temp] = reversemaptable[addr_next];
        reversemaptable[addr_next] = addr;
    }
    // this->returnaddr1 = intermaptable[addr];
    raddr = intermaptable[addr];
    return raddr;
}
bool twlobj::swaparbiter(double life1, double life2)
{
    double toss = rand() % (1000) / (float)(1000);
    if (toss >= life1 / (life1 + life2))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}
/* 写入的页号，是不是写 */
uint64_t *twlobj::access(uint64_t addr_temp, bool iswrite)
{
    // addr_temp2:页号,,现在是区域号
    // addr_temp = (addr_temp2 >> this->areashift)
    // uint64_t*ans = (uint64_t*)malloc(20*sizeof(uint64_t));
    ////5,6,7,8,9;10,11,12,13,14
    this->returnaddr1 = 0;
    this->returnaddr2 = 0;
    bool isdoswap = 0;
    uint64_t pairindex_temp = this->pairlist[addr_temp];
    uint64_t pairindex = 0;
    uint64_t pairaddr = 0;
    bool sourceaddr = 0; // 要写的地址是块内块0还是块1的地址，用于判断是否交换
    if (pairindex_temp >= this->maxpagenums / 2)
    {
        pairindex = this->maxpagenums - 1 - pairindex_temp;
        sourceaddr = 1;
    }
    else
    {
        pairindex = pairindex_temp;
        sourceaddr = 0;
    }
    this->returnaddr1 = addr_temp;
    uint64_t newaddr = this->getpairaddr(this->interswapcount, this->lifelist, int(this->maxpagenums), pairindex, this->intermaptable, this->reversemaptable, sourceaddr, iswrite);
    if (newaddr >= this->maxpagenums / 2)
    {
        pairaddr = this->maxpagenums - 1 - newaddr;
        sourceaddr = 1;
    }
    else
    {
        pairaddr = newaddr;
        sourceaddr = 0;
    }
    uint64_t addr = this->sortedlist[pairaddr].addr;
    uint64_t nowaddr2 = this->sortedlist[this->maxpagenums - 1 - this->pairlist[addr]].addr;
    if ((sourceaddr ^ this->isswap[pairaddr]) == 0)
    {
        if (iswrite == 1)
        {
            this->swapvisitcount[addr] = this->swapvisitcount[addr] + 1;
        }
        if (this->swapvisitcount[addr] >= swapthreshold)
        {
            isdoswap = 1;
        }
    }
    else
    {
        if (iswrite == 1)
        {
            this->swapvisitcount[nowaddr2] = this->swapvisitcount[nowaddr2] + 1;
        }
        if (this->swapvisitcount[nowaddr2] >= swapthreshold)
        {
            isdoswap = 1;
        }
    }
    if (isdoswap == 1)
    {
        this->swapvisitcount[addr] = 0;
        this->swapvisitcount[nowaddr2] = 0;
        isdoswap = 0;
        bool swaptemp = 0;
        if (sourceaddr == 1)
        {
            swaptemp = this->swaparbiter(this->lifelist[nowaddr2].life, this->lifelist[addr].life); //////////在执行过程中根据当前寿命调整
        }
        else
        {
            swaptemp = this->swaparbiter(this->lifelist[addr].life, this->lifelist[nowaddr2].life);
        }
        if ((swaptemp ^ this->isswap[pairaddr]) == 1)
        {
            // block end
            //////////////////////////////////////////////////////////////////////////////////
            //////进行交换，匹配对寿命降低
            this->swaptimes = this->swaptimes + 1;
            this->returnstat = this->returnstat + 1;
            this->returnaddr3 = this->returnaddr1;
            this->returnaddr1 = addr;
            this->returnaddr2 = this->sortedlist[this->maxpagenums - 1 - this->pairlist[addr]].addr;
            this->lifelist[addr].life = this->lifelist[addr].life - 1;
            this->lifelist[this->sortedlist[this->maxpagenums - 1 - this->pairlist[addr]].addr].life = this->lifelist[this->sortedlist[this->maxpagenums - 1 - this->pairlist[addr]].addr].life - 1;
            this->isswap[pairaddr] = this->isswap[pairaddr] ^ 1;
        }
        else
        {
            this->returnaddr4 += 10;
            if ((sourceaddr ^ this->isswap[pairaddr]) == 0)
            {
                // this->returnstat1 = 0
                this->returnaddr2 = this->returnaddr1;
                this->returnaddr1 = addr;
                this->lifelist[addr].life = this->lifelist[addr].life - 1; ////////当前页寿命降低
            }
            else
            {
                // this->returnstat1 = 0
                this->returnaddr2 = this->returnaddr1;
                this->returnaddr1 = this->sortedlist[this->maxpagenums - 1 - this->pairlist[addr]].addr;
                this->lifelist[this->sortedlist[this->maxpagenums - 1 - this->pairlist[addr]].addr].life = this->lifelist[this->sortedlist[this->maxpagenums - 1 - this->pairlist[addr]].addr].life - 1;
            }
        }
    }
    else
    {
        if ((sourceaddr ^ this->isswap[pairaddr]) == 0)
        {
            if (iswrite == 1)
            {
                this->lifelist[addr].life = this->lifelist[addr].life - 1; ////////当前页寿命降低
            }
            // this->returnstat = this->returnstat + 1
            this->returnaddr2 = this->returnaddr1;
            this->returnaddr1 = addr;
        }
        else
        {
            // this->returnstat = this->returnstat + 1
            this->returnaddr2 = this->returnaddr1;
            this->returnaddr1 = this->sortedlist[this->maxpagenums - 1 - this->pairlist[addr]].addr;
            if (iswrite == 1)
            {
                this->lifelist[this->sortedlist[this->maxpagenums - 1 - this->pairlist[addr]].addr].life = this->lifelist[this->sortedlist[this->maxpagenums - 1 - this->pairlist[addr]].addr].life - 1;
            }
        }
    }
    ans[0] = this->returnstat;
    ans[1] = this->returnaddr1;
    ans[2] = this->returnaddr2;
    ans[3] = this->returnaddr3;
    ans[8] = 0;
    ans[9] = this->dotwl;
    this->returnstat = 0;
    this->returnaddr4 = 0;
    return ans;
}
/* 返回值是一个数组，，，，ans[0]为0表示没有交换，，，，，ans[0]为1 交换ans[1]和ans[2]对应的页面，，，ans[0]为2交换ans[1]和ans[2]还有ans[3]对应的页面 */
// int main(){
//    twlobj* t1 = new twlobj(1048576);
// }
