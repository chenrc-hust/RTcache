import os

# 定义新目录的路径
new_directory = "/home/chenrc/gem5-23.0.1.0/configs/deprecated/example"
if not os.path.exists(new_directory):
    os.makedirs(new_directory)
# 原始配置文件路径
original_config = '1-3-6144-10800-1.py'

# 需要创建的新配置
new_configs = [
    (48,80),
    (96, 168),
    (192, 336),
    (768,1344),
    (1536,2696),
    (3072,5400),
    (12288,21600),
    (24576,43200),
]

# 逐个处理新配置
for cache_size, buffer_size in new_configs:
    # 读取原始配置文件
    with open(original_config, 'r') as file:
        lines = file.readlines()

    # 更新配置行
    for i, line in enumerate(lines):
        if line.strip().startswith('SimpleMemory.CACHE1_SIZE_1'):
            lines[i] = f'    SimpleMemory.CACHE1_SIZE_1 = {cache_size}\n'
        elif line.strip().startswith('SimpleMemory.BUFFER_SIZE_1'):
            lines[i] = f'    SimpleMemory.BUFFER_SIZE_1 = {buffer_size}\n'

    # 新文件名
    new_filename = f'1-3-{cache_size}-{buffer_size}-1.py'

    # 写入新文件到新目录
    new_file_path = os.path.join(new_directory, new_filename)
    with open(new_file_path, 'w') as file:
        file.writelines(lines)

print("配置文件已生成。")
