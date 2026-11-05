// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include <httplib.h>
#include <iostream>
int main(int argc, char** argv) {
    try {
        std::string url = argv[1];
        auto at = url.find('/', url.find("://") + 3);
        httplib::Client client(url.substr(0, at));
        client.set_connection_timeout(10);
        client.set_read_timeout(10);
        auto response = client.Post(at == url.npos ? "/" : url.substr(at), "METHOD=probe",
                                    "application/x-www-form-urlencoded");
        if (!response)
            throw std::runtime_error("request");
        std::string hex;
        for (unsigned char c : response->body) {
            hex += "0123456789abcdef"[c >> 4];
            hex += "0123456789abcdef"[c & 15];
        }
        std::cout << "OK|" << hex << std::endl;
    } catch (...) {
        std::cout << "EX" << std::endl;
    }
    return 0;
}
