//xr
#include <iostream>
#include <cstdint>
using std::cout;
using std::cin;
using std::endl;

static constexpr uint64_t hashBits = 32;
using TTab = uint64_t;
using TKey = uint8_t[32];

TTab* table;
static constexpr auto tabF = 8*sizeof(*table);

constexpr void int2Key(TKey k, uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3)
{
    *(uint64_t*)(k) = x0;
    *(uint64_t*)(k+8) = x1;
    *(uint64_t*)(k+16) = x2;
    *(uint64_t*)(k+24) = x3;
}

constexpr auto FNV(const TKey& key)
{
    //32-bit FNV-1a hash
    uint32_t h = 0x811C9DC5;
    for (int n = 0; n < 32; ++n)
        h = (h^(uint32_t)key[n]) * 0x01000193;
    return h;
}

constexpr uint64_t bitPos(uint32_t h, uint32_t n)
{
    return 1ull<<(h-n*tabF);
}

constexpr bool testKey(const TTab* tab, const TKey& key)
{
    auto h = FNV(key);
    auto n = h/tabF;
    return tab[n] & bitPos(h,n);
}

template<bool set=true>
constexpr void setKey(TTab* tab, const TKey& key)
{
    auto h = FNV(key);
    auto n = h/tabF;
    if constexpr (set) {
        tab[n] |= bitPos(h,n);
    } else {
        tab[n] &= ~bitPos(h,n);
    }
}

int main()
{
    cout << "---Bitcoin Hash---\n";
    cout << "hashtable (bits):   " << (1ull<<hashBits) << "\n";
    cout << "hashtable (bytes):  " << (1ull<<hashBits)/8 << "\n";
    cout << "hashtable (MB):     " << ((1ull<<hashBits)/8 >> 20)  << "\n" << endl;
    
    table = new uint64_t[(1ull<<hashBits)/tabF];
    
    uint64_t x0, x1, x2, x3;
    do {
        cout << "BTC key (bytes  0- 7)  :  "; cin >> x0;
        cout << "BTC key (bytes  8-15)  :  "; cin >> x1;
        cout << "BTC key (bytes 16-23)  :  "; cin >> x2;
        cout << "BTC key (bytes 24-31)  :  "; cin >> x3;
        TKey btc;
        int2Key(btc, x0, x1, x2, x3);
        cout << "32-bit FNV-1a hash     :  " << FNV(btc) << "\n";
        
        if (testKey(table, btc)) {
            cout << "Key is probably in the list!\n" << endl;
        } else {
            cout << "Key is definitly NOT in the list! - Adding it now...\n" << endl;
            setKey(table, btc);
        }
        
    } while (x0+x1+x2+x3);
    
    delete[] table;
    return 0;
}
