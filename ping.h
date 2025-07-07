#ifndef PING_HPP
#define PING_HPP

#include <string>
#include <chrono>
#include <memory>
#include <netinet/ip_icmp.h>
#include <netinet/in.h>

class Ping {
public:
    Ping(const std::string& host, int datalen = 56, int npackets = 0, bool verbose = false);
    ~Ping();

    void run();

private:
    struct PingStatistics {
        int transmitted = 0;
        int received = 0;
        int min_time = 99999999;
        int max_time = 0;
        long total_time = 0;
    };

    void send_ping();
    void receive_ping();
    void alarm_handler();
    void finish();
    void print_packet(const char* buf, int cc, const sockaddr_in* from);
    unsigned short calculate_checksum(unsigned short* ptr, int nbytes);
    void time_subtract(timeval* out, timeval* in);

    std::string hostname;
    std::string dest_ip;
    int sockfd;
    int datalen;
    int packsize;
    int npackets;
    int ident;
    bool verbose;
    bool timing;
    
    char send_packet[4096];
    char recv_packet[4096];
    sockaddr_in dest_addr{};
    PingStatistics stats;
};

#endif // PING_HPP
