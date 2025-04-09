#include "cache_test.hh"

namespace gem5 {

int main(int argc, char **argv)
{
    // 检查参数数量
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " <options>" << std::endl;
        return 1;
    }

    int i = 1;

    int cache_type = stoi(argv[i++]); // cache类型
    int wl_type = stoi(argv[i++]);    // wl 类型
    MaxDrampage = 1ULL * 256 * 1024 * 1024 * 1024; // 256GB对齐最大地址
    MaxDrampage /= PageSize;      // 最大页数

    uint64_t HBMsize = 4096;//单位B 

    int random_start = 0; // 默认不随机

    if (cache_type) 
    {
        HBMsize = stoll(argv[i++]);//设置容量
    }
    bool set_pre = true; // 默认有预取
    int print_other = 0;
    
    if (cache_type == 1 || cache_type == 3) // rt,以及8set, 区分有无预取
    {
        if (!stoi(argv[i++]))
        {
                 set_pre = false;
        }
    }

    if(cache_type !=2 )//2是 tlb 
        random_start = stoi(argv[i++]); 
    if (cache_type == 1 || cache_type == 2)
        print_other = stoi(argv[i++]); // 是否输出额外信息 也就是timeprint，目前应该都不用，

    string physical_path = argv[i++];

    wearlevel_threshold = 2048; // 理想用

    std::string extension;

    size_t pos = physical_path.rfind('.');
    if (pos != std::string::npos)
    {
        extension = physical_path.substr(pos + 1);
    }
    else
    {
        std::cerr << "No file extension found: " << physical_path << std::endl;
        return 0;
    }


    std::cout << physical_path << " " << MaxDrampage << endl;
    

    int fd = open(physical_path.c_str(), O_RDONLY);
    if (fd == -1)
    {
        std::cerr << "Failed to open file: " << physical_path << std::endl;
        return 1;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1)
    {
        std::cerr << "Failed to get file status: " << physical_path << std::endl;
        close(fd);
        return 1;
    }

    size_t fileSize = sb.st_size;
    char *data = static_cast<char *>(mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0));
    if (data == MAP_FAILED)
    {
        std::cerr << "Failed to map file: " << physical_path << std::endl;
        close(fd);
        return 1;
    }

    size_t offset = 0;
    while (offset < fileSize)
    {
        char *lineEnd = static_cast<char *>(memchr(data + offset, '\n', fileSize - offset));
        if (!lineEnd)
        {
            break;
        }

        std::string line(data + offset, lineEnd - (data + offset));
        offset += line.size() + 1;

        istringstream read(line);
        char operator_;
        uint64_t addr;
        std::string ignore1, ignore2, ignore4;
        int rw;

        if (extension == "out")
        {
            read >> operator_ >> addr;
            rw = (operator_ == 'R' )? 0 : 1;//写操作为1

        }
        else if (extension == "trace")
        {
            read >> rw >> addr;
            
        }
        else if (extension == "pout")
        {
            read >> operator_ >> hex >> addr >> ignore1;
            rw = (operator_ == 'R' )? 0 : 1;//写操作为1
        }
        else
        {
            std::cerr << "Unsupported file extension: " << extension << std::endl;
            break;
        }

        uint64_t dram_num = addr / PageSize;

        if (dram_num >= MaxDrampage)
        {
            dram_num %= MaxDrampage;
        }

        total_count++;

        if (!rw)
            read_count++;

        std::pair<bool, uint64_t> it = get(dram_num);
        
        if(wl_type==0)
            update_time= cache_model->wearlevelaccess(it.second,rw);//传入映射后的地址,rw == 写操作
        else if(wl_type!=3)
            update_time= cache_model->wearlevelaccess(dram_num,rw);//内部有映射表表，
    }

    munmap(data, fileSize);
    
    close(fd);

    cache_model->print();

    return 0;
}