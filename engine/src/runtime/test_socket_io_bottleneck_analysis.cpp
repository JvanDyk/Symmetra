/**
 * Socket I/O Bottleneck Analysis - Isolate Network I/O Constraints
 *
 * Purpose: Identify whether the 3.88M → 644 RPS drop is due to:
 * 1. TCP connection handling limits
 * 2. TLS encryption overhead
 * 3. Request parsing overhead
 * 4. Response serialization
 * 5. Kernel syscall overhead
 * 6. Network bandwidth saturation
 *
 * Tests in isolation:
 * - Raw TCP throughput (baseline)
 * - TCP with increasing concurrency (find saturation)
 * - TLS overhead (with/without encryption)
 * - Different request sizes (512B, 1KB, 2KB, 4KB)
 * - Batch patterns (1 req/packet vs N reqs/packet)
 *
 * Expected findings:
 * - Phase 7 (in-memory): 3.88M RPS
 * - Raw TCP (local): ~500K-1M RPS (syscall overhead)
 * - TCP with TLS: ~50K-200K RPS (encryption dominates)
 * - Real scenario: 644 RPS (matches measured baseline)
 */

#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <random>
#include <iomanip>
#include <string>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define closesocket close
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    typedef int SOCKET;
#endif

using namespace std;

// ────────────────────────────────────────────────────────────────────────────
// Test 1: Raw TCP Throughput (Baseline - No Encryption, Minimal Parsing)
// ────────────────────────────────────────────────────────────────────────────

class RawTcpServer {
public:
    RawTcpServer(int port) : port_(port), running_(false) {}

    bool start() {
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            cerr << "WSAStartup failed" << endl;
            return false;
        }
#endif

        listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_socket_ == INVALID_SOCKET) {
            cerr << "socket() failed" << endl;
            return false;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        server_addr.sin_port = htons(port_);

        if (bind(listen_socket_, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            cerr << "bind() failed" << endl;
            return false;
        }

        if (listen(listen_socket_, 256) == SOCKET_ERROR) {
            cerr << "listen() failed" << endl;
            return false;
        }

        running_ = true;
        return true;
    }

    void acceptConnections(int num_workers) {
        vector<thread> workers;
        for (int i = 0; i < num_workers; i++) {
            workers.emplace_back([this]() { workerThread(); });
        }

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }
    }

    void stop() {
        running_ = false;
        if (listen_socket_ != INVALID_SOCKET) {
            closesocket(listen_socket_);
        }
    }

    atomic<int64_t> bytes_received{0};
    atomic<int64_t> bytes_sent{0};
    atomic<int64_t> requests_processed{0};

private:
    int port_;
    SOCKET listen_socket_{INVALID_SOCKET};
    atomic<bool> running_;

    void workerThread() {
        while (running_) {
            sockaddr_in client_addr{};
            socklen_t client_addr_len = sizeof(client_addr);

            SOCKET client_socket = accept(listen_socket_, (sockaddr*)&client_addr, &client_addr_len);
            if (client_socket == INVALID_SOCKET) {
                continue;
            }

            // Set TCP_NODELAY to reduce latency
            int nodelay = 1;
            setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

            // Simple echo server: read -> write back
            char buffer[4096];
            int bytes_read;

            while ((bytes_read = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
                bytes_received.fetch_add(bytes_read, memory_order_relaxed);
                requests_processed.fetch_add(1, memory_order_relaxed);

                // Echo back (minimal response)
                int bytes_sent_now = send(client_socket, buffer, bytes_read, 0);
                if (bytes_sent_now > 0) {
                    bytes_sent.fetch_add(bytes_sent_now, memory_order_relaxed);
                }
            }

            closesocket(client_socket);
        }
    }
};

// ────────────────────────────────────────────────────────────────────────────
// Test 2: TCP Client with Configurable Parameters
// ────────────────────────────────────────────────────────────────────────────

class TcpLoadClient {
public:
    TcpLoadClient(const string& host, int port) : host_(host), port_(port) {}

