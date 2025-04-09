import os
import re
import pandas as pd
import sys
from datetime import datetime  # 导入 datetime 模块

def extract_hit_rate(file_path):
    """
    从文件中提取 hit_rate。
    """
    hit_rate = None
    with open(file_path, 'r') as file:
        for line in file:
            match = re.search(r'hit_rate\s*:\s*([\d\.]+)', line)
            if match:
                hit_rate = float(match.group(1))
                break
    return hit_rate

def process_files(input_folder):
    """
    处理文件夹中的所有文件，提取 hit_rate 并组织为 DataFrame。
    """
    data = {}
    for file_name in os.listdir(input_folder):
        file_path = os.path.join(input_folder, file_name)
        if os.path.isfile(file_path) and file_name.endswith('.out'):
            # 使用正则表达式从文件名末尾开始提取 dt, pre, rtpre 参数
            match = re.search(r'_pre(\d+)_swap\d+\.out$', file_name)
            if not match:
                print(f"Skipping file with unexpected format: {file_name}")
                continue

            pre = match.group(1)  # 提取 pre 参数

            # 提取 workload name
            workload_name = file_name.split('_')[0] + file_name.split('_')[1]

            hit_rate = extract_hit_rate(file_path)
            if hit_rate is None:
                continue

            if workload_name not in data:
                data[workload_name] = {}

            # 正确格式化配置键
            config_key = f"pre{pre}"
            data[workload_name][config_key] = hit_rate

    return data

def generate_table(data):
    """
    根据提取的数据生成 DataFrame，并将其保存为 CSV 文件。
    """
    df = pd.DataFrame.from_dict(data, orient='index')
    df.sort_index(axis=1, inplace=True)  # 根据列名排序
        # 按照 workload name（即 index）排序
    df = df.sort_index()

    return df

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 script.py <input_folder>")
        sys.exit(1)

    input_folder = sys.argv[1]

    if not os.path.isdir(input_folder):
        print(f"{input_folder} is not a valid directory")
        sys.exit(1)

    # 获取当前时间并格式化为字符串，形如 20241010_153045
    timestamp = datetime.now().strftime("%Y%m%d_%H%M")

    # 设置带时间戳的输出文件名
    output_file = f"{timestamp}_cache_3.csv"

    data = process_files(input_folder)
    df = generate_table(data)
    df.to_csv(output_file, float_format="%.6f")
    print(f"Results saved to {output_file}")

if __name__ == "__main__":
    main()
