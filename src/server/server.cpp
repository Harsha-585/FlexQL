#include "../../include/network/server.h"
#include "../../include/parser/parser.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <csignal>
#include <arpa/inet.h>

namespace flexql {

Server::Server(int port, const std::string& data_dir)
    : port_(port), data_dir_(data_dir), server_fd_(-1), running_(false) {
    
    pool_ = std::make_unique<BufferPool>(MAX_PAGES_IN_CACHE);
    catalog_ = std::make_unique<Catalog>(data_dir_, pool_.get());
    executor_ = std::make_unique<Executor>(catalog_.get(), pool_.get());
}

Server::~Server() {
    stop();
}

void Server::start() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "Failed to create socket\n";
        return;
    }
    
    // Allow address reuse
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);
    
    if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind to port " << port_ << "\n";
        close(server_fd_);
        return;
    }
    
    if (listen(server_fd_, 64) < 0) {
        std::cerr << "Failed to listen\n";
        close(server_fd_);
        return;
    }
    
    running_ = true;
    std::cout << "FlexQL Server running on port " << port_ << std::endl;
    
    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (running_) {
                std::cerr << "Failed to accept connection\n";
            }
            continue;
        }
        
        // Optimize client socket for low latency
        int flag = 1;
        setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        int bufsize = 1048576; // 1MB socket buffers
        setsockopt(client_socket, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
        setsockopt(client_socket, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
        
        std::cout << "Client connected\n";
        
        // Handle client in a new thread
        workers_.emplace_back(&Server::handle_client, this, client_socket);
        workers_.back().detach();
    }
}

void Server::stop() {
    running_ = false;
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    
    // Save catalog
    if (catalog_) {
        catalog_->save();
    }
}

void Server::handle_client(int client_socket) {
    std::string pending;
    pending.reserve(4 * 1024 * 1024); // 4MB pre-allocate for large batch inserts
    char buffer[262144]; // 256KB read buffer for large batches
    ssize_t bytes_read;
    
    while ((bytes_read = read(client_socket, buffer, sizeof(buffer))) > 0) {
        pending.append(buffer, bytes_read);
        
        // Process complete statements (delimited by ';')
        size_t start = 0;
        size_t pos;
        while ((pos = pending.find(';', start)) != std::string::npos) {
            // Find start of actual SQL (skip leading whitespace)
            size_t sql_start = start;
            while (sql_start < pos && std::isspace(pending[sql_start])) sql_start++;
            
            if (sql_start < pos) {
                // Process SQL using zero-copy pointer+length
                process_sql(client_socket, pending.data() + sql_start, pos - sql_start + 1);
            }
            start = pos + 1;
        }
        
        // Keep only unprocessed data
        if (start > 0) {
            pending.erase(0, start);
        }
    }
    
    std::cout << "Client disconnected\n";
    close(client_socket);
}

void Server::process_sql(int client_socket, const char* sql_data, size_t sql_len) {
    try {
        Parser parser(sql_data, sql_len);
        Statement stmt = parser.parse();
        QueryResult result = executor_->execute(stmt);
        
        if (!result.success) {
            std::string error_msg = "ERROR: " + result.error + "\nEND\n";
            send(client_socket, error_msg.c_str(), error_msg.size(), 0);
            return;
        }
        
        // Pre-allocate response buffer for better performance
        std::string response;
        size_t estimated_size = 64; // Base overhead
        if (!result.column_names.empty()) {
            estimated_size += result.column_names.size() * 20; // Avg column name
        }
        estimated_size += result.rows.size() * result.column_names.size() * 16; // Avg value
        response.reserve(estimated_size);
        
        // Send column names
        if (!result.column_names.empty()) {
            response = "COLS";
            for (const auto& c : result.column_names) {
                response += '\t';
                response += c;
            }
            response += '\n';
            send(client_socket, response.c_str(), response.size(), 0);
        }
        
        // Send result rows - batch small rows together
        for (const auto& row : result.rows) {
            response.clear();
            response = "ROW";
            for (const auto& val : row.values) {
                response += '\t';
                response += val;
            }
            response += '\n';
            send(client_socket, response.c_str(), response.size(), 0);
        }
        
        send(client_socket, "OK\nEND\n", 7, 0);
        
    } catch (const std::exception& e) {
        std::string error_msg = "ERROR: ";
        error_msg += e.what();
        error_msg += "\nEND\n";
        send(client_socket, error_msg.c_str(), error_msg.size(), 0);
    }
}

} // namespace flexql
