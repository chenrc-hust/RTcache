#!/bin/bash

# 遍历workload文件从0到36
for i in {0..36}
do
  # 构建输入和输出文件名
  input_file="/home/whb/node6/workload${i}.trace"
  # 运行命令并重定向输出
   nohup  ./trace_info $input_file &

done
