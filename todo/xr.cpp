// xr
#include <cstdint>
#include <iomanip>
#include <iostream>
using std::cin;
using std::cout;
using std::endl;

static constexpr uint64_t keyBytes = 20;
static constexpr uint64_t hashTabBitsExp = 32;
using THash = uint32_t; // may be > hashTabBitsExp

using TKey = uint8_t[keyBytes];
using TTab = uint64_t;
static constexpr uint64_t tabS = 6; // log2(8*sizeof(TTab))
static constexpr uint64_t tabM = ((uint64_t)1 << tabS) - 1;
static constexpr uint64_t hashFctBits = 8 * sizeof(THash);

// FNV-1a hash
template <THash f, int n = 0>
static constexpr THash SNVByte(const TKey &key, THash h) {
  if constexpr (n >= keyBytes) {
    return h;
  } else {
    return SNVByte<f, n + 1>(key, (h ^ (THash)key[n]) * f);
  }
}

static constexpr THash FNV(const TKey &key) {
  THash h = 0;
  if constexpr (hashFctBits == 32) {
    h = SNVByte<0x01000193>(key, 0x811C9DC5);
  }
  // if constexpr (hashFctBits==64) {
  //    h = SNVByte<0x00000100000001B3ull>(key, 0xCBF29CE484222325ull);
  //}

  if constexpr (hashFctBits > hashTabBitsExp) {
    h = (h ^ (h >> (hashFctBits - hashTabBitsExp))) &
        (((THash)1 << hashTabBitsExp) - 1);
  }

  return h;
}

static constexpr TTab bitPos(THash h) { return (TTab)1 << (h & tabM); }

static constexpr bool testKey(const TTab *tab, const TKey &key) {
  auto h = FNV(key);
  return tab[h >> tabS] & bitPos(h);
}

template <bool set = true>
static constexpr void setKey(TTab *tab, const TKey &key) {
  auto h = FNV(key);
  if constexpr (set) {
    tab[h >> tabS] |= bitPos(h);
  } else {
    tab[h >> tabS] &= ~bitPos(h);
  }
}

static constexpr void addInt2Key(TKey &k, uint64_t x, int b) {
  if (b + 8 <= keyBytes) {
    *(uint64_t *)(k + b) = x;
  } else {
    for (int c = b; c < keyBytes; ++c)
      k[c] = (x >> ((c - b) * 8)) & 0xFF;
  }
}
static constexpr bool keyZero(const TKey &k) {
  for (int b = 0; b < keyBytes; ++b)
    if (k[b])
      return false;
  return true;
}
static void printKey(const TKey &k) {
  for (int b = 0; b < keyBytes; ++b) {
    if (!(b & 3))
      cout << "\n";
    cout << std::hex << std::setw(2) << (int)k[b] << " " << std::dec;
  }
  cout << endl;
}

int main() {
  static_assert(hashFctBits >= hashTabBitsExp);

  cin.unsetf(std::ios::basefield);
  cout << "---Bitcoin Hash---\n";
  cout << "hashtable (bits)   : " << (1ull << (hashTabBitsExp)) << "\n";
  cout << "hashtable (bytes)  : " << (1ull << (hashTabBitsExp - 3)) << "\n";
  cout << "hashtable (MB)     : " << (1ull << (hashTabBitsExp - 23)) << "\n"
       << endl;

  TTab *preLookup =
      new TTab[1ull << (hashTabBitsExp > tabS ? hashTabBitsExp - tabS : 0)]();

  TKey btc;
  do {

    for (int b = 0; b < keyBytes; b += 8) {
      uint64_t x;
      cout << "BTC key (bytes " << std::setw(2) << b << "-" << std::setw(2)
           << (b + 8 > keyBytes ? keyBytes - 1 : b + 7) << ")  :  ";
      cin >> x;
      addInt2Key(btc, x, b);
    }
    // printKey(btc);

    cout << "\n32-bit FNV-1a hash     :  " << std::hex << std::uppercase
         << std::showbase << FNV(btc) << std::dec << "\n";

    if (testKey(preLookup, btc)) {
      cout << "Key is probably in the list!\n" << endl;
    } else {
      cout << "Key is definitely NOT in the list! - Adding it now...\n" << endl;
      setKey(preLookup, btc);
    }

  } while (!keyZero(btc));

  delete[] preLookup;
  return 0;
}
