// Native production Darwin lock-policy helper; original-source comparison is separate.
#include "opensim/framework/darwin_file_lock_policy.hpp"
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    using opensim::framework::darwin_shared_write_can_lock;
    if (argc == 2 && std::string(argv[1]) == "--map") {
        std::string name;
        while (std::getline(std::cin, name))
            std::cout << darwin_shared_write_can_lock(name) << '\n';
        return 0;
    }
    if (!darwin_shared_write_can_lock(std::string_view("apfs\0ignored", 12)) ||
        darwin_shared_write_can_lock(std::string_view("smb2\0apfs", 9)) ||
        !darwin_shared_write_can_lock("apfs") || darwin_shared_write_can_lock("unknown") ||
        darwin_shared_write_can_lock("smb2"))
        return 1;
    std::cout << "PASS: production Darwin lock-policy and CString semantics\n";
    return 0;
}
