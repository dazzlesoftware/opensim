#include "opensim/framework/time_zone.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
int main() {
    opensim::framework::TimeZoneDatabase db;
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            std::istringstream in(line);
            std::string name;
            long long stamp;
            in >> name >> stamp;
            std::optional<std::string> id;
            if (name != "-" && name != "local") {
                id = "";
                if (name != ".")
                    for (std::size_t i = 0; i < name.size(); i += 2)
                        id->push_back(static_cast<char>(std::stoi(name.substr(i, 2), nullptr, 16)));
            }
            auto zone = name == "local" ? db.local() : db.find(id);
            std::cout << (zone->daylight_at(stamp) ? "Y" : "N") << "\n";
        } catch (const std::exception&) {
            std::cout << "E\n";
        }
    }
}
