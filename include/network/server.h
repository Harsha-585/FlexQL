#ifndef FLEXQL_NETWORK_SERVER_H
#define FLEXQL_NETWORK_SERVER_H

#include "../query/executor.h"
#include "../storage/catalog.h"
#include "../cache/lru_cache.h"
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <functional>

namespace flexql {

class Server {
public:
    Server(int port, const std::string& data_dir);
    ~Server();
    
    void start();
    void stop();
    
private:
    int port_;
    std::string data_dir_;
    int server_fd_;
    std::atomic<bool> running_;
    std::vector<std::thread> workers_;
    
    std::unique_ptr<BufferPool> pool_;
    std::unique_ptr<Catalog> catalog_;
    std::unique_ptr<Executor> executor_;
    
    void handle_client(int client_socket);
    void process_sql(int client_socket, const char* sql_data, size_t sql_len);
};

} // namespace flexql

#endif
