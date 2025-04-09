#coding:utf-8
import random
import numpy as np
import math
import sys

def parse_size(size_str):
    """
    解析表示大小的字符串，并返回以字节为单位的大小。
    """
    size_str = size_str.upper()
    if size_str.endswith("G"):
        return int(size_str[:-1]) * 1024 * 1024 * 1024
    elif size_str.endswith("M"):
        return int(size_str[:-1]) * 1024 * 1024
    elif size_str.endswith("K"):
        return int(size_str[:-1]) * 1024
    else:
        return int(size_str)

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 script.py <size>")
        sys.exit(1)
    
    size_str = sys.argv[1]
    size_in_bytes = parse_size(size_str)
    
    # 每个页面的大小为4KB
    page_size = 4 * 1024
    maxpagenums = size_in_bytes // page_size
    
    # 定义常量
    mu = 0.3 
    sigma = mu * 0.11

    # 设置areanums足够大，以涵盖所有可能的索引
    areanums = maxpagenums // 1024 

    # 生成正态分布的随机数
    np.random.seed(0)
    print("gen life distribution begin")
    p = np.random.normal(loc=mu, scale=sigma, size=2*areanums)
    p.sort()

    # 计算“生命分布”
    x = [0 for i in range(maxpagenums)]
    for i in range(maxpagenums):
        x[i] = math.pow(p[areanums + (i >> 10) - 1], -12) * 90.345
    print("gen life distribution end")

    # 生成输出文件名
    output_file = f"life_{size_str}.dat"
    
    # 将结果写入文件，保留6位小数
    with open(output_file, 'w') as f:
        for value in x:
            f.write(f"{value:.6f}\n")

    print(f"Results have been written to {output_file}")

if __name__ == "__main__":
    main()
