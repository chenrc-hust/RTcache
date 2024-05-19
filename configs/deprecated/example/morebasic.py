import os

# 定义新目录的路径
new_directory = "/home/chenrc/gem5-23.0.1.0/configs/deprecated/example"
if not os.path.exists(new_directory):
    os.makedirs(new_directory)
# 原始配置文件路径
original_config = '1-3-6144-10800-0.py'

# 需要创建的新配置
new_configs = [
    (0,0,0),
    (0,1,0),
    (0,2,0),
    (0,3,0),
    (1,1,0),
    (1,2,0),
    (1,3,0),
    (1,1,1),
    (1,2,1),
    (1,3,1),    
]

# 逐个处理新配置
for PCM_TRUE, TWL,CACHE_TRUE in new_configs:
    # 读取原始配置文件
    with open(original_config, 'r') as file:
        lines = file.readlines()

    # 更新配置行
    for i, line in enumerate(lines):
        if line.strip().startswith('SimpleMemory.bool_fpag_mem'):
            lines[i] = f'    SimpleMemory.bool_fpag_mem = {PCM_TRUE}\n'
        elif line.strip().startswith('SimpleMemory.what_twl'):
            lines[i] = f'    SimpleMemory.what_twl = {TWL}\n'
        elif line.strip().startswith('SimpleMemory.bool_cache'):
            lines[i] = f'    SimpleMemory.bool_cache = {CACHE_TRUE}\n'
    # 新文件名
    new_filename = f'{PCM_TRUE}-{TWL}-6144-10800-{CACHE_TRUE}.py'

    # 写入新文件到新目录
    new_file_path = os.path.join(new_directory, new_filename)
    with open(new_file_path, 'w') as file:
        file.writelines(lines)

print("配置文件已生成。")
