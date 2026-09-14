#include "decrypt_tcp.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

namespace wrapper {

namespace {

constexpr std::uint32_t kMagic       = 0x57563244u;  // "WV2D"
constexpr std::uint16_t kVersion     = 1;
constexpr std::uint16_t kKindBatch   = 1;
constexpr std::uint16_t kKindOk      = 2;
constexpr std::uint16_t kKindError   = 3;
constexpr std::uint16_t kKindClose   = 9;

constexpr std::size_t kHeaderLen = 16;

// Same guard rails the HTTP /decrypt frame parser uses (server.cpp), so both
// entry points agree on what is acceptable. adam_len/uri_len are u16 on the
// wire and therefore always below kMaxFieldLen; the check is kept so the two
// parsers stay comparable.
constexpr std::uint32_t kMaxSamples  = 100000;
constexpr std::uint32_t kMaxFieldLen = 1024 * 1024;
constexpr std::uint64_t kMaxBodyLen  = 256ull * 1024ull * 1024ull;

// Matches upstream wrapper-v2's decrypt socket timeouts.
constexpr int kSocketTimeoutSeconds = 60;

std::uint16_t read_u16_be(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8)
                                      | static_cast<std::uint16_t>(p[1]));
}

std::uint32_t read_u32_be(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24)
         | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) << 8)
         |  static_cast<std::uint32_t>(p[3]);
}

void append_u32_be(std::string* out, std::uint32_t v) {
    out->push_back(static_cast<char>((v >> 24) & 0xff));
    out->push_back(static_cast<char>((v >> 16) & 0xff));
    out->push_back(static_cast<char>((v >> 8) & 0xff));
    out->push_back(static_cast<char>(v & 0xff));
}

