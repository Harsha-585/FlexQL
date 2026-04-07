#include "../../include/flexql.h"
#include <iostream>
#include <string>
#include <cstdio>

static int print_callback(void *data, int argc, char **argv, char **azColName) {
    (void)data;
    for (int i = 0; i < argc; i++) {
        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }
    printf("\n");
    return 0;
}

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    int port = 9000;
    
    if (argc >= 3) {
        host = argv[1];
        port = atoi(argv[2]);
    } else if (argc == 2) {
        host = argv[1];
    }
    
    FlexQL *db = nullptr;
    if (flexql_open(host, port, &db) != FLEXQL_OK) {
        std::cerr << "Cannot connect to FlexQL server at " << host << ":" << port << std::endl;
        return 1;
    }
    
    std::cout << "Connected to FlexQL server" << std::endl;
    
    std::string line;
    std::string query;
    
    while (true) {
        if (query.empty()) {
            std::cout << "flexql> ";
        } else {
            std::cout << "   ...> ";
        }
        std::cout.flush();
        
        if (!std::getline(std::cin, line)) {
            break;
        }
        
        // Trim
        while (!line.empty() && std::isspace(line.front())) line.erase(0, 1);
        while (!line.empty() && std::isspace(line.back())) line.pop_back();
        
        if (line.empty()) continue;
        
        // Commands
        if (line == ".exit" || line == ".quit" || line == "exit" || line == "quit") {
            break;
        }
        
        if (line == ".tables") {
            // TODO: implement show tables
            std::cout << "Not yet implemented\n";
            continue;
        }
        
        query += " " + line;
        
        // Check for semicolon to execute
        if (query.find(';') != std::string::npos) {
            char *errMsg = nullptr;
            int rc = flexql_exec_ext(db, query.c_str(), print_callback, nullptr, &errMsg);
            
            if (rc != FLEXQL_OK) {
                std::cerr << "Error: " << (errMsg ? errMsg : "unknown error") << std::endl;
                if (errMsg) flexql_free(errMsg);
            }
            
            query.clear();
        }
    }
    
    std::cout << "Connection closed" << std::endl;
    flexql_close(db);
    return 0;
}
