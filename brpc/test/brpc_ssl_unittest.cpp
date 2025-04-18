// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// Baidu RPC - A framework to host and access services throughout Baidu.

// Date: Sun Jul 13 15:04:18 CST 2014

#include <fstream>
#include <gtest/gtest.h>
#include <google/protobuf/descriptor.h>
#include <butil/time.h>
#include <butil/macros.h>
#include <butil/fd_guard.h>
#include <butil/files/scoped_file.h>
#include <brpc/policy/baidu_rpc_meta.pb.h>
#include <brpc/policy/baidu_rpc_protocol.h>
#include <brpc/policy/most_common_message.h>
#include "brpc/global.h"
#include "brpc/socket.h"
#include "brpc/server.h"
#include "brpc/channel.h"
#include "brpc/socket_map.h"
#include "brpc/controller.h"
#include "echo.pb.h"

namespace brpc {

void ExtractHostnames(X509* x, std::vector<std::string>* hostnames);
} // namespace brpc


int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    brpc::GlobalInitializeOrDie();
    return RUN_ALL_TESTS();
}

bool g_delete = false;
const std::string EXP_REQUEST = "hello";
const std::string EXP_RESPONSE = "world";

class EchoServiceImpl : public test::EchoService {
public:
    EchoServiceImpl() : count(0) {}
    ~EchoServiceImpl() override { g_delete = true; }
    void Echo(google::protobuf::RpcController* cntl_base,
              const test::EchoRequest* request,
              test::EchoResponse* response,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = (brpc::Controller*)cntl_base;
        count.fetch_add(1, butil::memory_order_relaxed);
        EXPECT_EQ(EXP_REQUEST, request->message());
        EXPECT_TRUE(cntl->is_ssl());

        response->set_message(EXP_RESPONSE);
        if (request->sleep_us() > 0) {
            LOG(INFO) << "Sleep " << request->sleep_us() << " us, protocol="
                      << cntl->request_protocol();
            bthread_usleep(request->sleep_us());
        }
    }

    butil::atomic<int64_t> count;
};

class SSLTest : public ::testing::Test{
protected:
    SSLTest() {};
    virtual ~SSLTest(){};
    virtual void SetUp() {};
    virtual void TearDown() {};
};

void* RunClosure(void* arg) {
    google::protobuf::Closure* done = (google::protobuf::Closure*)arg;
    done->Run();
    return NULL;
}

void SendMultipleRPC(brpc::Channel* channel, int count) {
    for (int i = 0; i < count; ++i) {
        brpc::Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message(EXP_REQUEST);
        test::EchoService_Stub stub(channel);
        stub.Echo(&cntl, &req, &res, NULL);

        EXPECT_EQ(EXP_RESPONSE, res.message()) << cntl.ErrorText();
    }
}