// Reads exactly `len` bytes. Returns false on EOF or error; `*clean_eof` is
// true when the peer closed before any byte of this read arrived, which is the
// normal way a session ends.
bool read_exact(int fd, void* buf, std::size_t len, bool* clean_eof) {
    auto* p = static_cast<std::uint8_t*>(buf);
    std::size_t done = 0;
    while (done < len) {
        const ssize_t n = ::read(fd, p + done, len - done);
        if (n == 0) {
            if (clean_eof != nullptr) *clean_eof = (done == 0);
            return false;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

bool write_all(int fd, const void* buf, std::size_t len) {
    const auto* p = static_cast<const std::uint8_t*>(buf);
    std::size_t done = 0;
    while (done < len) {
        const ssize_t n = ::write(fd, p + done, len - done);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

bool write_frame(int fd,
                 std::uint16_t kind,
                 std::uint32_t request_id,
                 const std::string& payload) {
    if (payload.size() > 0xffffffffull) return false;

    std::string header;
    header.reserve(kHeaderLen);
    append_u32_be(&header, kMagic);
    header.push_back(static_cast<char>((kVersion >> 8) & 0xff));
    header.push_back(static_cast<char>(kVersion & 0xff));
    header.push_back(static_cast<char>((kind >> 8) & 0xff));
    header.push_back(static_cast<char>(kind & 0xff));
    append_u32_be(&header, request_id);
    append_u32_be(&header, static_cast<std::uint32_t>(payload.size()));

    if (!write_all(fd, header.data(), header.size())) return false;
    if (payload.empty()) return true;
    return write_all(fd, payload.data(), payload.size());
}

bool write_error_frame(int fd, std::uint32_t request_id, const std::string& message) {
    return write_frame(fd, kKindError, request_id, message);
}

void set_socket_options(int fd) {
    int one = 1;
    (void)::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct timeval tv{};
    tv.tv_sec = kSocketTimeoutSeconds;
    tv.tv_usec = 0;
    (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

std::string peer_label(int fd) {
    struct sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (::getpeername(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) {
        return "unknown";
    }
    char host[INET_ADDRSTRLEN] = {0};
    if (::inet_ntop(AF_INET, &addr.sin_addr, host, sizeof(host)) == nullptr) {
        return "unknown";
    }
    return std::string(host) + ":" + std::to_string(ntohs(addr.sin_port));
}

// Serves one connection until CLOSE, EOF, or a protocol error.
void serve_connection(int fd, const DecryptBackend& backend) {
    set_socket_options(fd);
    const std::string peer = peer_label(fd);
    bool logged_session = false;

    for (;;) {
        std::uint8_t header[kHeaderLen];
        bool clean_eof = false;
        if (!read_exact(fd, header, sizeof(header), &clean_eof)) {
            if (!clean_eof) {
                std::fprintf(stderr,
                             "wrapper-v2: decrypt client %s dropped mid-header\n",
                             peer.c_str());
            }
            return;
        }

        const std::uint32_t magic      = read_u32_be(header);
        const std::uint16_t version    = read_u16_be(header + 4);
        const std::uint16_t kind       = read_u16_be(header + 6);
        const std::uint32_t request_id = read_u32_be(header + 8);
        const std::uint32_t payload_len = read_u32_be(header + 12);

        if (magic != kMagic) {
            std::fprintf(stderr, "wrapper-v2: decrypt client %s bad magic\n", peer.c_str());
            (void)write_error_frame(fd, request_id, "bad decrypt frame magic");
            return;
        }
        if (version != kVersion) {
            std::fprintf(stderr,
                         "wrapper-v2: decrypt client %s unsupported version %u\n",
                         peer.c_str(), static_cast<unsigned>(version));
            (void)write_error_frame(fd, request_id, "unsupported decrypt frame version");
            return;
        }
        // Checked before allocating so a bogus length cannot exhaust memory.
        if (static_cast<std::uint64_t>(payload_len) > kMaxBodyLen) {
            (void)write_error_frame(fd, request_id, "decrypt frame payload too large");
            return;
        }

        std::string payload;
        if (payload_len > 0) {
            payload.resize(payload_len);
            if (!read_exact(fd, &payload[0], payload.size(), nullptr)) {
                std::fprintf(stderr,
                             "wrapper-v2: decrypt client %s dropped mid-payload\n",
                             peer.c_str());
                return;
            }
        }

        if (kind == kKindClose) {
            return;
        }
        if (kind != kKindBatch) {
            (void)write_error_frame(fd, request_id, "unsupported decrypt frame kind");
            return;
        }

        std::string http_body;
        std::string adam_id;
        std::string uri;
        std::string error;
        if (!transcode_wv2d_batch(payload, &http_body, &adam_id, &uri, &error)) {
            std::fprintf(stderr,
                         "wrapper-v2: decrypt client %s invalid batch: %s\n",
                         peer.c_str(), error.c_str());
            (void)write_error_frame(fd, request_id, error);
            return;
        }

        if (!logged_session) {
            std::fprintf(stderr,
                         "wrapper-v2: decrypt client %s adam=%s fps_key_uri=%s\n",
                         peer.c_str(), adam_id.c_str(), uri.c_str());
            logged_session = true;
        }

        DecryptOutcome outcome = backend
                                     ? backend(http_body)
                                     : DecryptOutcome{false, {}, "decrypt backend unavailable"};

        if (!outcome.ok) {
            const std::string message =
                outcome.error.empty() ? std::string("decrypt failed") : outcome.error;
            (void)write_error_frame(fd, request_id, message);
            return;
        }
        if (!write_frame(fd, kKindOk, request_id, outcome.payload)) {
            std::fprintf(stderr,
                         "wrapper-v2: decrypt client %s write failed\n", peer.c_str());
            return;
        }
        // Loop: the client may send another batch on this connection.
    }
}

}  // namespace

bool transcode_wv2d_batch(const std::string& batch_payload,
                          std::string* http_body,
                          std::string* adam_id,
                          std::string* uri,
                          std::string* error) {
    const auto* data = reinterpret_cast<const std::uint8_t*>(batch_payload.data());
    const std::size_t size = batch_payload.size();

    if (size > kMaxBodyLen) {
        *error = "decrypt batch too large";
        return false;
    }
    if (size < 8) {
        *error = "decrypt batch too short";
        return false;
    }

    const std::uint32_t adam_len     = read_u16_be(data);
    const std::uint32_t uri_len      = read_u16_be(data + 2);
    const std::uint32_t sample_count = read_u32_be(data + 4);

    if (adam_len == 0 || uri_len == 0) {
        *error = "adam_id and uri must be non-empty";
        return false;
    }
    if (adam_len > kMaxFieldLen || uri_len > kMaxFieldLen) {
        *error = "adam_id or uri is too large";
        return false;
    }
    if (sample_count == 0 || sample_count > kMaxSamples) {
        *error = "sample_count must be between 1 and 100000";
        return false;
    }

    const std::uint64_t table_bytes = static_cast<std::uint64_t>(sample_count) * 4ull;
    const std::uint64_t fixed = 8ull + table_bytes + adam_len + uri_len;
    if (fixed > size) {
        *error = "decrypt batch length table exceeds payload";
        return false;
    }

    std::vector<std::uint32_t> lengths;
    lengths.reserve(sample_count);
    std::uint64_t sample_bytes = 0;
    const std::uint8_t* lenp = data + 8;
    for (std::uint32_t i = 0; i < sample_count; ++i) {
        const std::uint32_t n = read_u32_be(lenp + (static_cast<std::size_t>(i) * 4u));
        if (n == 0) {
            *error = "sample length must be non-zero";
            return false;
        }
        sample_bytes += n;
        if (sample_bytes > kMaxBodyLen) {
            *error = "decrypt sample payload too large";
            return false;
        }
        lengths.push_back(n);
    }
    if (fixed + sample_bytes != size) {
        *error = "decrypt batch size does not match declared lengths";
        return false;
    }

    // Same field order, wider header: rewrite the three counts as u32 and copy
    // the length table plus the body through unchanged.
    http_body->clear();
    http_body->reserve(static_cast<std::size_t>(size + 4));
    append_u32_be(http_body, adam_len);
    append_u32_be(http_body, uri_len);
    append_u32_be(http_body, sample_count);
    http_body->append(batch_payload, 8, batch_payload.size() - 8);

    if (adam_id != nullptr) {
        adam_id->assign(batch_payload,
                        static_cast<std::size_t>(8 + table_bytes),
                        adam_len);
    }
    if (uri != nullptr) {
        uri->assign(batch_payload,
                    static_cast<std::size_t>(8 + table_bytes + adam_len),
                    uri_len);
    }
    return true;
}

bool run_decrypt_tcp(const DecryptTcpOptions& options, DecryptBackend backend) {
    const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::fprintf(stderr, "wrapper-v2: decrypt socket() failed: %s\n",
                     std::strerror(errno));
        return false;
    }

    int one = 1;
    (void)::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(options.port));
    if (options.host.empty() || options.host == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, options.host.c_str(), &addr.sin_addr) != 1) {
        std::fprintf(stderr,
                     "wrapper-v2: decrypt listener bad host '%s'\n",
                     options.host.c_str());
        ::close(listen_fd);
        return false;
    }

    if (::bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::fprintf(stderr, "wrapper-v2: decrypt bind failed on %s:%d: %s\n",
                     options.host.c_str(), options.port, std::strerror(errno));
        ::close(listen_fd);
        return false;
    }
    if (::listen(listen_fd, 16) != 0) {
        std::fprintf(stderr, "wrapper-v2: decrypt listen failed on %s:%d: %s\n",
                     options.host.c_str(), options.port, std::strerror(errno));
        ::close(listen_fd);
        return false;
    }

    std::fprintf(stderr, "wrapper-v2: TCP decrypt listening on %s:%d\n",
                 options.host.c_str(), options.port);

    for (;;) {
        const int fd = ::accept(listen_fd, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "wrapper-v2: decrypt accept failed: %s\n",
                         std::strerror(errno));
            if (errno == EMFILE || errno == ENFILE || errno == ENOBUFS
                || errno == ENOMEM || errno == ECONNABORTED) {
                // Transient resource pressure: keep the listener alive.
                continue;
            }
            break;
        }
        std::thread([fd, backend]() {
            serve_connection(fd, backend);
            ::close(fd);
        }).detach();
    }

    ::close(listen_fd);
    return false;
}

}  // namespace wrapper
