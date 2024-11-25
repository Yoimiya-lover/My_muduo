#include <muduo/base/Logging.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <atomic>

using namespace muduo;
using namespace muduo::net;

class EchoClient {
public:
    EchoClient(EventLoop* loop, const InetAddress& serverAddr)
        : client_(loop, serverAddr, "EchoClient"),
          messageCount_(0),
          startTime_(Timestamp::now()),
          isCompleted_(false) {
        client_.setConnectionCallback(std::bind(&EchoClient::onConnection, this, std::placeholders::_1));
        client_.setMessageCallback(std::bind(&EchoClient::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }

    void start() {
        client_.connect();
    }

    int getMessageCount() const { return messageCount_; }

    double getTestDuration() const {
        // Convert microseconds to seconds
        return static_cast<double>(Timestamp::now().microSecondsSinceEpoch() - startTime_.microSecondsSinceEpoch()) / 1e6;
    }

    bool isCompleted() const { return isCompleted_; }

private:
    void onConnection(const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            // Send a message to start the echo test
            std::string message = "Hello from client!";
            conn->send(message);
        } else {
            // Close connection after message exchange
            conn->shutdown();
            isCompleted_ = true; // Mark as completed after connection shutdown
        }
    }

    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp receiveTime) {
        ++messageCount_;
        // Send another message to simulate continuous traffic
        std::string message = "Hello from client!";
        conn->send(message);

        // 假设我们让每个客户端在发送了 10 条消息后主动断开连接
        if (messageCount_ >= 10) {
            LOG_INFO << "Client " << conn->peerAddress().toIpPort() << " has sent 10 messages, shutting down connection.";
            conn->shutdown();  // 主动关闭连接
            isCompleted_ = true; // Mark as completed after shutdown
        }
    }

    TcpClient client_;
    std::atomic<int> messageCount_;  // Atomic to ensure thread-safe access
    Timestamp startTime_;
    bool isCompleted_;  // To track if client completes the test
};

void runTest(const std::string& ip, uint16_t port, int connectionCount, int duration) {
    EventLoop loop;
    InetAddress serverAddr(ip, port);
    
    std::vector<std::shared_ptr<EchoClient>> clients;
    
    // Create multiple clients for stress testing
    for (int i = 0; i < connectionCount; ++i) {
        clients.push_back(std::make_shared<EchoClient>(&loop, serverAddr));
    }

    // Start all clients
    for (auto& client : clients) {
        client->start();
    }

    // Run the event loop for the specified duration
    loop.runAfter(static_cast<double>(duration), [&loop]() {
        loop.quit();
    });

    loop.loop();

    // Calculate and output the results
    int totalMessages = 0;
    double totalDuration = 0.0;
    int successfulClients = 0;
    for (auto& client : clients) {
        totalMessages += client->getMessageCount();
        totalDuration += client->getTestDuration();
        if (client->isCompleted()) {
            ++successfulClients;
        }
    }

    double avgDuration = totalDuration / connectionCount;
    double throughput = totalMessages / totalDuration;  // messages per second
    double successRate = (successfulClients / static_cast<double>(connectionCount)) * 100;

    std::cout << "Test results:" << std::endl;
    std::cout << "Total messages sent: " << totalMessages << std::endl;
    std::cout << "Average duration per client: " << avgDuration << " seconds" << std::endl;
    std::cout << "Throughput (messages per second): " << throughput << std::endl;
    std::cout << "Success rate: " << successRate << "%" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <IP> <Port> <ConnectionCount> <Duration>" << std::endl;
        return 1;
    }

    std::string ip = argv[1];
    uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));
    int connectionCount = std::stoi(argv[3]);
    int duration = std::stoi(argv[4]);

    // Disable muduo logging output
    muduo::Logger::setLogLevel(muduo::Logger::FATAL);

    // Run the test
    runTest(ip, port, connectionCount, duration);

    return 0;
}