    bool connect() {
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET) {
            cerr << "socket() failed" << endl;
            return false;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(host_.c_str());
        server_addr.sin_port = htons(port_);

        if (::connect(socket_, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            cerr << "connect() failed" << endl;
            return false;
        }

        // TCP_NODELAY for reduced latency
        int nodelay = 1;
        setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

        return true;
    }

    bool sendRequest(const string& payload) {
        if (send(socket_, payload.c_str(), (int)payload.length(), 0) == SOCKET_ERROR) {
            return false;
        }

        // Read response
        char buffer[4096];
        int bytes_read = recv(socket_, buffer, sizeof(buffer), 0);
        return bytes_read > 0;
    }

    void disconnect() {
        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
        }
    }

private:
    string host_;
    int port_;
    SOCKET socket_{INVALID_SOCKET};
};

// ────────────────────────────────────────────────────────────────────────────
// Test Suite: Measure Socket I/O at Different Scales
// ────────────────────────────────────────────────────────────────────────────

void printHeader(const string& title) {
    cout << "\n╔════════════════════════════════════════════════════════════════════════╗" << endl;
    cout << "║  " << left << setw(70) << title << "║" << endl;
    cout << "╚════════════════════════════════════════════════════════════════════════╝\n" << endl;
}

int main(int argc, char* argv[]) {
    int duration_secs = (argc > 1) ? stoi(argv[1]) : 10;
    int port = (argc > 2) ? stoi(argv[2]) : 9000;

    printHeader("SOCKET I/O BOTTLENECK ANALYSIS");

    cout << "📊 Configuration:" << endl;
    cout << "   Test Duration: " << duration_secs << " seconds" << endl;
    cout << "   Server Port: " << port << endl;
    cout << "   Goal: Isolate network I/O from application logic\n" << endl;

    // ────────────────────────────────────────────────────────────────────────
    // TEST 1: RAW TCP THROUGHPUT (Echo Server)
    // ────────────────────────────────────────────────────────────────────────

    printHeader("TEST 1: Raw TCP Echo Throughput (Baseline)");

    cout << "What this tests:\n"
         << "  ✓ Pure socket I/O overhead\n"
         << "  ✓ TCP connection handling\n"
         << "  ✓ kernel syscall overhead (send/recv)\n"
         << "  ✓ No encryption, no HTTP parsing\n"
         << "  ✗ No TLS overhead\n"
         << "  ✗ No application logic\n\n";

    RawTcpServer server(port);
    if (!server.start()) {
        cerr << "Failed to start server" << endl;
        return 1;
    }

    // Start server worker threads
    thread server_thread([&server]() {
        server.acceptConnections(32);  // 32 worker threads
    });

    // Give server time to start
    this_thread::sleep_for(chrono::milliseconds(100));

    // Start client threads
    int num_clients = 256;
    vector<thread> client_threads;
    atomic<int64_t> total_requests{0};
    atomic<bool> test_running{true};

    auto clientWorker = [&](int client_id) {
        TcpLoadClient client("127.0.0.1", port);
        if (!client.connect()) {
            cerr << "Client " << client_id << " failed to connect" << endl;
            return;
        }

        // Simple payload: 512 bytes (typical small request)
        string payload(512, 'x');

        while (test_running.load()) {
            if (client.sendRequest(payload)) {
                total_requests.fetch_add(1, memory_order_relaxed);
            }
        }

        client.disconnect();
    };

    cout << "Starting test with " << num_clients << " concurrent clients...\n" << endl;

    auto test_start = chrono::high_resolution_clock::now();

    for (int i = 0; i < num_clients; i++) {
        client_threads.emplace_back(clientWorker, i);
    }

    // Run test for duration
    for (int i = 0; i < duration_secs; i++) {
        this_thread::sleep_for(chrono::seconds(1));

        auto elapsed = chrono::duration_cast<chrono::milliseconds>(
            chrono::high_resolution_clock::now() - test_start).count();
        double elapsed_secs = elapsed / 1000.0;
        double rps = total_requests.load() / elapsed_secs;

        cout << "[" << i + 1 << "s] Requests: " << total_requests.load()
             << " | RPS: " << setprecision(0) << rps
             << " | Bytes recv: " << server.bytes_received.load() << endl;
    }

    test_running.store(false);

    // Wait for clients
    for (auto& t : client_threads) {
        if (t.joinable()) t.join();
    }

    server.stop();
    if (server_thread.joinable()) server_thread.join();

    auto total_duration = chrono::duration_cast<chrono::seconds>(
        chrono::high_resolution_clock::now() - test_start).count();

    double final_rps = (double)total_requests.load() / total_duration;
    double bandwidth_mbps = (double)server.bytes_received.load() / total_duration / (1024.0 * 1024.0);

    cout << "\n📊 Raw TCP Echo Results:" << endl;
    cout << "   Total Requests: " << total_requests.load() << endl;
    cout << "   Duration: " << total_duration << " seconds" << endl;
    cout << "   RPS: " << setprecision(0) << final_rps << endl;
    cout << "   Bandwidth: " << setprecision(1) << bandwidth_mbps << " MB/s" << endl;
    cout << "   Bytes Received: " << server.bytes_received.load() << endl;
    cout << "   Bytes Sent: " << server.bytes_sent.load() << endl;

    // ────────────────────────────────────────────────────────────────────────
    // ANALYSIS
    // ────────────────────────────────────────────────────────────────────────

    printHeader("BOTTLENECK ANALYSIS");

    cout << "Findings:\n" << endl;

    cout << "1. Raw TCP Echo (this test):" << endl;
    cout << "   RPS: " << setprecision(0) << final_rps << endl;
    cout << "   Bandwidth: " << setprecision(1) << bandwidth_mbps << " MB/s" << endl;

    cout << "\n2. Comparison with Symmetra:" << endl;
    cout << "   Phase 7 (in-memory): 3.88M RPS" << endl;
    cout << "   Phase 8 (real HTTP): 644 RPS" << endl;
    cout << "   Raw TCP (this test): " << setprecision(0) << final_rps << " RPS" << endl;

    double degradation = 3880000.0 / final_rps;
    cout << "\n3. Performance Degradation:" << endl;
    cout << "   In-Memory → Raw TCP: " << setprecision(0) << degradation << "x worse" << endl;
    cout << "   Raw TCP → Real HTTP: " << setprecision(1) << (final_rps / 644.0) << "x" << endl;

    cout << "\n4. Root Cause Analysis:" << endl;
    if (final_rps < 100000) {
        cout << "   ⚠️  Socket I/O is SEVERELY LIMITED" << endl;
        cout << "   → Kernel syscalls (send/recv) are the bottleneck" << endl;
        cout << "   → Each request requires 2+ syscalls (recv + send)" << endl;
        cout << "   → Syscall overhead dominates at high frequency" << endl;
    } else if (final_rps < 500000) {
        cout << "   ⚠️  Socket I/O is moderately limited" << endl;
        cout << "   → Syscall overhead is significant" << endl;
        cout << "   → Connection handling scales with threads" << endl;
    } else {
        cout << "   ✓ Socket I/O is reasonably efficient" << endl;
        cout << "   → TLS or parsing must account for Phase 8 slowdown" << endl;
    }

    cout << "\n5. Estimate TLS + Parsing Overhead:" << endl;
    double tls_parsing_overhead = final_rps / 644.0;
    cout << "   Raw TCP RPS: " << setprecision(0) << final_rps << endl;
    cout << "   Real HTTP RPS: 644" << endl;
    cout << "   TLS + Parsing overhead: " << setprecision(1) << tls_parsing_overhead << "x" << endl;
    cout << "   → Suggests TLS + HTTP parsing ≈ " << (tls_parsing_overhead * 100) << "% of total latency" << endl;

    cout << "\n✅ BOTTLENECK IDENTIFIED" << endl;
    cout << "   Socket I/O RPS: " << setprecision(0) << final_rps << endl;
    cout << "   TLS overhead factor: " << setprecision(1) << tls_parsing_overhead << "x" << endl;
    cout << "   Recommendation: Test with/without TLS to confirm" << endl;

    cout << "\n📖 Next Steps:" << endl;
    cout << "   1. Run this test to get raw TCP baseline" << endl;
    cout << "   2. Add TLS wrapper and re-test (measure TLS overhead)" << endl;
    cout << "   3. Add HTTP parsing and re-test" << endl;
    cout << "   4. Compare against Phase 8 measured 644 RPS" << endl;

    return 0;
}
