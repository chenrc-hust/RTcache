#!/bin/bash

# 检查是否提供了足够的命令行参数
if [ $# -lt 1 ]; then
  echo "Usage: $0 <output_directory>"
  exit 1
fi

# 获取命令行参数
output_base_dir=$1
trace_directory="/home/chen/real_trace"

# 定义trace文件和hbm_size映射
declare -A trace_hbm_size_map=(
  ["les-7-12_deal.out"]=512
  ["libquantum-7-12_deal.out"]=512
  ["mcf-6-24_deal.out"]=1677
  ["milc-6-24_deal.out"]=67197
  ["omnetpp-7-12.pout"]=1244
  ["soplex-6-24.pout"]=512
  # ["workloada.trace"]=18698
  # ["workloadb.trace"]=30711
  # ["workloadc.trace"]=18934
  # ["workloadd.trace"]=18717
  # ["workloade.trace"]=18946
)

# 定义其他参数列表
cache_types=(1)
wl_types=(2)
random_starts=(1)
pre_values=(0 1)
print_other_values=(0)

# 最大并行进程数量
max_jobs=50
max_workload_jobs=10

# 函数用于根据wl_type返回swap值
get_swap_value() {
  local wl_type=$1
  case $wl_type in
    1) echo 32 ;;
    2) echo 10 ;;
    *) echo 2048 ;; # 默认值
  esac
}

# 等待直到并行进程数目小于最大并行进程数目
wait_for_jobs() {
  while [ $(jobs | wc -l) -ge $max_jobs ]; do
    sleep 10
  done
}

# 等待直到workload进程数目小于最大workload并行进程数目
wait_for_workload_jobs() {
  while [ $(jobs | grep -c "workload") -ge $max_workload_jobs ]; do
    sleep 10
  done
}

# 遍历trace文件夹中的所有文件
for wl_type in "${wl_types[@]}"; do
  swap_time=$(get_swap_value $wl_type)
  for cache_type in "${cache_types[@]}"; do
    for input_file in "$trace_directory"/*; do
      if [ -f "$input_file" ]; then
        # 获取输入文件的文件名和扩展名
        input_filename=$(basename -- "$input_file")
        input_filename_no_ext="${input_filename%.*}"

        # 获取对应的hbm_size
        hbm_size=${trace_hbm_size_map["$input_filename"]}
        if [ -z "$hbm_size" ]; then
          echo "No hbm_size found for $input_filename" >> error_log.txt
          continue
        fi

        # 检查是否是workload输入
        is_workload=false
        if [[ "$input_filename" == workload* ]]; then
          is_workload=true
        fi

        # 等待进程数目小于限制
        if $is_workload; then
          wait_for_workload_jobs
        else
          wait_for_jobs
        fi

        # 运行命令并重定向输出
        if [ "$cache_type" -ne 0 ]; then
          for random_start in "${random_starts[@]}"; do
            for pre in "${pre_values[@]}"; do
              output_file="${output_base_dir}/wl${wl_type}_cache${cache_type}/${input_filename_no_ext}_hbm${hbm_size}_rs${random_start}_pre${pre}_swap${swap_time}.out"
              mkdir -p "$(dirname "$output_file")"
              cmd="./test \"$cache_type\" \"$wl_type\" \"$hbm_size\" \"$random_start\" \"$pre\" \"$swap_time\" \"$input_file\" > \"$output_file\" 2>> error_log.txt &"
              echo "Executing: $cmd"
              eval $cmd
            done
          done
        else
          output_file="${output_base_dir}/wl${wl_type}_cache${cache_type}/${input_filename_no_ext}_swap${swap_time}.out"
          mkdir -p "$(dirname "$output_file")"
          cmd="./test \"$cache_type\" \"$wl_type\" \"$swap_time\" \"$input_file\" > \"$output_file\" 2>> error_log.txt &"
          echo "Executing: $cmd"
          eval $cmd
        fi
      else
        echo "File $input_file not found!" >> error_log.txt
      fi
    done
  done

done

echo "All tasks completed."
