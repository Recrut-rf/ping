#include "ping.h"
#include <iostream>
#include <cstdlib>

void print_usage(const char* progname) {
    std::cerr << "Usage: " << progname << " [-v] host [datalen] [npackets]\n"
              << "Options:\n"
              << "  -v      Verbose output\n"
              << "  datalen Size of data portion (default: 56)\n"
              << "  npackets Number of packets to send (default: unlimited)\n";
}

int main(int argc, char* argv[]) {
    try {
        bool verbose = false;
        std::string host;
        int datalen = 56;
        int npackets = 0;

        int argi = 1;
        while (argi < argc && argv[argi][0] == '-') {
            if (std::string(argv[argi]) == "-v") {
                verbose = true;
            } else {
                print_usage(argv[0]);
                return 1;
            }
            argi++;
        }

        if (argi >= argc) {
            print_usage(argv[0]);
            return 1;
        }

        host = argv[argi++];

        if (argi < argc) {
            datalen = std::atoi(argv[argi++]);
        }

        if (argi < argc) {
            npackets = std::atoi(argv[argi++]);
        }

        Ping ping(host, datalen, npackets, verbose);
        ping.run();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
