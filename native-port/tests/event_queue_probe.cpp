#define EVENT_QUEUE_PROBE
#include "event_queue_tests.cpp"
std::string probe_hex(const std::string& value) {
    std::string out;
    for (unsigned char c : value) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
#include "event_queue_producers.hpp"
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        int profile = 0;
        if (line.starts_with('@')) {
            auto end = line.find(':');
            profile = std::stoi(line.substr(1, end - 1));
            line.erase(0, end + 1);
        }
        Backend b;
        auto m = std::make_shared<EventQueueModule>(b);
        auto s = std::make_shared<Scene>();
        auto c = std::make_shared<CapsSink>();
        framework::Uuid a("00000000-0000-0000-0000-000000000002"), r;
        m->add_region(s);
        for (char op : line) {
            try {
                switch (op) {
                case 'R':
                    m->on_register_caps(a, c);
                    std::cout << *c->handlers.back()->url;
                    break;
                case 'N':
                    m->on_register_caps(a, nullptr);
                    break;
                case 'A':
                    std::cout << m->enqueue(bytes("A"), a);
                    break;
                case 'B':
                    std::cout << m->enqueue(bytes("B"), a);
                    break;
                case 'Z':
                    std::cout << m->enqueue(bytes(""), a);
                    break;
                case 'M':
                    std::cout << m->enqueue(nullptr, a);
                    break;
                case 'U':
                    std::cout << m->enqueue_utf8(nullptr, a);
                    break;
                case 'L':
                    std::cout << m->enqueue_osd(
                        std::make_shared<network::Value>(network::Value::long_integer(-12345678901234)), a);
                    break;
                case 'O':
                    std::cout << m->enqueue_osd(std::make_shared<network::Value>(int32_t(0)), a);
                    break;
                case 'G': {
                    auto v = m->get_events(r, a);
                    std::cout << status(v);
                    if (v->contains("bin_response_data"))
                        std::cout << ":" << probe_hex(body(v));
                    break;
                }
                case 'H':
                    std::cout << m->has_events(r, a);
                    break;
                case 'T':
                    std::cout << status(m->no_events(r, a));
                    break;
                case 'C':
                    m->client_closed(a, nullptr);
                    break;
                case 'D':
                    m->drop(r, a);
                    break;
                case '0':
                    EventQueueProbe::id(*m, a, 0);
                    break;
                case 'X':
                    EventQueueProbe::id(*m, a, std::numeric_limits<int32_t>::max());
                    break;
                case 'Y':
                    EventQueueProbe::id(*m, a, std::numeric_limits<int32_t>::min());
                    break;
                default:
                    produce(*m, a, op, profile);
                    break;
                }
                std::cout << ';';
            } catch (...) {
                std::cout << "EX;";
            }
        }
        std::cout << "|" << b.draws << '\n';
    }
}
