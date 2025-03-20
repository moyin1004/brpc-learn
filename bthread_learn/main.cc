#include <bthread/bthread.h>
#include <iostream>
#include <gflags/gflags.h>

using namespace std;

DECLARE_int32(task_group_ntags);
namespace bthread {
    DECLARE_int32(bthread_concurrency);
    DECLARE_int32(bthread_min_concurrency);
}

void *bthread_main_func(void *arg) {
    LOG(INFO) << "bthread_main_func";
    // bthread_exit(nullptr);
    return nullptr;
}

void TestBthreadTag() {
    logging::FLAGS_v = 200;
    bthread_t tid;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    // tag不能超过FLAGS_task_group_ntags
    // FLAGS_task_group_ntags不能超过线程数量FLAGS_bthread_min_concurrency/FLAGS_bthread_concurrency 会在创建TaskControl时死循环
    // attr.tag = 2;
    int rc = bthread_start_background(&tid, &attr, bthread_main_func, nullptr);
    if (rc != 0) {
        cout << "bthread_start_background ret:" << rc << endl;
        return ;
    }
    // bthread_stop(tid);
    bthread_join(tid, nullptr);
}

// ./bthread_learn -bthread_min_concurrency=4 -task_group_ntags=5
int main(int argc, char** argv) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    cout << "Hello World" << endl;
    TestBthreadTag();
    return 0;
}