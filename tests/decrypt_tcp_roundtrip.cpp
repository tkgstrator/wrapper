// Host-native round-trip check for the WV2D TCP decrypt listener.
//
// The daemon itself is cross-compiled with the Android NDK and links against
// Apple's libc++_shared.so, so it cannot be built on a plain host. This check
// covers the part that needs no Apple code at all: the WV2D framing, the BATCH
// parser, and the accept/session loop in decrypt_tcp.cpp. The decrypt backend
// is stubbed, so nothing here touches FairPlay.
//
// It drives the real listener over a real socket and encodes requests exactly
// the way gamdl's Rust client does (gamdl/downloader/ammuxer/src/media.rs
// write_frame + decrypt_batch, cross-checked against decrypt.rs
// build_decrypt_batch_payload).
//
// Build and run from the repository root:
//
//   g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread
//       -I src/daemon -o /tmp/decrypt_tcp_roundtrip
//       tests/decrypt_tcp_roundtrip.cpp src/daemon/decrypt_tcp.cpp
//
//   (one line; split here only for readability)
//   /tmp/decrypt_tcp_roundtrip

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "decrypt_tcp.hpp"

namespace {

constexpr std::uint32_t kMagic     = 0x57563244u;
constexpr std::uint16_t kVersion   = 1;
constexpr std::uint16_t kKindBatch = 1;
constexpr std::uint16_t kKindOk    = 2;
constexpr std::uint16_t kKindError = 3;
constexpr std::uint16_t kKindClose = 9;

constexpr int kTestPort = 11020;

int g_failures = 0;

void check(bool cond, const std::string& what) {
    std::printf("%s  %s\n", cond ? "  ok  " : "  FAIL", what.c_str());
    if (!cond) ++g_failures;
}

void put_u16(std::string* out, std::uint16_t v) {
    out->push_back(static_cast<char>((v >> 8) & 0xff));
    out->push_back(static_cast<char>(v & 0xff));
}

void put_u32(std::string* out, std::uint32_t v) {
    out->push_back(static_cast<char>((v >> 24) & 0xff));
    out->push_back(static_cast<char>((v >> 16) & 0xff));
    out->push_back(static_cast<char>((v >> 8) & 0xff));
    out->push_back(static_cast<char>(v & 0xff));
}

std::uint16_t get_u16(const std::string& s, std::size_t off) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint8_t>(s[off]) << 8) | static_cast<std::uint8_t>(s[off + 1]));
}

std::uint32_t get_u32(const std::string& s, std::size_t off) {
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[off])) << 24)
         | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[off + 1])) << 16)
         | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[off + 2])) << 8)
         |  static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[off + 3]));
}

// Exactly gamdl's BATCH payload encoder.
std::string encode_batch(const std::string& adam_id,
                         const std::string& uri,
                         const std::vector<std::string>& samples) {
    std::string p;
    put_u16(&p, static_cast<std::uint16_t>(adam_id.size()));
    put_u16(&p, static_cast<std::uint16_t>(uri.size()));
    put_u32(&p, static_cast<std::uint32_t>(samples.size()));
    for (const auto& s : samples) put_u32(&p, static_cast<std::uint32_t>(s.size()));
    p += adam_id;
    p += uri;
    for (const auto& s : samples) p += s;
    return p;
}

// Exactly gamdl's OK payload decoder, including its no-trailing-bytes rule.
bool decode_ok_payload(const std::string& payload, std::vector<std::string>* out) {
    if (payload.size() < 4) return false;
    const std::uint32_t count = get_u32(payload, 0);
    const std::size_t table_end = 4 + static_cast<std::size_t>(count) * 4;
    if (payload.size() < table_end) return false;
    std::size_t offset = table_end;
    out->clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t len = get_u32(payload, 4 + static_cast<std::size_t>(i) * 4);
        if (offset + len > payload.size()) return false;
        out->push_back(payload.substr(offset, len));
        offset += len;
    }
    return offset == payload.size();
}

