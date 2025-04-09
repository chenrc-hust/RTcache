#!/bin/bash

# 检查是否提供了足够的命令行参数
if [ $# -lt 2 ]; then
  echo "Usage: $0 <output_directory> <trace_directory>"
  exit 1
fi

# 获取命令行参数
output_base_dir=$1
trace_directory=$2

# 创建输出目录（如果不存在）
mkdir -p "$output_base_dir"

# 定义参数列表
# 预取
cache_types=(1 3)
wl_types=(0)
hbm_sizes=(128) # 单位KB
random_starts=(1)
pre_values=(0 1)
print_other_values=(0)

# 函数用于根据wl_type返回swap值
get_swap_value() {
  local wl_type=$1
  case $wl_type in
    0) echo 2048 ;;
    1) echo 32 ;;
    2) echo 10 ;;
    *) echo 2048 ;; # 默认值
  esac
}

# 遍历trace文件夹中的所有文件
for cache_type in "${cache_types[@]}"; do
  for input_file in "$trace_directory"/*; do
    if [ -f "$input_file" ]; then
      # 获取输入文件的文件名和扩展名
      input_filename=$(basename -- "$input_file")
      input_filename_no_ext="${input_filename%.*}"

      # 运行命令并重定向输出
      for wl_type in "${wl_types[@]}"; do
        swap_time=$(get_swap_value $wl_type)
        for hbm_size in "${hbm_sizes[@]}"; do
          for random_start in "${random_starts[@]}"; do
            if [ "$cache_type" -eq 1 ] || [ "$cache_type" -eq 3 ]; then
              for pre in "${pre_values[@]}"; do
                if [ "$cache_type" -ne 3 ]; then
                  for print_other in "${print_other_values[@]}"; do
                    output_file="${output_base_dir}/cache${cache_type}_wl${wl_type}/${input_filename_no_ext}_hbm${hbm_size}_rs${random_start}_pre${pre}_po${print_other}_swap${swap_time}.out"
                    mkdir -p "$(dirname "$output_file")"
                    ./test "$cache_type" "$wl_type" "$hbm_size" "$random_start" "$pre" "$swap_time" "$print_other" "$input_file" > "$output_file" 2>> error_log.txt &
                    if [ $? -ne 0 ]; then
                      echo "Error occurred while processing $input_file with cache_type=$cache_type, wl_type=$wl_type, hbm_size=$hbm_size, random_start=$random_start, pre=$pre, print_other=$print_other, swap_time=$swap_time" >> error_log.txt
                    fi
                  done
                else
                  output_file="${output_base_dir}/cache${cache_type}_wl${wl_type}/${input_filename_no_ext}_hbm${hbm_size}_rs${random_start}_pre${pre}_swap${swap_time}.out"
                  mkdir -p "$(dirname "$output_file")"
                  ./test "$cache_type" "$wl_type" "$hbm_size" "$random_start" "$pre" "$swap_time" "$input_file" > "$output_file" 2>> error_log.txt &
                  if [ $? -ne 0 ]; then
                    echo "Error occurred while processing $input_file with cache_type=$cache_type, wl_type=$wl_type, hbm_size=$hbm_size, random_start=$random_start, pre=$pre, swap_time=$swap_time" >> error_log.txt
                  fi
                fi
              done
            else
              output_file="${output_base_dir}/cache${cache_type}_wl${wl_type}/${input_filename_no_ext}_hbm${hbm_size}_rs${random_start}_swap${swap_time}.out"
              mkdir -p "$(dirname "$output_file")"
              ./test "$cache_type" "$wl_type" "$hbm_size" "$random_start" "$swap_time" "$input_file" > "$output_file" 2>> error_log.txt &
              if [ $? -ne 0 ]; then
                echo "Error occurred while processing $input_file with cache_type=$cache_type, wl_type=$wl_type, hbm_size=$hbm_size, random_start=$random_start, swap_time=$swap_time" >> error_log.txt
              fi
            fi
          done
        done
      done
    else
      echo "File $input_file not found!" >> error_log.txt
    fi
  done
  # 等待当前 cache_type 的所有任务完成
  wait
done
