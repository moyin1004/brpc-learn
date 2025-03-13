#include "proto/http.pb.h"
#include <brpc/server.h>
#include <bvar/bvar.h>
#include <gflags/gflags.h>
#include <iostream>
#include <json2pb/pb_to_json.h>
#include <butil/logging.h>

using namespace std;
using namespace example;

DEFINE_int32(port, 8010, "TCP Port of this server");

class HttpServiceImpl : public HttpService {
  public:
    HttpServiceImpl() {}
    virtual ~HttpServiceImpl() {}
    void Echo(google::protobuf::RpcController *cntl_base, const HttpRequest *, HttpResponse *,
              google::protobuf::Closure *done) {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);

        brpc::Controller *cntl = static_cast<brpc::Controller *>(cntl_base);

        // optional: set a callback function which is called after response is sent
        // and before cntl/req/res is destructed.
        cntl->set_after_rpc_resp_fn(std::bind(&HttpServiceImpl::CallAfterRpc, std::placeholders::_1,
                                              std::placeholders::_2, std::placeholders::_3));

        // Fill response.
        cntl->http_response().set_content_type("text/plain");
        butil::IOBufBuilder os;
        os << cntl->http_request().major_version() << endl;
        os << "queries:";
        cntl->http_request().method();
        for (brpc::URI::QueryIterator it = cntl->http_request().uri().QueryBegin();
             it != cntl->http_request().uri().QueryEnd(); ++it) {
            os << ' ' << it->first << '=' << it->second;
        }
        os << "\nbody: " << cntl->request_attachment() << '\n';
        os.move_to(cntl->response_attachment());
    }

    // optional
    static void CallAfterRpc(brpc::Controller *cntl, const google::protobuf::Message *req,
                             const google::protobuf::Message *res) {
        // at this time res is already sent to client, but cntl/req/res is not destructed
        std::string req_str;
        std::string res_str;
        json2pb::ProtoMessageToJson(*req, &req_str, NULL);
        json2pb::ProtoMessageToJson(*res, &res_str, NULL);
        cout << "req:" << req_str << " res:" << res_str << endl;
    }
};

int main() {
    printf("Hello World\n");
    // 打印分层日志
    logging::FLAGS_v = 200;

    // bvar::Adder<T>用于累加，下面定义了一个统计read error总数的Adder。
    bvar::Adder<int> g_read_error;
    // 把bvar::Window套在其他bvar上就可以获得时间窗口内的值。
    bvar::Window<bvar::Adder<int>> g_read_error_minute("foo_bar", "read_error", &g_read_error, 60);
    //                                                     ^          ^                         ^
    //                                                    前缀       监控项名称 60秒,忽略则为10秒

    // bvar::LatencyRecorder是一个复合变量，可以统计：总量、qps、平均延时，延时分位值，最大延时。
    bvar::LatencyRecorder g_write_latency("foo_bar", "write");
    //                                      ^          ^
    //                                     前缀
    //                                     监控项，别加latency！LatencyRecorder包含多个bvar，它们会加上各自的后缀，比如write_qps,
    //                                     write_latency等等。

    // 定义一个统计“已推入task”个数的变量。
    bvar::Adder<int> g_task_pushed("foo_bar", "task_pushed");
    // 把bvar::PerSecond套在其他bvar上可以获得时间窗口内*平均每秒*的值，这里是每秒内推入task的个数。
    bvar::PerSecond<bvar::Adder<int>> g_task_pushed_second("foo_bar", "task_pushed_second",
                                                           &g_task_pushed);
    //       ^ ^
    //    和Window不同，PerSecond会除以时间窗口的大小.
    //    时间窗口是最后一个参数，这里没填，就是默认10秒。
    // 碰到read error
    g_read_error << 1;

    // write_latency是23ms
    g_write_latency << 23;

    // 推入了1个task
    g_task_pushed << 1;

    brpc::Server server;
    HttpServiceImpl http_svc;
    if (server.AddService(&http_svc, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        cout << "Fail to add http_svc";
        return -1;
    }
    brpc::ServerOptions options;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start HttpServer";
        return -1;
    }
    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    // sleep(10);
    return 0;
}