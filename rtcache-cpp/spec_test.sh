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
cache_types=(2)
wl_types=(3)
random_starts=(0)
pre_values=(0)

# 最大并行进程数量
max_jobs=3
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

# 定义一个函数检查文件中是否包含 "rt hit" 或 "sc_hit"
check_hit() {
  local file=$1
  if [ -f "$file" ]; then
    if grep -q "rt hit" "$file" || grep -q "sc_hit" "$file" || grep -q "hit_rate" "$file"; then
      return 0
    else
      return 1
    fi
  else
    return 1
  fi
}

# 记录未完成的测试
unfinished_tests=()

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
          if [ "$cache_type" -eq 2 ]; then
            # cache_type 为 2 时，使用 random_start=0 和 1
            for random_start in "${random_starts[@]}"; do
              for pre in "${pre_values[@]}"; do
                output_file="${output_base_dir}/wl${wl_type}_cache${cache_type}/${input_filename_no_ext}_hbm${hbm_size}_rs${random_start}_pre${pre}_swap${swap_time}.out"
                mkdir -p "$(dirname "$output_file")"
                cmd="./test \"$cache_type\" \"$wl_type\" \"$hbm_size\" \"$random_start\" \"$pre\" \"$swap_time\" \"$input_file\" > \"$output_file\" 2>> error_log.txt &"
                echo "Executing: $cmd"
                eval $cmd
                if ! check_hit "$output_file"; then
                  unfinished_tests+=("$output_file $cache_type $wl_type $hbm_size $random_start $pre $swap_time $input_file")
                fi
              done
            done
          else
            # 其他 cache_type 使用 random_start=1
            random_start=1
            for pre in "${pre_values[@]}"; do
              output_file="${output_base_dir}/wl${wl_type}_cache${cache_type}/${input_filename_no_ext}_hbm${hbm_size}_rs${random_start}_pre${pre}_swap${swap_time}.out"
              mkdir -p "$(dirname "$output_file")"
              cmd="./test \"$cache_type\" \"$wl_type\" \"$hbm_size\" \"$random_start\" \"$pre\" \"$swap_time\" \"$input_file\" > \"$output_file\" 2>> error_log.txt &"
              echo "Executing: $cmd"
              eval $cmd
              if ! check_hit "$output_file"; then
                unfinished_tests+=("$output_file $cache_type $wl_type $hbm_size $random_start $pre $swap_time $input_file")
              fi
            done
          fi
        else
          output_file="${output_base_dir}/wl${wl_type}_cache${cache_type}/${input_filename_no_ext}_swap${swap_time}.out"
          mkdir -p "$(dirname "$output_file")"
          cmd="./test \"$cache_type\" \"$wl_type\" \"$swap_time\" \"$input_file\" > \"$output_file\" 2>> error_log.txt &"
          echo "Executing: $cmd"
          eval $cmd
          if ! check_hit "$output_file"; then
            unfinished_tests+=("$output_file $cache_type $wl_type 0 0 0 $swap_time $input_file")
          fi
        fi
      else
        echo "File $input_file not found!" >> error_log.txt
      fi
    done
  done
done

# 重新运行未完成的测试
if [ ${#unfinished_tests[@]} -ne 0 ]; then
  echo "Rerunning unfinished tests..."
  for test in "${unfinished_tests[@]}"; do
    IFS=' ' read -r -a params <<< "$test"
    output_file="${params[0]}"
    cache_type="${params[1]}"
    wl_type="${params[2]}"
    hbm_size="${params[3]}"
    random_start="${params[4]}"
    pre="${params[5]}"
    swap_time="${params[6]}"
    input_file="${params[7]}"

    mkdir -p "$(dirname "$output_file")"

    # 判断是否是workload输入
    if [[ "$input_file" == workload* ]]; then
      wait_for_workload_jobs
    else
      wait_for_jobs
    fi

    # 构建命令
    if [ "$cache_type" -eq 0 ]; then
      cmd="./test \"$cache_type\" \"$wl_type\" \"$swap_time\" \"$input_file\" > \"$output_file\" 2>> error_log.txt &"
    elif [ "$cache_type" -eq 1 ] || [ "$cache_type" -eq 2 ]; then
      cmd="./test \"$cache_type\" \"$wl_type\" \"$hbm_size\" \"$random_start\" \"$pre\" \"$swap_time\" \"$input_file\" > \"$output_file\" 2>> error_log.txt &"
    else
      cmd="./test \"$cache_type\" \"$wl_type\" \"$hbm_size\" \"$random_start\" \"$pre\" \"$swap_time\" \"$input_file\" > \"$output_file\" 2>> error_log.txt &"
    fi

    echo "Executing: $cmd"
    eval $cmd
  done
fi

# 等待所有后台任务完成
wait

echo "All tasks completed."
