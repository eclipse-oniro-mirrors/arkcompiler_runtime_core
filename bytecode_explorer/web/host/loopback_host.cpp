/**
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "web/host/loopback_host.h"

#include "web/host/http_request.h"
#include "web/host/pick_controller.h"
#include "web/host/request_router.h"

#include <uv.h>

#include <algorithm>
#include <array>
#include <csignal>
#include <exception>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer {
namespace {

constexpr size_t TOKEN_BYTE_COUNT = 32U;
constexpr size_t MAX_CONNECTION_COUNT = 64U;
constexpr unsigned int LISTEN_BACKLOG = 32U;
constexpr int HEX_DIGIT_WIDTH = 2;
// Transport-level HTTP status codes, mirroring the router-level names in request_router.cpp.
constexpr int HTTP_STATUS_BAD_REQUEST = 400;
constexpr int HTTP_STATUS_CONTENT_TOO_LARGE = 413;
constexpr int HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE = 431;
constexpr int HTTP_STATUS_INTERNAL_SERVER_ERROR = 500;
constexpr int HTTP_STATUS_SERVICE_UNAVAILABLE = 503;

Status UvError(std::string_view operation, int error)
{
    return Status(ErrorCode::PLATFORM_ERROR, std::string(operation) + ": " + uv_strerror(error));
}

std::string HexToken(const std::array<uint8_t, TOKEN_BYTE_COUNT> &bytes)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        output << std::setw(HEX_DIGIT_WIDTH) << static_cast<uint32_t>(byte);
    }
    return output.str();
}

HttpResponse TransportError(int status, std::string reason, std::string message)
{
    HttpResponse response;
    response.status = status;
    response.reason = std::move(reason);
    response.contentType = "text/plain; charset=utf-8";
    response.body = std::move(message);
    response.headers = {{"Cache-Control", "no-store"},
                        {"Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'"},
                        {"X-Content-Type-Options", "nosniff"}};
    return response;
}

}  // namespace

class LoopbackHost::Impl final {
public:
    explicit Impl(WebSession &session, RecentFiles &recents, Preferences &preferences)
        : session_(session), recents_(recents), preferences_(preferences), pick_(session) {}

    ~Impl()
    {
        Stop();
    }

    Result<LoopbackEndpoint> Start()
    {
        if (started_) {
            return Result<LoopbackEndpoint>::Failure(
                Status(ErrorCode::INVALID_ARGUMENT, "Loopback host has already been started"));
        }
        auto token = CreateSessionToken();
        if (!token.HasValue()) {
            return Result<LoopbackEndpoint>::Failure(token.Error());
        }
        endpoint_.token = token.Value();
        auto port = BindLoopbackListener();
        if (!port.HasValue()) {
            return Result<LoopbackEndpoint>::Failure(port.Error());
        }
        endpoint_.port = port.Value();
        router_ =
            std::make_unique<RequestRouter>(session_, endpoint_.token, pick_, recents_, preferences_, endpoint_.port);

        int status = InitializeSignal(&interruptSignal_, SIGINT, &interruptSignalInitialized_);
#if defined(SIGTERM)
        if (status == 0) {
            status = InitializeSignal(&terminateSignal_, SIGTERM, &terminateSignalInitialized_);
        }
#endif
        if (status != 0) {
            auto error = UvError("Cannot initialize local shutdown signal", status);
            Stop();
            return Result<LoopbackEndpoint>::Failure(std::move(error));
        }

        started_ = true;
        stopping_ = false;
        return Result<LoopbackEndpoint>::Success(endpoint_);
    }

    // Initializes the event loop and creates the local session token.
    Result<std::string> CreateSessionToken()
    {
        int status = uv_loop_init(&loop_);
        if (status != 0) {
            return Result<std::string>::Failure(UvError("Cannot initialize local event loop", status));
        }
        loopInitialized_ = true;

        std::array<uint8_t, TOKEN_BYTE_COUNT> tokenBytes {};
        status = uv_random(&loop_, nullptr, tokenBytes.data(), tokenBytes.size(), 0U, nullptr);
        if (status != 0) {
            auto error = UvError("Cannot create local session token", status);
            Stop();
            return Result<std::string>::Failure(std::move(error));
        }
        return Result<std::string>::Success(HexToken(tokenBytes));
    }

    // Binds the TCP listener to an ephemeral loopback port; returns the bound port.
    Result<uint16_t> BindLoopbackListener()
    {
        int status = uv_tcp_init(&loop_, &server_);
        if (status != 0) {
            auto error = UvError("Cannot initialize local TCP listener", status);
            Stop();
            return Result<uint16_t>::Failure(std::move(error));
        }
        serverInitialized_ = true;
        server_.data = this;

        sockaddr_in address {};
        status = uv_ip4_addr("127.0.0.1", 0, &address);
        if (status == 0) {
            status = uv_tcp_bind(&server_, reinterpret_cast<const sockaddr *>(&address), 0U);
        }
        if (status == 0) {
            status = uv_listen(reinterpret_cast<uv_stream_t *>(&server_), LISTEN_BACKLOG, OnConnection);
        }
        if (status != 0) {
            auto error = UvError("Cannot bind local bytecode explorer to 127.0.0.1", status);
            Stop();
            return Result<uint16_t>::Failure(std::move(error));
        }

        sockaddr_storage boundAddress {};
        int addressLength = sizeof(boundAddress);
        status = uv_tcp_getsockname(&server_, reinterpret_cast<sockaddr *>(&boundAddress), &addressLength);
        if (status != 0 || boundAddress.ss_family != AF_INET) {
            auto error = status == 0 ? Status(ErrorCode::PLATFORM_ERROR, "Local listener did not bind an IPv4 address")
                                     : UvError("Cannot inspect local listener address", status);
            Stop();
            return Result<uint16_t>::Failure(std::move(error));
        }
        return Result<uint16_t>::Success(ntohs(reinterpret_cast<const sockaddr_in *>(&boundAddress)->sin_port));
    }

    Status Run()
    {
        if (!started_) {
            return Status(ErrorCode::INVALID_ARGUMENT, "Loopback host must be started before it can run");
        }
        running_ = true;
        const int status = uv_run(&loop_, UV_RUN_DEFAULT);
        running_ = false;
        FinishStop();
        return status < 0 ? UvError("Local event loop failed", status) : Status {};
    }

    void Stop()
    {
        if (!loopInitialized_) {
            return;
        }
        BeginStop();
        if (!running_) {
            FinishStop();
        }
    }

private:
    struct ClientConnection {
        uv_tcp_t stream {};
        Impl *host {nullptr};
        std::string requestBytes;
    };

    struct WriteRequest {
        uv_write_t request {};
        ClientConnection *client {nullptr};
        std::string bytes;
    };

    int InitializeSignal(uv_signal_t *signal, int number, bool *initialized)
    {
        int status = uv_signal_init(&loop_, signal);
        if (status != 0) {
            return status;
        }
        *initialized = true;
        signal->data = this;
        return uv_signal_start(signal, OnSignal, number);
    }

    void BeginStop()
    {
        if (stopping_) {
            return;
        }
        stopping_ = true;
        CloseHandle(reinterpret_cast<uv_handle_t *>(&server_), serverInitialized_);
        CloseHandle(reinterpret_cast<uv_handle_t *>(&interruptSignal_), interruptSignalInitialized_);
        CloseHandle(reinterpret_cast<uv_handle_t *>(&terminateSignal_), terminateSignalInitialized_);
        const std::vector<ClientConnection *> clients(clients_.begin(), clients_.end());
        for (auto *client : clients) {
            CloseClient(client);
        }
    }

    void FinishStop()
    {
        if (!loopInitialized_) {
            return;
        }
        uv_run(&loop_, UV_RUN_DEFAULT);
        const int status = uv_loop_close(&loop_);
        if (status == 0) {
            loopInitialized_ = false;
            serverInitialized_ = false;
            interruptSignalInitialized_ = false;
            terminateSignalInitialized_ = false;
            started_ = false;
            router_.reset();
        }
    }

    static void CloseHandle(uv_handle_t *handle, bool initialized)
    {
        if (initialized && uv_is_closing(handle) == 0) {
            uv_close(handle, nullptr);
        }
    }

    static void OnSignal(uv_signal_t *signal, int)
    {
        static_cast<Impl *>(signal->data)->BeginStop();
    }

    static void OnConnection(uv_stream_t *server, int status)
    {
        auto *host = static_cast<Impl *>(server->data);
        if (status < 0 || host->stopping_) {
            return;
        }
        auto *client = new ClientConnection;
        client->host = host;
        status = uv_tcp_init(&host->loop_, &client->stream);
        if (status != 0) {
            delete client;
            return;
        }
        client->stream.data = client;
        if (uv_accept(server, reinterpret_cast<uv_stream_t *>(&client->stream)) != 0) {
            uv_close(reinterpret_cast<uv_handle_t *>(&client->stream),
                     [](uv_handle_t *handle) { delete static_cast<ClientConnection *>(handle->data); });
            return;
        }
        host->clients_.insert(client);
        if (host->clients_.size() > MAX_CONNECTION_COUNT) {
            auto response =
                TransportError(HTTP_STATUS_SERVICE_UNAVAILABLE, "Service Unavailable", "Too many local connections");
            host->Send(client, response);
            return;
        }
        if (uv_read_start(reinterpret_cast<uv_stream_t *>(&client->stream), AllocateReadBuffer, OnRead) != 0) {
            host->CloseClient(client);
        }
    }

    static void AllocateReadBuffer(uv_handle_t *, size_t suggestedSize, uv_buf_t *buffer)
    {
        constexpr size_t MAX_READ_SIZE = 8192U;
        const auto size = std::max<size_t>(1U, std::min(suggestedSize, MAX_READ_SIZE));
        buffer->base = new char[size];
        buffer->len = size;
    }

    static void OnRead(uv_stream_t *stream, ssize_t count, const uv_buf_t *buffer)
    {
        std::unique_ptr<char[]> storage(buffer->base);
        auto *client = static_cast<ClientConnection *>(stream->data);
        auto *host = client->host;
        if (count < 0) {
            host->CloseClient(client);
            return;
        }
        if (count == 0) {
            return;
        }
        const auto byteCount = static_cast<size_t>(count);
        if (byteCount > HttpRequestLimits::MAX_REQUEST_BYTES - client->requestBytes.size()) {
            auto response =
                TransportError(HTTP_STATUS_CONTENT_TOO_LARGE, "Content Too Large", "HTTP request is too large");
            host->Send(client, response);
            return;
        }
        client->requestBytes.append(buffer->base, byteCount);
        auto parsed = ParseHttpRequest(client->requestBytes);
        if (parsed.status == HttpParseStatus::INCOMPLETE) {
            return;
        }
        if (parsed.status == HttpParseStatus::TOO_LARGE) {
            auto response = TransportError(HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE,
                                           "Request Header Fields Too Large", parsed.diagnostic);
            host->Send(client, response);
            return;
        }
        if (parsed.status != HttpParseStatus::COMPLETE) {
            host->Send(client, TransportError(HTTP_STATUS_BAD_REQUEST, "Bad Request", parsed.diagnostic));
            return;
        }
        try {
            host->Send(client, host->router_->Route(parsed.request));
        } catch (const std::exception &error) {
            host->Send(client,
                       TransportError(HTTP_STATUS_INTERNAL_SERVER_ERROR, "Internal Server Error", error.what()));
        }
    }

    void Send(ClientConnection *client, HttpResponse response)
    {
        uv_read_stop(reinterpret_cast<uv_stream_t *>(&client->stream));
        auto *write = new WriteRequest;
        write->request.data = write;
        write->client = client;
        write->bytes = response.Serialize();
        auto buffer = uv_buf_init(write->bytes.data(), static_cast<unsigned int>(write->bytes.size()));
        const int status = uv_write(&write->request, reinterpret_cast<uv_stream_t *>(&client->stream), &buffer, 1U,
                                    [](uv_write_t *request, int) {
                                        auto *completed = static_cast<WriteRequest *>(request->data);
                                        auto *completedClient = completed->client;
                                        auto *host = completedClient->host;
                                        delete completed;
                                        host->CloseClient(completedClient);
                                    });
        if (status != 0) {
            delete write;
            CloseClient(client);
        }
    }

    void CloseClient(ClientConnection *client)
    {
        auto *handle = reinterpret_cast<uv_handle_t *>(&client->stream);
        if (uv_is_closing(handle) != 0) {
            return;
        }
        uv_close(handle, [](uv_handle_t *closed) {
            auto *client = static_cast<ClientConnection *>(closed->data);
            client->host->clients_.erase(client);
            delete client;
        });
    }

    WebSession &session_;
    RecentFiles &recents_;
    Preferences &preferences_;
    PickController pick_;
    uv_loop_t loop_ {};
    uv_tcp_t server_ {};
    uv_signal_t interruptSignal_ {};
    uv_signal_t terminateSignal_ {};
    bool loopInitialized_ {false};
    bool serverInitialized_ {false};
    bool interruptSignalInitialized_ {false};
    bool terminateSignalInitialized_ {false};
    bool started_ {false};
    bool stopping_ {false};
    bool running_ {false};
    LoopbackEndpoint endpoint_;
    std::unique_ptr<RequestRouter> router_;
    std::unordered_set<ClientConnection *> clients_;
};

std::string LoopbackEndpoint::Url() const
{
    return "http://127.0.0.1:" + std::to_string(port) + "/#token=" + token;
}

LoopbackHost::LoopbackHost(WebSession &session, RecentFiles &recents, Preferences &preferences)
    : impl_(std::make_unique<Impl>(session, recents, preferences))
{
}

LoopbackHost::~LoopbackHost() = default;

Result<LoopbackEndpoint> LoopbackHost::Start()
{
    return impl_->Start();
}

Status LoopbackHost::Run()
{
    return impl_->Run();
}

void LoopbackHost::Stop()
{
    impl_->Stop();
}

}  // namespace panda::bytecode_explorer
