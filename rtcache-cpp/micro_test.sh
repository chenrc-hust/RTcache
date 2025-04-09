#!/bin/bash

# 检查是否提供了足够的命令行参数
if [ $# -lt 2 ]; then
  echo "Usage: $0 <output_directory> <trace_directory_or_file> [hbm_size]"
  exit 1
fi

# 获取命令行参数
output_base_dir=$1
trace_input=$2
hbm_size_global=$3  # 如果指定了 hbm_size，则使用全局变量

# 定义trace文件和hbm_size映射
declare -A trace_hbm_size_map=(
  ["workload5.pout"]=4096
  ["workload6.pout"]=4096
  ["workload7.pout"]=4096
  ["workload8.pout"]=4096
  ["workload9.pout"]=4096
  ["workload10.pout"]=4096
  ["workload11.pout"]=4096
  ["workload12.pout"]=4096
)

# 定义其他参数列表
cache_types=(1 2 3)
wl_types=(0)
pre_values=(0)
dongtai_values=(0)  # 新增：动态控制
rt_pre_values=(8)  # 新增：RT预取个数
# rt_pre_values=(32 64)
random_starts=(1)  # 仅当cache_type == 2时使用

# 最大并行进程数量
max_jobs=72
max_workload_jobs=100

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

# 处理trace文件或文件夹中的文件
process_trace_file() {
  local input_file=$1
  local swap_time=$2
  local cache_type=$3
  local wl_type=$4
  local hbm_size=$5

  # 获取输入文件的文件名和扩展名
  input_filename=$(basename -- "$input_file")
  input_filename_no_ext="${input_filename%.*}"

  # 如果全局 hbm_size 被指定，则使用它，否则使用映射的默认值
  hbm_size=${hbm_size_global:-${trace_hbm_size_map["$input_filename"]}}

  if [ -z "$hbm_size" ]; then
    echo "No hbm_size found for $input_filename" | tee -a error_log.txt
    return
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

  # 构建并运行命令
  if [ "$cache_type" -eq 1 ]; then  # 针对RTcache
    for pre in "${pre_values[@]}"; do
      for dongtai in "${dongtai_values[@]}"; do
        for rt_pre in "${rt_pre_values[@]}"; do
          output_file="${output_base_dir}/wl${wl_type}_cache${cache_type}/${input_filename_no_ext}_hbm${hbm_size}_dt${dongtai}_pre${pre}_rtpre${rt_pre}_swap${swap_time}.out"
          mkdir -p "$(dirname "$output_file")"
          cmd="./test \"$cache_type\" \"$wl_type\" \"$hbm_size\" \"$dongtai\" \"$rt_pre\" \"$pre\" \"$swap_time\" \"$input_file\" > \"$output_file\" 2>> error_log.txt &"
          echo "Executing: $cmd"
          eval $cmd
        done
      done
    done
  elif [ "$cache_type" -eq 2 ]; then  # 针对SB TLB
    for random_start in "${random_starts[@]}"; do
      for pre in "${pre_values[@]}"; do
        output_file="${output_base_dir}/wl${wl_type}_cache${cache_type}/${input_filename_no_ext}_hbm${hbm_size}_rs${random_start}_pre${pre}_swap${swap_time}.out"
        mkdir -p "$(dirname "$output_file")"
        cmd="./test \"$cache_type\" \"$wl_type\" \"$hbm_size\" \"$random_start\" \"$pre\" \"$swap_time\" \"$input_file\" > \"$output_file\" 2>> error_log.txt &"
        echo "Executing: $cmd"
        eval $cmd
      done
    done
  else  # 针对其他缓存类型
    for pre in "${pre_values[@]}"; do
      output_file="${output_base_dir}/wl${wl_type}_cache${cache_type}/${input_filename_no_ext}_hbm${hbm_size}_pre${pre}_swap${swap_time}.out"
      mkdir -p "$(dirname "$output_file")"
      cmd="./test \"$cache_type\" \"$wl_type\" \"$hbm_size\" \"$pre\" \"$swap_time\" \"$input_file\" > \"$output_file\" 2>> error_log.txt &"
      echo "Executing: $cmd"
      eval $cmd
    done
  fi
}

# 遍历trace文件或文件夹
for wl_type in "${wl_types[@]}"; do
  swap_time=$(get_swap_value $wl_type)
  for cache_type in "${cache_types[@]}"; do

    # 如果输入的是文件夹，遍历文件夹中的所有文件
    if [ -d "$trace_input" ]; then
      for input_file in "$trace_input"/*; do
        if [ -f "$input_file" ]; then
          process_trace_file "$input_file" "$swap_time" "$cache_type" "$wl_type" "$hbm_size_global"
        else
          echo "File $input_file not found for cache_type=$cache_type, wl_type=$wl_type" | tee -a error_log.txt
        fi
      done
    elif [ -f "$trace_input" ]; then
      # 如果输入的是单个文件，直接处理这个文件
      process_trace_file "$trace_input" "$swap_time" "$cache_type" "$wl_type" "$hbm_size_global"
    else
      echo "Input $trace_input is not valid!" | tee -a error_log.txt
      exit 1
    fi

  done
done

echo "All tasks completed."
