#include <queue>

class Connection {
public:
    Connection(int id) : id(id) {
        std::cout << "Connection " << id << " created.\n";
    }
    ~Connection() {
        std::cout << "Connection " << id << " destroyed.\n";
    }

    void use() {
        std::cout << "Connection " << id << " is being used.\n";
    }

    int id;
};


class ConnectionPoolAbstract {
public:
    virtual std::shared_ptr<Connection> borrowConnection() = 0;   
    ConnectionPoolAbstract(size_t poolSize) : poolSize(poolSize) {}
protected:
    size_t poolSize; 
    std::queue<std::unique_ptr<Connection>> pool;
};

