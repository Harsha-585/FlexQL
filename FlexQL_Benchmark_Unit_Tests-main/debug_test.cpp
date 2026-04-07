#include <iostream>
#include "flexql.h"

using namespace std;

int main() {
    FlexQL *db;
    cout << "Attempting to connect to FlexQL server on 127.0.0.1:9000...\n";

    if (flexql_open("127.0.0.1", 9000, &db) != FLEXQL_OK) {
        cout << "Cannot open FlexQL\n";
        return 1;
    }

    cout << "Connected to FlexQL!\n";

    // Try a simple query
    char *errMsg = nullptr;
    cout << "Testing simple CREATE TABLE...\n";
    int rc = flexql_exec(db, "CREATE TABLE test_debug (id INT PRIMARY KEY)", nullptr, nullptr, &errMsg);

    if (rc == FLEXQL_OK) {
        cout << "CREATE TABLE: SUCCESS\n";
    } else {
        cout << "CREATE TABLE: FAILED - " << (errMsg ? errMsg : "unknown error") << "\n";
        if (errMsg) flexql_free(errMsg);
    }

    cout << "Testing INSERT...\n";
    rc = flexql_exec(db, "INSERT INTO test_debug VALUES (123)", nullptr, nullptr, &errMsg);

    if (rc == FLEXQL_OK) {
        cout << "INSERT: SUCCESS\n";
    } else {
        cout << "INSERT: FAILED - " << (errMsg ? errMsg : "unknown error") << "\n";
        if (errMsg) flexql_free(errMsg);
    }

    cout << "Testing SELECT...\n";
    rc = flexql_exec(db, "SELECT * FROM test_debug", nullptr, nullptr, &errMsg);

    if (rc == FLEXQL_OK) {
        cout << "SELECT: SUCCESS\n";
    } else {
        cout << "SELECT: FAILED - " << (errMsg ? errMsg : "unknown error") << "\n";
        if (errMsg) flexql_free(errMsg);
    }

    flexql_close(db);
    cout << "Test complete!\n";

    return 0;
}