#include "../../include/flexql.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>

struct FlexQL {
    int sock;
};

extern "C" {

int flexql_open(const char *host, int port, FlexQL **outDb) {
    if (!host || !outDb) return FLEXQL_ERROR;
    
    FlexQL *db = (FlexQL*)malloc(sizeof(FlexQL));
    if (!db) return FLEXQL_ERROR;
    
    db->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (db->sock < 0) {
        free(db);
        return FLEXQL_ERROR;
    }
    
    // Disable Nagle's algorithm for lower latency
    int flag = 1;
    setsockopt(db->sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    // Increase socket buffer sizes to 1MB
    int bufsize = 1048576; // 1MB
    setsockopt(db->sock, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
    setsockopt(db->sock, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
        close(db->sock);
        free(db);
        return FLEXQL_ERROR;
    }
    
    if (connect(db->sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(db->sock);
        free(db);
        return FLEXQL_ERROR;
    }
    
    *outDb = db;
    return FLEXQL_OK;
}

int flexql_close(FlexQL *db) {
    if (!db) return FLEXQL_ERROR;
    close(db->sock);
    free(db);
    return FLEXQL_OK;
}

static int internal_exec(
    FlexQL *db,
    const char *sql,
    int (*callback)(void*, int, char**, char**),
    void *arg,
    char **errmsg,
    bool is_ext
) {
    if (!db || !sql) {
        if (errmsg) {
            const char *msg = "Invalid database handle or SQL";
            *errmsg = (char*)malloc(strlen(msg) + 1);
            if (*errmsg) strcpy(*errmsg, msg);
        }
        return FLEXQL_ERROR;
    }
    
    // Send SQL to server
    size_t sql_len = strlen(sql);
    ssize_t sent = send(db->sock, sql, sql_len, 0);
    if (sent < 0) {
        if (errmsg) {
            const char *msg = "send failed (socket closed by server)";
            *errmsg = (char*)malloc(strlen(msg) + 1);
            if (*errmsg) strcpy(*errmsg, msg);
        }
        return FLEXQL_ERROR;
    }
    
    // Read response
    std::string pending;
    pending.reserve(256);  // Small reserve for OK response
    char buffer[4096];     // Smaller buffer for faster response
    ssize_t valread;
    bool done = false;
    bool hasError = false;
    std::string errorText;
    
    // Parse column names from first ROW to supply columnNames
    // The benchmark uses ROW val1 val2 ... format
    // We need to parse ROW lines and call callback
    
    std::vector<std::string> col_names;
    
    while (!done && (valread = read(db->sock, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[valread] = '\0';
        pending.append(buffer, valread);
        
        size_t newlinePos;
        while ((newlinePos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, newlinePos);
            pending.erase(0, newlinePos + 1);
            
            // Remove trailing \r if present
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            
            if (line == "END") {
                done = true;
                break;
            }
            
            if (line.rfind("ERROR:", 0) == 0) {
                hasError = true;
                errorText = line;
                continue;
            }
            
            if (line.rfind("COLS\t", 0) == 0) {
                std::string colsData = line.substr(5);
                col_names.clear();
                size_t pos = 0;
                while ((pos = colsData.find('\t')) != std::string::npos) {
                    col_names.push_back(colsData.substr(0, pos));
                    colsData.erase(0, pos + 1);
                }
                if (!colsData.empty()) col_names.push_back(colsData);
                continue;
            }
            
            if (callback && line.rfind("ROW\t", 0) == 0) {
                std::string rowData = line.substr(4);
                std::vector<std::string> values;
                size_t pos = 0;
                while ((pos = rowData.find('\t')) != std::string::npos) {
                    values.push_back(rowData.substr(0, pos));
                    rowData.erase(0, pos + 1);
                }
                if (!rowData.empty()) values.push_back(rowData);
                
                int argc = values.size();
                if (argc == 0) continue; // safety
                
                // Both ext and non-ext modes now pass individual columns
                std::vector<char*> argv(argc);
                std::vector<char*> col(argc);
                
                for (int i = 0; i < argc; i++) {
                    argv[i] = const_cast<char*>(values[i].c_str());
                    if (i < (int)col_names.size()) {
                        col[i] = const_cast<char*>(col_names[i].c_str());
                    } else {
                        col[i] = (char*)"unknown";
                    }
                }
                int rc = callback(arg, argc, argv.data(), col.data());
                if (rc != 0) break;
            }
        }
    }
    
    if (!done && valread <= 0) {
        if (errmsg) {
            const char *msg = "connection closed before END";
            *errmsg = (char*)malloc(strlen(msg) + 1);
            if (*errmsg) strcpy(*errmsg, msg);
        }
        return FLEXQL_ERROR;
    }
    
    if (hasError) {
        if (errmsg) {
            *errmsg = (char*)malloc(errorText.size() + 1);
            if (*errmsg) strcpy(*errmsg, errorText.c_str());
        }
        return FLEXQL_ERROR;
    }
    
    if (errmsg) *errmsg = nullptr;
    return FLEXQL_OK;
}

int flexql_exec(
    FlexQL *db,
    const char *sql,
    int (*callback)(void*, int, char**, char**),
    void *arg,
    char **errmsg
) {
    return internal_exec(db, sql, callback, arg, errmsg, false);
}

int flexql_exec_ext(
    FlexQL *db,
    const char *sql,
    int (*callback)(void*, int, char**, char**),
    void *arg,
    char **errmsg
) {
    return internal_exec(db, sql, callback, arg, errmsg, true);
}

void flexql_free(void *ptr) {
    free(ptr);
}

} // extern "C"