class Client {
public:
    bool connect() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kTestPort);
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        return ::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0;
    }

    ~Client() {
        if (fd_ >= 0) ::close(fd_);
    }

    bool write_frame(std::uint16_t kind, std::uint32_t request_id, const std::string& payload) {
        std::string f;
        put_u32(&f, kMagic);
        put_u16(&f, kVersion);
        put_u16(&f, kind);
        put_u32(&f, request_id);
        put_u32(&f, static_cast<std::uint32_t>(payload.size()));
        f += payload;
        std::size_t done = 0;
        while (done < f.size()) {
            const ssize_t n = ::write(fd_, f.data() + done, f.size() - done);
            if (n <= 0) return false;
            done += static_cast<std::size_t>(n);
        }
        return true;
    }

    // Returns false on EOF (which is how a clean close looks to the client).
    bool read_frame(std::uint16_t* kind, std::uint32_t* request_id, std::string* payload) {
        std::string header;
        if (!read_exact(16, &header)) return false;
        if (get_u32(header, 0) != kMagic) return false;
        if (get_u16(header, 4) != kVersion) return false;
        *kind = get_u16(header, 6);
        *request_id = get_u32(header, 8);
        const std::uint32_t len = get_u32(header, 12);
        payload->clear();
        return len == 0 || read_exact(len, payload);
    }

    // Writes a frame with a caller-chosen magic, for protocol-error cases.
    bool write_raw_frame(std::uint32_t magic,
                         std::uint16_t version,
                         std::uint16_t kind,
                         std::uint32_t request_id,
                         const std::string& payload) {
        std::string f;
        put_u32(&f, magic);
        put_u16(&f, version);
        put_u16(&f, kind);
        put_u32(&f, request_id);
        put_u32(&f, static_cast<std::uint32_t>(payload.size()));
        f += payload;
        std::size_t done = 0;
        while (done < f.size()) {
            const ssize_t n = ::write(fd_, f.data() + done, f.size() - done);
            if (n <= 0) return false;
            done += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool at_eof() {
        char c = 0;
        return ::read(fd_, &c, 1) == 0;
    }

private:
    bool read_exact(std::size_t len, std::string* out) {
        out->resize(len);
        std::size_t done = 0;
        while (done < len) {
            const ssize_t n = ::read(fd_, &(*out)[done], len - done);
            if (n <= 0) return false;
            done += static_cast<std::size_t>(n);
        }
        return true;
    }

    int fd_ = -1;
};

}  // namespace