TEST_F(SSLTest, sanity) {
    // Test RPC based on SSL + brpc protocol
    const int port = 8613;
    brpc::Server server;
    brpc::ServerOptions options;

    brpc::CertInfo cert;
    cert.certificate = "cert1.crt";
    cert.private_key = "cert1.key";
    options.mutable_ssl_options()->default_cert = cert;

    EchoServiceImpl echo_svc;
    ASSERT_EQ(0, server.AddService(
        &echo_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(port, &options));

    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(EXP_REQUEST);
    {
        brpc::Channel channel;
        brpc::ChannelOptions coptions;
        coptions.mutable_ssl_options();
        coptions.mutable_ssl_options()->sni_name = "localhost";
        ASSERT_EQ(0, channel.Init("localhost", port, &coptions));

        brpc::Controller cntl;
        test::EchoService_Stub stub(&channel);
        stub.Echo(&cntl, &req, &res, NULL);
        EXPECT_EQ(EXP_RESPONSE, res.message()) << cntl.ErrorText();
    }

    // stress test
    const int NUM = 5;
    const int COUNT = 3000;
    pthread_t tids[NUM];
    {
        brpc::Channel channel;
        brpc::ChannelOptions coptions;
        coptions.mutable_ssl_options();
        coptions.mutable_ssl_options()->sni_name = "localhost";
        ASSERT_EQ(0, channel.Init("127.0.0.1", port, &coptions));
        for (int i = 0; i < NUM; ++i) {
            google::protobuf::Closure* thrd_func =
                    brpc::NewCallback(SendMultipleRPC, &channel, COUNT);
            EXPECT_EQ(0, pthread_create(&tids[i], NULL, RunClosure, thrd_func));
        }
        for (int i = 0; i < NUM; ++i) {
            pthread_join(tids[i], NULL);
        }
    }
    {
        // Use HTTP
        brpc::Channel channel;
        brpc::ChannelOptions coptions;
        coptions.protocol = "http";
        coptions.mutable_ssl_options();
        coptions.mutable_ssl_options()->sni_name = "localhost";
        ASSERT_EQ(0, channel.Init("127.0.0.1", port, &coptions));
        for (int i = 0; i < NUM; ++i) {
            google::protobuf::Closure* thrd_func =
                    brpc::NewCallback(SendMultipleRPC, &channel, COUNT);
            EXPECT_EQ(0, pthread_create(&tids[i], NULL, RunClosure, thrd_func));
        }
        for (int i = 0; i < NUM; ++i) {
            pthread_join(tids[i], NULL);
        }
    }

    ASSERT_EQ(0, server.Stop(0));
    ASSERT_EQ(0, server.Join());
}

TEST_F(SSLTest, force_ssl) {
    const int port = 8613;
    brpc::Server server;
    brpc::ServerOptions options;
    EchoServiceImpl echo_svc;
    ASSERT_EQ(0, server.AddService(
        &echo_svc, brpc::SERVER_DOESNT_OWN_SERVICE));

    options.force_ssl = true;
    ASSERT_EQ(-1, server.Start(port, &options));

    brpc::CertInfo cert;
    cert.certificate = "cert1.crt";
    cert.private_key = "cert1.key";
    options.mutable_ssl_options()->default_cert = cert;

    ASSERT_EQ(0, server.Start(port, &options));

    test::EchoRequest req;
    req.set_message(EXP_REQUEST);
    {
        brpc::Channel channel;
        brpc::ChannelOptions coptions;
        coptions.mutable_ssl_options();
        coptions.mutable_ssl_options()->sni_name = "localhost";
        ASSERT_EQ(0, channel.Init("localhost", port, &coptions));

        brpc::Controller cntl;
        test::EchoService_Stub stub(&channel);
        test::EchoResponse res;
        stub.Echo(&cntl, &req, &res, NULL);
        ASSERT_EQ(EXP_RESPONSE, res.message()) << cntl.ErrorText();
    }

    {
        brpc::Channel channel;
        ASSERT_EQ(0, channel.Init("localhost", port, NULL));

        brpc::Controller cntl;
        test::EchoService_Stub stub(&channel);
        test::EchoResponse res;
        stub.Echo(&cntl, &req, &res, NULL);
        ASSERT_TRUE(cntl.Failed());
    }

    ASSERT_EQ(0, server.Stop(0));
    ASSERT_EQ(0, server.Join());
}

void ProcessResponse(brpc::InputMessageBase* msg_base) {
    brpc::DestroyingPtr<brpc::policy::MostCommonMessage> msg(
        static_cast<brpc::policy::MostCommonMessage*>(msg_base));
    brpc::policy::RpcMeta meta;
    ASSERT_TRUE(brpc::ParsePbFromIOBuf(&meta, msg->meta));
    const brpc::policy::RpcResponseMeta &response_meta = meta.response();
    ASSERT_EQ(0, response_meta.error_code()) << response_meta.error_text();

    const brpc::CallId cid = { static_cast<uint64_t>(meta.correlation_id()) };
    brpc::Controller* cntl = NULL;
    ASSERT_EQ(0, bthread_id_lock(cid, (void**)&cntl));
    ASSERT_NE(nullptr, cntl);
    ASSERT_TRUE(brpc::ParsePbFromIOBuf(cntl->response(), msg->payload));
    ASSERT_EQ(0, bthread_id_unlock_and_destroy(cid));
}

TEST_F(SSLTest, connect_on_create) {
    brpc::Protocol dummy_protocol = {
        brpc::policy::ParseRpcMessage, brpc::SerializeRequestDefault,
        brpc::policy::PackRpcRequest,NULL, ProcessResponse,
        NULL, NULL, NULL, brpc::CONNECTION_TYPE_ALL, "ssl_ut_baidu"
    };
    ASSERT_EQ(0, RegisterProtocol((brpc::ProtocolType)30, dummy_protocol));

    brpc::InputMessageHandler dummy_handler ={
        dummy_protocol.parse, dummy_protocol.process_response,
        NULL, NULL, dummy_protocol.name
    };
    brpc::InputMessenger messenger;
    ASSERT_EQ(0, messenger.AddHandler(dummy_handler));

    const int port = 8613;
    brpc::Server server;
    brpc::ServerOptions server_options;
    server_options.force_ssl = true;

    brpc::CertInfo cert;
    cert.certificate = "cert1.crt";
    cert.private_key = "cert1.key";
    server_options.mutable_ssl_options()->default_cert = cert;

    EchoServiceImpl echo_svc;
    ASSERT_EQ(0, server.AddService(
        &echo_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(port, &server_options));

    // Create client socket.
    brpc::SocketOptions socket_options;
    butil::EndPoint ep(butil::IP_ANY, port);
    socket_options.remote_side = ep;
    socket_options.connect_on_create = true;
    socket_options.on_edge_triggered_events = brpc::InputMessenger::OnNewMessages;
    socket_options.user = &messenger;
    brpc::ChannelSSLOptions ssl_options;
    SSL_CTX* raw_ctx = brpc::CreateClientSSLContext(ssl_options);
    ASSERT_NE(nullptr, raw_ctx);
    std::shared_ptr<brpc::SocketSSLContext> ssl_ctx
        = std::make_shared<brpc::SocketSSLContext>();
    ssl_ctx->raw_ctx = raw_ctx;
    socket_options.initial_ssl_ctx = ssl_ctx;

    brpc::SocketId socket_id;
    ASSERT_EQ(0, brpc::Socket::Create(socket_options, &socket_id));
    brpc::SocketUniquePtr ptr;
    ASSERT_EQ(0, brpc::Socket::Address(socket_id, &ptr));

    test::EchoRequest req;
    req.set_message(EXP_REQUEST);
    for (int i = 0; i < 100; ++i) {
        test::EchoResponse res;
        butil::IOBuf request_buf;
        butil::IOBuf request_body;
        brpc::Controller cntl;
        cntl._response = &res;
        const brpc::CallId correlation_id = cntl.call_id();
        brpc::SerializeRequestDefault(&request_body, &cntl, &req);
        brpc::policy::PackRpcRequest(&request_buf, NULL, correlation_id.value,
                                     test::EchoService_Stub::descriptor()->method(0),
                                     &cntl, request_body, NULL);
        ASSERT_EQ(0, ptr->Write(&request_buf));
        brpc::Join(correlation_id);
        ASSERT_EQ(EXP_RESPONSE, res.message());
    }
}

void CheckCert(const char* cname, const char* cert) {
    const int port = 8613;
    brpc::Channel channel;
    brpc::ChannelOptions coptions;
    coptions.mutable_ssl_options()->sni_name = cname;
    ASSERT_EQ(0, channel.Init("127.0.0.1", port, &coptions));

    SendMultipleRPC(&channel, 1);
    // client has no access to the sending socket
    std::vector<brpc::SocketId> ids;
    brpc::SocketMapList(&ids);
    ASSERT_EQ(1u, ids.size());
    brpc::SocketUniquePtr sock;
    ASSERT_EQ(0, brpc::Socket::Address(ids[0], &sock));

    X509* x509 = sock->GetPeerCertificate();
    ASSERT_TRUE(x509 != NULL);
    std::vector<std::string> cnames;
    brpc::ExtractHostnames(x509, &cnames);
    ASSERT_EQ(cert, cnames[0]) << x509;
}

std::string GetRawPemString(const char* fname) {
    butil::ScopedFILE fp(fname, "r");
    char buf[4096];
    int size = read(fileno(fp), buf, sizeof(buf));
    std::string raw;
    raw.append(buf, size);
    return raw;
}

#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME

TEST_F(SSLTest, ssl_sni) {
    const int port = 8613;
    brpc::Server server;
    brpc::ServerOptions options;
    {
        brpc::CertInfo cert;
        cert.certificate = "cert1.crt";
        cert.private_key = "cert1.key";
        cert.sni_filters.push_back("cert1.com");
        options.mutable_ssl_options()->default_cert = cert;
    }
    {
        brpc::CertInfo cert;
        cert.certificate = GetRawPemString("cert2.crt");
        cert.private_key = GetRawPemString("cert2.key");
        cert.sni_filters.push_back("*.cert2.com");
        options.mutable_ssl_options()->certs.push_back(cert);
    }
    EchoServiceImpl echo_svc;
    ASSERT_EQ(0, server.AddService(
        &echo_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(port, &options));

    CheckCert("cert1.com", "cert1");
    CheckCert("www.cert2.com", "cert2");
    CheckCert("noexist", "cert1");    // default cert

    server.Stop(0);
    server.Join();
}

TEST_F(SSLTest, ssl_reload) {
    const int port = 8613;
    brpc::Server server;
    brpc::ServerOptions options;
    {
        brpc::CertInfo cert;
        cert.certificate = "cert1.crt";
        cert.private_key = "cert1.key";
        cert.sni_filters.push_back("cert1.com");
        options.mutable_ssl_options()->default_cert = cert;
    }
    EchoServiceImpl echo_svc;
    ASSERT_EQ(0, server.AddService(
        &echo_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(port, &options));

    CheckCert("cert2.com", "cert1");    // default cert
    {
        brpc::CertInfo cert;
        cert.certificate = GetRawPemString("cert2.crt");
        cert.private_key = GetRawPemString("cert2.key");
        cert.sni_filters.push_back("cert2.com");
        ASSERT_EQ(0, server.AddCertificate(cert));
    }
    CheckCert("cert2.com", "cert2");

    {
        brpc::CertInfo cert;
        cert.certificate = GetRawPemString("cert2.crt");
        cert.private_key = GetRawPemString("cert2.key");
        ASSERT_EQ(0, server.RemoveCertificate(cert));
    }
    CheckCert("cert2.com", "cert1");    // default cert after remove cert2

    {
        brpc::CertInfo cert;
        cert.certificate = GetRawPemString("cert2.crt");
        cert.private_key = GetRawPemString("cert2.key");
        cert.sni_filters.push_back("cert2.com");
        std::vector<brpc::CertInfo> certs;
        certs.push_back(cert);
        ASSERT_EQ(0, server.ResetCertificates(certs));
    }
    CheckCert("cert2.com", "cert2");

    server.Stop(0);
    server.Join();
}

#endif  // SSL_CTRL_SET_TLSEXT_HOSTNAME

const int BUFSIZE[] = {64, 128, 256, 1024, 4096};
const int REP = 100000;

void* ssl_perf_client(void* arg) {
    SSL* ssl = (SSL*)arg;
    EXPECT_EQ(1, SSL_do_handshake(ssl));

    char buf[4096];
    butil::Timer tm;
    for (size_t i = 0; i < ARRAY_SIZE(BUFSIZE); ++i) {
        int size = BUFSIZE[i];
        tm.start();
        for (int j = 0; j < REP; ++j) {
            SSL_write(ssl, buf, size);
        }
        tm.stop();
        LOG(INFO) << "SSL_write(" << size << ") tp="
                  << size * REP / tm.u_elapsed() << "M/s"
                  << ", latency=" << tm.u_elapsed() / REP << "us";
    }
    return NULL;
}

void* ssl_perf_server(void* arg) {
    SSL* ssl = (SSL*)arg;
    EXPECT_EQ(1, SSL_do_handshake(ssl));
    char buf[4096];
    for (size_t i = 0; i < ARRAY_SIZE(BUFSIZE); ++i) {
        int size = BUFSIZE[i];
        for (int j = 0; j < REP; ++j) {
            SSL_read(ssl, buf, size);
        }
    }
    return NULL;
}

TEST_F(SSLTest, ssl_perf) {
    const butil::EndPoint ep(butil::IP_ANY, 5961);
    butil::fd_guard listenfd(butil::tcp_listen(ep));
    ASSERT_GT(listenfd, 0);
    int clifd = tcp_connect(ep, NULL);
    ASSERT_GT(clifd, 0);
    int servfd = accept(listenfd, NULL, NULL);
    ASSERT_GT(servfd, 0);

    brpc::ChannelSSLOptions opt;
    SSL_CTX* cli_ctx = brpc::CreateClientSSLContext(opt);
    SSL_CTX* serv_ctx =
            brpc::CreateServerSSLContext("cert1.crt", "cert1.key",
                                         brpc::SSLOptions(), NULL, NULL);
    SSL* cli_ssl = brpc::CreateSSLSession(cli_ctx, 0, clifd, false);
#if defined(SSL_CTRL_SET_TLSEXT_HOSTNAME) || defined(USE_MESALINK)
    SSL_set_tlsext_host_name(cli_ssl, "localhost");
#endif
    SSL* serv_ssl = brpc::CreateSSLSession(serv_ctx, 0, servfd, true);
    pthread_t cpid;
    pthread_t spid;
    ASSERT_EQ(0, pthread_create(&cpid, NULL, ssl_perf_client, cli_ssl));
    ASSERT_EQ(0, pthread_create(&spid, NULL, ssl_perf_server , serv_ssl));
    ASSERT_EQ(0, pthread_join(cpid, NULL));
    ASSERT_EQ(0, pthread_join(spid, NULL));
    close(clifd);
    close(servfd);
}
