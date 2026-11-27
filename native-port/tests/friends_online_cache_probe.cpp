#include "opensim/scene/friends_online_cache.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace opensim;

framework::Uuid identity(int value) {
    std::ostringstream text;
    text << "00000000-0000-0000-0000-" << std::setw(12) << std::setfill('0') << value;
    return framework::Uuid(text.str());
}

int main() {
    scene::FriendsOnlineCache cache;
    char operation;
    std::string user;
    while (std::cin >> operation >> user) {
        const framework::Uuid principal(user);
        if (operation == 'B' || operation == 'S') {
            int online, count = 1;
            std::cin >> online;
            if (operation == 'B')
                std::cin >> count;
            std::vector<framework::Uuid> friends;
            for (int n = 0, id; n < count; ++n) {
                std::cin >> id;
                friends.push_back(identity(id));
            }
            cache.update(principal, friends, online != 0);
        } else if (operation == 'E')
            cache.erase(principal);
        const auto friends = cache.get(principal);
        if (!friends)
            std::cout << "NULL";
        else
            for (std::size_t n = 0; n < friends->size(); ++n)
                std::cout << (n ? "," : "") << (*friends)[n].str();
        std::cout << '|';
        for (int n = 0; n < 25; ++n)
            std::cout << (cache.contains(principal, identity(n)) ? '1' : '0');
        std::cout << '\n';
    }
}
