import matplotlib.pyplot as plt

# 用于存储数据的字典
data = {}

# 打开并读取stats.txt文件
with open('stats.txt', 'r') as file:
    for line in file:
        # 查找包含"pagenum-"的行
        if "pagenum-" in line:
            # 分割行以获取所需的值
            parts = line.split()
            # 分割以获取pagenum后的数字
            pagenum = int(parts[0].split('-')[-1])
            # 获取次数值
            count = int(parts[1])
            # 将数据存储在字典中
            data[pagenum] = count

# 创建图表
plt.figure(figsize=(10, 6))

# 绘制柱状图
plt.bar(data.keys(), data.values(), color='skyblue')

# 设置图表标题和坐标轴标签
plt.title('Page Write Count by Page Number')
plt.xlabel('Page Number')
plt.ylabel('Write Count')

# 保存图表为图片，指定分辨率和去除周边空白
plt.savefig('page_write_count.png', dpi=300, bbox_inches='tight')

# 显示图表
plt.show()
