#include <butil/resource_pool.h>
#include <iostream>

class Data {
  public:
    int64_t data;
    char c;
    int32_t data2;
};

class BigData {
  public:
    int64_t data[8];
    char c;
};

// > 256B
class BidBigData {
  public:
    char data[258];
};

using namespace std;

int main() {
    cout << sizeof(Data) << endl;                             // 16
    cout << butil::ResourcePool<Data>::BLOCK_NITEM << endl;   // 256
    cout << sizeof(butil::ResourcePool<Data>::Block) << endl; // 4160 = 4096+64

    cout << sizeof(BigData) << endl;                             // 72=8*8+8
    cout << butil::ResourcePool<BigData>::BLOCK_NITEM << endl;   // 256
    cout << sizeof(butil::ResourcePool<BigData>::Block) << endl; // 18496 = 18432+64

    cout << sizeof(BidBigData) << endl;                             // 258
    cout << butil::ResourcePool<BidBigData>::BLOCK_NITEM << endl;   // 254
    cout << sizeof(butil::ResourcePool<BidBigData>::Block::items) << endl; // 65532
    // items被对齐到64B
    cout << sizeof(butil::ResourcePool<BidBigData>::Block) << endl; // 65600 = 65532+4+64

    // gcc拓展 https://gcc.gnu.org/onlinedocs/gcc-9.1.0/gcc/Alignment.html
    cout << __alignof__(BidBigData) << endl;
    return 0;
}