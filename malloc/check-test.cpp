#include <map>
#include <iostream>
#include <fstream>

bool cover(size_t addr_1, size_t end_1, size_t addr_2, size_t end_2) {
    return !(end_2 <= addr_1 || end_1 <= addr_2);
}

bool add_mem_alloc(std::map<size_t, size_t> &mems, size_t addr, size_t end) {
    auto low_it = mems.lower_bound(addr);
    auto up_it = mems.upper_bound(addr);

    if (low_it != mems.end() &&
        cover(addr, end, low_it->first, low_it->second)) {
        return false;
    }

    if (up_it != mems.end() &&
        cover(addr, end, up_it->first, up_it->second)) {
        return false;
    }
    mems.insert({addr, end});
    return true;
}

bool del_mem_alloc(std::map<size_t, size_t> &mems, size_t addr) {
    if (mems.empty()) {
        return false;
    }
    auto it = mems.find(addr);
    if (it != mems.end()) {
        mems.erase(it);
        return true;
    }
    return false;
}

int main() {
    std::ifstream fin("./mem.log");
    if (!fin) {
        std::cerr << "Open `mem.log` failure\n";
        return 0;
    }

    std::map<size_t, size_t> mems;

    std::string line;
    size_t line_no = 0;
    while (std::getline(fin, line)) {
        line_no += 1;
        std::string s_addr = line.substr(0, line.find(","));
        std::string s_len = line.substr(line.find(",")+1);
        size_t addr = std::stoul(s_addr, nullptr, 16);
        size_t len = std::stoul(s_len);

        if (len == 0) {
            if (!del_mem_alloc(mems, addr)) {
                std::cout << "Free failure" << std::endl;
                std::cout << line_no << ' ' << line << std::endl;
            }
        } else {
            size_t end = addr + len;
            if (!add_mem_alloc(mems, addr, end)) {
                std::cout << "Malloc failure" << std::endl;
            }
        }
    }
    std::cout << "Success" << std::endl;

    return 0;
}
