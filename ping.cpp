#include "ping.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>

namespace {
volatile sig_atomic_t alarm_flag = 0;

void alarm_signal_handler(int) {
    alarm_flag = 1;
}
}

Ping::Ping(const std::string& host, int datalen, int npackets, bool verbose) 
    : hostname(host), datalen(datalen), npackets(npackets), verbose(verbose) {
    
    // Initialize destination address
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;

    // Try to convert host as IP address first
    if (inet_pton(AF_INET, host.c_str(), &dest_addr.sin_addr) != 1) {
        // If not IP, try to resolve hostname
        hostent* he = gethostbyname(host.c_str());
        if (!he) {
            throw std::runtime_error("Failed to resolve host: " + host);
        }
        memcpy(&dest_addr.sin_addr, he->h_addr, he->h_length);
        hostname = he->h_name;
    }

    dest_ip = inet_ntoa(dest_addr.sin_addr);

    // Calculate packet size
    packsize = datalen + sizeof(icmphdr);
    if (packsize > sizeof(send_packet)) {
        throw std::runtime_error("Packet size too large");
    }

    timing = (datalen >= sizeof(timeval));
    ident = getpid() & 0xFFFF;

    // Create raw socket
    sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        throw std::runtime_error("Failed to create raw socket (try running as root)");
    }

    // Set socket timeout
    timeval tv{1, 0}; // 1 second timeout
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::cout << "PING " << hostname << " (" << dest_ip << "): "
              << datalen << " data bytes" << std::endl;
}

Ping::~Ping() {
    if (sockfd >= 0) {
        close(sockfd);
    }
}

void Ping::run() {
    // Set up signal handlers
    signal(SIGALRM, alarm_signal_handler);
    signal(SIGINT, [](int) { exit(0); });

    // Send first ping immediately
    send_ping();
    alarm(1); // Schedule next ping in 1 second

    while (true) {
        if (alarm_flag) {
            alarm_flag = 0;
            alarm_handler();
        }

        receive_ping();

        if (npackets > 0 && stats.received >= npackets) {
            finish();
            break;
        }
    }
}

void Ping::send_ping() {
    icmphdr* icmp = reinterpret_cast<icmphdr*>(send_packet);
    
    // Fill in ICMP header
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->un.echo.id = htons(ident);
    icmp->un.echo.sequence = htons(stats.transmitted);
    
    // Add timestamp if timing is enabled
    if (timing) {
        timeval* tv = reinterpret_cast<timeval*>(send_packet + sizeof(icmphdr));
        gettimeofday(tv, nullptr);
    }

    // Fill data with incrementing pattern
    for (int i = sizeof(icmphdr) + sizeof(timeval); i < packsize; i++) {
        send_packet[i] = i & 0xFF;
    }

    // Calculate checksum
    icmp->checksum = calculate_checksum(reinterpret_cast<unsigned short*>(icmp), packsize);

    // Send packet
    if (sendto(sockfd, send_packet, packsize, 0,
               reinterpret_cast<sockaddr*>(&dest_addr), sizeof(dest_addr)) <= 0) {
        if (verbose) {
            std::cerr << "Failed to send ping packet" << std::endl;
        }
        return;
    }

    stats.transmitted++;
}

void Ping::receive_ping() {
    sockaddr_in from{};
    socklen_t fromlen = sizeof(from);
    
    int cc = recvfrom(sockfd, recv_packet, sizeof(recv_packet), 0,
                      reinterpret_cast<sockaddr*>(&from), &fromlen);
    
    if (cc < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && verbose) {
            std::cerr << "Error receiving packet: " << strerror(errno) << std::endl;
        }
        return;
    }

    print_packet(recv_packet, cc, &from);
}

void Ping::alarm_handler() {
    if (npackets == 0 || stats.transmitted < npackets) {
        send_ping();
        alarm(1);
    } else {
        // Wait for remaining replies
        int waittime = stats.received ? (2 * stats.max_time / 1000) : 10;
        if (waittime == 0) waittime = 1;
        
        signal(SIGALRM, [](int) { exit(0); });
        alarm(waittime);
    }
}

void Ping::finish() {
    std::cout << "\n--- " << hostname << " ping statistics ---" << std::endl;
    std::cout << stats.transmitted << " packets transmitted, "
              << stats.received << " packets received, ";
    
    if (stats.transmitted > 0) {
        int loss = ((stats.transmitted - stats.received) * 100) / stats.transmitted;
        std::cout << loss << "% packet loss" << std::endl;
    }
    
    if (stats.received > 0 && timing) {
        int avg = stats.total_time / stats.received;
        std::cout << "round-trip min/avg/max = " << stats.min_time << "/"
                  << avg << "/" << stats.max_time << " ms" << std::endl;
    }
}

unsigned short Ping::calculate_checksum(unsigned short* ptr, int nbytes) {
    long sum = 0;
    unsigned short oddbyte;
    unsigned short answer;

    while (nbytes > 1) {
        sum += *ptr++;
        nbytes -= 2;
    }

    if (nbytes == 1) {
        oddbyte = 0;
        *((unsigned char*)&oddbyte) = *(unsigned char*)ptr;
        sum += oddbyte;
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = ~sum;

    return answer;
}

void Ping::print_packet(const char* buf, int cc, const sockaddr_in* from) {
    iphdr* ip = (iphdr*)buf;
    int iphdrlen = ip->ihl * 4;

    if (cc < iphdrlen + ICMP_MINLEN) {
        if (verbose) {
            std::cerr << "packet too short (" << cc << " bytes) from "
                      << inet_ntoa(from->sin_addr) << std::endl;
        }
        return;
    }

    icmphdr* icmp = (icmphdr*)(buf + iphdrlen);

    if (icmp->type != ICMP_ECHOREPLY) {
        if (verbose) {
            std::cerr << cc << " bytes from " << inet_ntoa(from->sin_addr)
                      << ": icmp_type=" << (int)icmp->type << std::endl;
        }
        return;
    }

    if (ntohs(icmp->un.echo.id) != ident) {
        return;
    }

    std::cout << cc << " bytes from " << inet_ntoa(from->sin_addr)
              << ": icmp_seq=" << ntohs(icmp->un.echo.sequence);

    if (timing) {
        timeval* tv_send = (timeval*)(send_packet + sizeof(icmphdr));
        timeval tv_recv;
        gettimeofday(&tv_recv, nullptr);

        time_subtract(&tv_recv, tv_send);
        int triptime = tv_recv.tv_sec * 1000 + (tv_recv.tv_usec / 1000);

        std::cout << " time=" << triptime << " ms";

        stats.total_time += triptime;
        if (triptime < stats.min_time) stats.min_time = triptime;
        if (triptime > stats.max_time) stats.max_time = triptime;
    }

    std::cout << std::endl;
    stats.received++;
}

void Ping::time_subtract(timeval* out, timeval* in) {
    if ((out->tv_usec -= in->tv_usec) < 0) {
        out->tv_sec--;
        out->tv_usec += 1000000;
    }
    out->tv_sec -= in->tv_sec;
}
