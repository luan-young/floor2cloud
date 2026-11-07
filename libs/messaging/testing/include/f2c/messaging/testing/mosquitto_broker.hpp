#pragma once

#include <sys/types.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace f2c::messaging::testing {

// A Mosquitto broker in a child process on a free loopback port, for component tests (ADR-0003).
// The executable is $F2C_MOSQUITTO if set, else the one CMake found (`apt-get install mosquitto`).
// Throws std::runtime_error if it cannot be started; the destructor stops it.
class MosquittoBroker {
public:
    MosquittoBroker(); // starts it
    MosquittoBroker(const MosquittoBroker&) = delete;
    MosquittoBroker& operator=(const MosquittoBroker&) = delete;
    MosquittoBroker(MosquittoBroker&&) = delete;
    MosquittoBroker& operator=(MosquittoBroker&&) = delete;
    ~MosquittoBroker();

    // Returns once it accepts connections; same port every time.
    void start();
    // SIGTERM and wait: clients see their connection drop. No-op if stopped.
    void stop();

    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
    [[nodiscard]] std::string uri() const; // tcp://127.0.0.1:<port>

private:
    std::filesystem::path dir_; // config file
    std::uint16_t port_{0};
    pid_t pid_{-1};
};

} // namespace f2c::messaging::testing
