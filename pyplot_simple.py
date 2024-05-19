import os
import matplotlib.pyplot as plt

def process_stats_file(stats_file, keyword):
    """
    Process stats.txt file and collect data based on keyword ("pagenum-" or "llcnum-").
    """
    data = {}
    with open(stats_file, 'r') as file:
        for line in file:
            if keyword in line:
                parts = line.split()
                num = int(parts[0].split('-')[-1])
                count = int(parts[1])
                data[num] = count
    return data

def plot_data(data, title, xlabel, ylabel, plot_type, file_name):
    """
    Plot the collected data as either a bar chart or a line plot.
    """
    plt.figure(figsize=(10, 6))
    if plot_type == 'bar':
        plt.bar(data.keys(), data.values(), color='skyblue')
    elif plot_type == 'line':
        plt.plot(list(data.keys()), list(data.values()), marker='o', linestyle='-', color='skyblue')
    plt.title(title)
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.savefig(file_name)
    plt.close()

def process_all_folders(base_folder):
    """
    Process specified sub-folders in the base_folder for stats.txt and plot data.
    """
    sub_folders = [
        "m5out"
    ]
    
    for sub_folder in sub_folders:
        folder_path = os.path.join(base_folder, sub_folder)
        stats_file = os.path.join(folder_path, 'stats.txt')
        if os.path.exists(stats_file):
            # Process and plot pagenum data
            pagenum_data = process_stats_file(stats_file, 'pagenum-')
            plot_data(pagenum_data, f'Page Number Distribution in {sub_folder}', 'Page Number', 'Count', 'bar', os.path.join(folder_path, 'pagenum_plot.png'))
            
            # Process and plot llcnum data
            llcnum_data = process_stats_file(stats_file, 'llcnum-')
            plot_data(llcnum_data, f'LLC Number Distribution in {sub_folder}', 'LLC Number', 'Count', 'line', os.path.join(folder_path, 'llcnum_plot.png'))

# Replace 'path_to_base_folder' with the path to your base folder
base_folder = '/home/chenrc/gem5-23.0.1.0/'
process_all_folders(base_folder)

