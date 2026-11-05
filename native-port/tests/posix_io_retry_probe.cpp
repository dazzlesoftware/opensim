#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/directory_entries.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include "opensim/framework/time_zone.hpp"
#include "opensim/framework/invariant_datetime_context.hpp"
using namespace opensim::framework;
int main(int argc, char** argv) {
    try {
        std::string op = argv[1];
        auto path = nini_writer_text(utf16_replacing(argv[2]));
        if (op == "zone-named") {
            auto zone = time_zones().find(std::string("Owned"));
            std::cout << "OK " << zone->daylight_at(1719792000);
        } else if (op == "zone-local") {
            auto context = shared_native_invariant_datetime_context();
            context->clear_cached_time_zone_data();
            std::cout << "OK " << context->local_rule_snapshot()->utc_offset(638554752000000000LL).ticks;
        } else if (op == "list") {
            auto r = directory_get_files(path, nini_writer_text(u"*"));
            std::cout << "OK " << r.size();
        } else {
            auto f = native_nini_file_stream_provider(false)(path, op == "read" ? 3 : 2, op == "read" ? 1 : 2,
                                                             op == "read" ? 1 : 0, 4096, 0);
            if (op == "read") {
                std::uint8_t b[32];
                auto n = f->read(b, 0, 32);
                std::cout << "OK " << n;
            } else {
                std::uint8_t b[] = {65, 66};
                f->write(b);
                f->flush();
                std::cout << "OK";
            }
            f->close();
        }
    } catch (const NiniStreamError& e) {
        std::cout << "ERR " << e.source_type;
    } catch (const DirectoryEntriesError& e) {
        std::cout << "ERR " << e.source_type;
    } catch (const std::exception& e) {
        std::cout << "ERR " << e.what();
    }
    std::cout << "\n";
}
