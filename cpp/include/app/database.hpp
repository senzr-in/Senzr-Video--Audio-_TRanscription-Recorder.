#pragma once
#include <string>

namespace app {

class Database {
public:
    explicit Database(std::string db_path = "database/edge_gateway.db");
    void init_db() const;

private:
    std::string db_path_;
};

}
