#include "f2c/messaging/testing/mosquitto_broker.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h> // NOLINT(modernize-deprecated-headers): kill() is POSIX, not in <csignal>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <random>
#include <stdexcept>
#include <thread>

namespace f2c::messaging::testing {

namespace {

using namespace std::chrono_literals;

std::string executable() {
    if (const char* path = std::getenv("F2C_MOSQUITTO"); // NOLINT(concurrency-mt-unsafe)
        path != nullptr && *path != '\0') {
        return path;
    }
    // Set by CMake; empty when it was not found.
    constexpr const char* found = F2C_MOSQUITTO_EXECUTABLE;
    if (*found == '\0') {
        throw std::runtime_error{
            "mosquitto not found: install it (apt-get install mosquitto) and re-run CMake, or set "
            "F2C_MOSQUITTO"};
    }
    return found;
}

sockaddr_in loopback(std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
}

class Socket {
public:
    Socket() : fd_{::socket(AF_INET, SOCK_STREAM, 0)} {
        if (fd_ < 0) {
            throw std::runtime_error{"socket() failed"};
        }
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&&) = delete;
    Socket& operator=(Socket&&) = delete;
    ~Socket() { ::close(fd_); }

    [[nodiscard]] int fd() const noexcept { return fd_; }

private:
    int fd_;
};

// A port the kernel just handed out and released; nothing else on loopback asks for it by number.
std::uint16_t free_port() {
    const Socket socket;
    auto address = loopback(0);
    socklen_t length = sizeof(address);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast): the BSD socket API
    if (::bind(socket.fd(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::getsockname(socket.fd(), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        throw std::runtime_error{"cannot find a free port"};
    }
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
    return ntohs(address.sin_port);
}

bool accepts_connections(std::uint16_t port) {
    const Socket socket;
    auto address = loopback(port);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): the BSD socket API
    return ::connect(socket.fd(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
}

} // namespace

MosquittoBroker::MosquittoBroker()
    : dir_{std::filesystem::temp_directory_path() / ("f2c-mosquitto-" + std::to_string(::getpid()) +
                                                     "-" + std::to_string(std::random_device{}()))},
      port_{free_port()} {
    std::filesystem::create_directories(dir_);
    std::ofstream{dir_ / "mosquitto.conf"} << "listener " << port_ << " 127.0.0.1\n"
                                           << "allow_anonymous true\n"
                                           << "persistence false\n"
                                           << "log_dest none\n";
    try {
        start();
    } catch (...) {
        // The destructor does not run when the constructor throws.
        std::error_code ignored;
        std::filesystem::remove_all(dir_, ignored);
        throw;
    }
}

MosquittoBroker::~MosquittoBroker() {
    stop();
    std::error_code ignored;
    std::filesystem::remove_all(dir_, ignored);
}

void MosquittoBroker::start() {
    if (pid_ > 0) {
        return;
    }
    const auto exe = executable();
    const auto config = (dir_ / "mosquitto.conf").string();
    std::array<std::string, 3> args{exe, "-c", config};
    std::array<char*, 4> argv{args[0].data(), args[1].data(), args[2].data(), nullptr};
    if (::posix_spawn(&pid_, exe.c_str(), nullptr, nullptr, argv.data(), environ) != 0) {
        pid_ = -1;
        throw std::runtime_error{"cannot start " + exe};
    }
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!accepts_connections(port_)) {
        int status = 0;
        if (::waitpid(pid_, &status, WNOHANG) == pid_) {
            pid_ = -1;
            throw std::runtime_error{exe + " exited at start (port " + std::to_string(port_) + ")"};
        }
        if (std::chrono::steady_clock::now() > deadline) {
            stop();
            throw std::runtime_error{exe + " does not accept connections on port " +
                                     std::to_string(port_)};
        }
        std::this_thread::sleep_for(10ms);
    }
}

void MosquittoBroker::stop() {
    if (pid_ <= 0) {
        return;
    }
    ::kill(pid_, SIGTERM);
    int status = 0;
    ::waitpid(pid_, &status, 0);
    pid_ = -1;
}

std::string MosquittoBroker::uri() const {
    return "tcp://127.0.0.1:" + std::to_string(port_);
}

} // namespace f2c::messaging::testing
