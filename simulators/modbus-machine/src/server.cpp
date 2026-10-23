#include "server.hpp"

#include <modbus.h>
#include <netinet/in.h>
#include <spdlog/spdlog.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "filler_model.hpp"
#include "register_map.hpp"

namespace f2c::sim {

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr auto health_interval = 5s;
constexpr auto stop_poll_interval = 100ms; // bounds the reaction to `stop` set from another thread
constexpr int listen_backlog = 8;
constexpr std::size_t max_clients = 32;
constexpr std::uint32_t request_timeout_us =
    1'000'000; // a client that stalls mid-request is dropped

struct ContextDeleter {
    void operator()(modbus_t* ctx) const noexcept { modbus_free(ctx); }
};
struct MappingDeleter {
    void operator()(modbus_mapping_t* mapping) const noexcept { modbus_mapping_free(mapping); }
};
using Context = std::unique_ptr<modbus_t, ContextDeleter>;
using Mapping = std::unique_ptr<modbus_mapping_t, MappingDeleter>;

// Closes the listening socket and every client socket on scope exit.
class Sockets {
public:
    Sockets() = default;
    Sockets(const Sockets&) = delete;
    Sockets& operator=(const Sockets&) = delete;
    Sockets(Sockets&&) = delete;
    Sockets& operator=(Sockets&&) = delete;
    ~Sockets() {
        for (const int fd : clients) {
            ::close(fd);
        }
        if (listener >= 0) {
            ::close(listener);
        }
    }

    void drop(int fd) {
        ::close(fd);
        std::erase(clients, fd);
    }

    int listener{-1};
    std::vector<int> clients;
};

std::string last_modbus_error() {
    return modbus_strerror(errno);
}

void write_image(modbus_mapping_t& mapping, const RegisterImage& image) {
    std::ranges::copy(image.holding, mapping.tab_registers);
    std::ranges::copy(image.input, mapping.tab_input_registers);
    std::ranges::copy(image.coils, mapping.tab_bits);
}

RegisterImage read_image(const modbus_mapping_t& mapping) {
    RegisterImage image;
    std::copy_n(mapping.tab_registers, image.holding.size(), image.holding.begin());
    std::copy_n(mapping.tab_input_registers, image.input.size(), image.input.begin());
    std::copy_n(mapping.tab_bits, image.coils.size(), image.coils.begin());
    return image;
}

void touch(const std::filesystem::path& path) {
    {
        const std::ofstream file{path, std::ios::app}; // creates the file if needed
    }
    std::error_code ec;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
    if (ec) {
        spdlog::warn("cannot touch health file {}: {}", path.string(), ec.message());
    }
}

// The port a listening socket is bound to, or nullopt if it cannot be read.
std::optional<std::uint16_t> bound_port(int listener) {
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): the sockets API takes sockaddr*
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return std::nullopt;
    }
    return ntohs(address.sin_port);
}

timeval to_timeval(Clock::duration duration) {
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::max(duration, {}));
    return timeval{.tv_sec = static_cast<time_t>(us.count() / 1'000'000),
                   .tv_usec = static_cast<suseconds_t>(us.count() % 1'000'000)};
}

} // namespace

core::Expected<void> run_server(const SimulatorOptions& options, std::uint32_t seed,
                                const std::atomic<bool>& stop,
                                const std::function<void(std::uint16_t port)>& listening) {
    const Context ctx{modbus_new_tcp(options.bind_address.c_str(), options.port)};
    if (!ctx) {
        return core::make_error(core::Errc::invalid_argument,
                                "modbus_new_tcp: " + last_modbus_error());
    }
    modbus_set_slave(ctx.get(), 1);
    modbus_set_indication_timeout(ctx.get(), 0, request_timeout_us);

    const RegisterImage layout{};
    const Mapping mapping{modbus_mapping_new(static_cast<int>(layout.coils.size()), 0,
                                             static_cast<int>(layout.holding.size()),
                                             static_cast<int>(layout.input.size()))};
    if (!mapping) {
        return core::make_error(core::Errc::io_error, "modbus_mapping_new: " + last_modbus_error());
    }

    Sockets sockets;
    sockets.listener = modbus_tcp_listen(ctx.get(), listen_backlog);
    if (sockets.listener < 0) {
        return core::make_error(core::Errc::io_error, "cannot listen on " + options.bind_address +
                                                          ":" + std::to_string(options.port) +
                                                          ": " + last_modbus_error());
    }
    const auto port = bound_port(sockets.listener);
    if (!port) {
        return core::make_error(core::Errc::io_error, "getsockname: " + last_modbus_error());
    }
    spdlog::info("listening on {}:{} (seed {}, step {} ms)", options.bind_address, *port, seed,
                 options.step_period.count());

    FillerModel model{seed};
    RegisterImage published = to_registers(model.outputs());
    write_image(*mapping, published);
    const auto publish = [&] {
        published = to_registers(model.outputs());
        write_image(*mapping, published);
    };

    auto next_step = Clock::now() + options.step_period;
    auto next_health = Clock::now();
    std::array<std::uint8_t, MODBUS_TCP_MAX_ADU_LENGTH> query{};
    if (listening) {
        listening(*port);
    }

    while (!stop.load()) {
        const auto now = Clock::now();
        if (now >= next_step) {
            model.step(options.step_period);
            publish();
            next_step += options.step_period;
            if (next_step <= now) { // fell behind: skip the missed steps instead of bursting
                next_step = now + options.step_period;
            }
        }
        if (options.health_file && now >= next_health) {
            touch(*options.health_file);
            next_health = now + health_interval;
        }

        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(sockets.listener, &readable);
        int max_fd = sockets.listener;
        for (const int fd : sockets.clients) {
            FD_SET(fd, &readable);
            max_fd = std::max(max_fd, fd);
        }
        auto wake = std::min(next_step, Clock::now() + stop_poll_interval);
        if (options.health_file) {
            wake = std::min(wake, next_health);
        }
        auto timeout = to_timeval(wake - Clock::now());

        const int ready = ::select(max_fd + 1, &readable, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue; // a signal: re-check `stop`
            }
            return core::make_error(core::Errc::io_error,
                                    "select: " +
                                        std::error_code{errno, std::generic_category()}.message());
        }

        if (FD_ISSET(sockets.listener, &readable)) {
            int listener = sockets.listener;
            const int fd = modbus_tcp_accept(ctx.get(), &listener);
            if (fd < 0) {
                spdlog::warn("accept failed: {}", last_modbus_error());
            } else if (fd >= FD_SETSIZE || sockets.clients.size() >= max_clients) {
                spdlog::warn("too many clients, refusing a connection");
                ::close(fd);
            } else {
                sockets.clients.push_back(fd);
                spdlog::info("client connected ({} open)", sockets.clients.size());
            }
        }

        // Iterate over a copy: dropping a client modifies the list.
        for (const int fd : std::vector<int>{sockets.clients}) {
            if (!FD_ISSET(fd, &readable)) {
                continue;
            }
            modbus_set_socket(ctx.get(), fd);
            const int length = modbus_receive(ctx.get(), query.data());
            if (length > 0 && modbus_reply(ctx.get(), query.data(), length, mapping.get()) >= 0) {
                apply_client_writes(model, read_image(*mapping), published);
                publish();            // overwrites writes to read-only registers right away
            } else if (length != 0) { // 0: request not for us, ignored
                sockets.drop(fd);
                spdlog::info("client disconnected ({} open)", sockets.clients.size());
            }
        }
    }

    spdlog::info("stopping");
    return {};
}

} // namespace f2c::sim