int main() {
    const std::string adam = "1440857781";
    const std::string uri  = "skd://itunes.apple.com/P000000000/s1/e1";
    const std::vector<std::string> samples = {
        std::string("\x01\x02\x03\x04", 4),
        std::string("\xaa\xbb", 2),
        std::string(64, '\x7f'),
    };

    // What the backend is handed, captured for inspection.
    std::string seen_body;
    int calls = 0;

    wrapper::DecryptBackend backend = [&](const std::string& http_body) {
        ++calls;
        seen_body = http_body;
        // Echo each ciphertext back with its bytes inverted, so the response is
        // clearly derived from the request and length-sensitive.
        std::vector<std::string> plain;
        // Re-read the request the way server.cpp's parser does (u32 header).
        const std::uint32_t adam_len = get_u32(http_body, 0);
        const std::uint32_t uri_len  = get_u32(http_body, 4);
        const std::uint32_t count    = get_u32(http_body, 8);
        std::size_t off = 12 + static_cast<std::size_t>(count) * 4 + adam_len + uri_len;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t len = get_u32(http_body, 12 + static_cast<std::size_t>(i) * 4);
            std::string s = http_body.substr(off, len);
            for (char& c : s) c = static_cast<char>(~static_cast<std::uint8_t>(c));
            plain.push_back(s);
            off += len;
        }
        std::string payload;
        put_u32(&payload, static_cast<std::uint32_t>(plain.size()));
        for (const auto& s : plain) put_u32(&payload, static_cast<std::uint32_t>(s.size()));
        for (const auto& s : plain) payload += s;

        wrapper::DecryptOutcome out;
        out.ok = true;
        out.payload = payload;
        return out;
    };

    wrapper::DecryptTcpOptions opts;
    opts.host = "127.0.0.1";
    opts.port = kTestPort;
    std::thread listener([&]() { wrapper::run_decrypt_tcp(opts, backend); });
    listener.detach();

    // Give the listener a moment to bind.
    for (int i = 0; i < 200; ++i) {
        Client probe;
        if (probe.connect()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::printf("transcode_wv2d_batch\n");
    {
        const std::string batch = encode_batch(adam, uri, samples);
        std::string body, got_adam, got_uri, err;
        const bool ok = wrapper::transcode_wv2d_batch(batch, &body, &got_adam, &got_uri, &err);
        check(ok, "accepts a well-formed batch");
        check(got_adam == adam, "extracts adam_id");
        check(got_uri == uri, "extracts skd uri");

        // Independently computed expectation: u32 header, then the batch tail.
        std::string want;
        put_u32(&want, static_cast<std::uint32_t>(adam.size()));
        put_u32(&want, static_cast<std::uint32_t>(uri.size()));
        put_u32(&want, static_cast<std::uint32_t>(samples.size()));
        for (const auto& s : samples) put_u32(&want, static_cast<std::uint32_t>(s.size()));
        want += adam;
        want += uri;
        for (const auto& s : samples) want += s;
        check(body == want, "produces the HTTP /decrypt body byte-for-byte");
        check(body.size() == batch.size() + 4, "body is exactly 4 bytes wider than the batch");
    }

    std::printf("rejects malformed batches\n");
    {
        std::string body, err;
        check(!wrapper::transcode_wv2d_batch("", &body, nullptr, nullptr, &err),
              "empty payload");
        check(!wrapper::transcode_wv2d_batch(std::string(8, '\0'), &body, nullptr, nullptr, &err),
              "zero lengths");

        std::string truncated = encode_batch(adam, uri, samples);
        truncated.resize(truncated.size() - 1);
        check(!wrapper::transcode_wv2d_batch(truncated, &body, nullptr, nullptr, &err),
              "truncated sample bytes");

        std::string trailing = encode_batch(adam, uri, samples);
        trailing.push_back('\x00');
        check(!wrapper::transcode_wv2d_batch(trailing, &body, nullptr, nullptr, &err),
              "trailing bytes");
    }

    std::printf("socket session\n");
    {
        Client c;
        check(c.connect(), "connects");

        // First batch, non-1 request id to prove it is echoed and not assumed.
        check(c.write_frame(kKindBatch, 7, encode_batch(adam, uri, samples)), "sends batch #1");
        std::uint16_t kind = 0;
        std::uint32_t rid = 0;
        std::string payload;
        check(c.read_frame(&kind, &rid, &payload), "reads a response");
        check(kind == kKindOk, "response kind is OK");
        check(rid == 7, "request_id is echoed back");

        std::vector<std::string> plain;
        check(decode_ok_payload(payload, &plain), "OK payload decodes with no trailing bytes");
        check(plain.size() == samples.size(), "one plaintext per ciphertext");
        bool lengths_match = plain.size() == samples.size();
        for (std::size_t i = 0; lengths_match && i < plain.size(); ++i) {
            if (plain[i].size() != samples[i].size()) lengths_match = false;
        }
        check(lengths_match, "plaintext lengths match the ciphertext lengths");

        // Second batch on the same connection: the session must persist.
        check(c.write_frame(kKindBatch, 8, encode_batch(adam, uri, {samples[1]})),
              "sends batch #2 on the same connection");
        check(c.read_frame(&kind, &rid, &payload), "reads the second response");
        check(kind == kKindOk && rid == 8, "second response is OK with its own id");
        check(calls == 2, "backend was invoked once per batch");

        // CLOSE ends the session cleanly.
        check(c.write_frame(kKindClose, 0, ""), "sends CLOSE");
        check(c.at_eof(), "server closes the connection after CLOSE");
    }

    std::printf("error paths\n");
    {
        Client c;
        check(c.connect(), "connects");
        check(c.write_frame(kKindBatch, 42, std::string(4, '\x00')), "sends a malformed batch");
        std::uint16_t kind = 0;
        std::uint32_t rid = 0;
        std::string payload;
        check(c.read_frame(&kind, &rid, &payload), "reads a response");
        check(kind == kKindError, "response kind is ERROR");
        check(rid == 42, "ERROR echoes the request_id");
        check(!payload.empty(), "ERROR carries a message");
        std::printf("        message: %s\n", payload.c_str());
    }
    {
        Client c;
        check(c.connect(), "connects");
        check(c.write_frame(4, 99, ""), "sends an unsupported frame kind");
        std::uint16_t kind = 0;
        std::uint32_t rid = 0;
        std::string payload;
        check(c.read_frame(&kind, &rid, &payload), "reads a response");
        check(kind == kKindError && rid == 99, "ERROR for unsupported kind");
    }
    {
        Client c;
        check(c.connect(), "connects");
        check(c.write_raw_frame(0x58585858u, kVersion, kKindBatch, 5, ""),
              "sends a frame with bad magic");
        std::uint16_t kind = 0;
        std::uint32_t rid = 0;
        std::string payload;
        check(c.read_frame(&kind, &rid, &payload), "server answers rather than hanging");
        check(kind == kKindError && rid == 5, "bad magic yields an ERROR frame");
    }
    {
        Client c;
        check(c.connect(), "connects");
        check(c.write_raw_frame(kMagic, 2, kKindBatch, 6, ""),
              "sends an unsupported protocol version");
        std::uint16_t kind = 0;
        std::uint32_t rid = 0;
        std::string payload;
        check(c.read_frame(&kind, &rid, &payload), "server answers");
        check(kind == kKindError && rid == 6, "bad version yields an ERROR frame");
    }

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
