#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if __has_include(<sqlite3.h>)
#include <sqlite3.h>
#define BTCSM_HAVE_SQLITE3 1
#else
#define BTCSM_HAVE_SQLITE3 0
#endif

using namespace std;
namespace fs = std::filesystem;

std::atomic<bool> keep_running(true);

void signal_handler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    keep_running = false;
  }
}

// ==============================================================================
// 1. Core Logic from xr.cpp (FNV Hash and Lookup Table)
// ==============================================================================
#define main xr_standalone_main
#include "xr.cpp"
#undef main

// Global Configurations for Multi-Tier Filters
const int NUM_FILTERS = 2;
const int filterSizesMB[] = {512, 16384};
const int filterBitsExp[] = {32, 37};

// Balance Filters (addresses with unspent outputs only)
const int NUM_BAL_FILTERS = 2;
const int balFilterSizesMB[] = {512, 16384};
const int balFilterBitsExp[] = {32, 37};

typedef std::atomic<uint64_t> ATTab;
static_assert(
    ATTab::is_always_lock_free,
    "std::atomic<uint64_t> must be lock-free for safe bulk binary I/O");
ATTab *preLookups[NUM_FILTERS];
ATTab *balLookups[NUM_BAL_FILTERS];

// ==============================================================================
// SHA256 (for UTXO txid computation)
// ==============================================================================
static inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static const uint32_t SHA256_K[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
static array<uint8_t,32> sha256(const uint8_t *data, size_t len) {
  uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                   0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  uint64_t bitLen = (uint64_t)len * 8;
  vector<uint8_t> msg(data, data + len);
  msg.push_back(0x80);
  while ((msg.size() % 64) != 56) msg.push_back(0x00);
  for (int i = 7; i >= 0; i--) msg.push_back((uint8_t)(bitLen >> (i * 8)));
  for (size_t i = 0; i < msg.size(); i += 64) {
    uint32_t w[64];
    for (int j = 0; j < 16; j++)
      w[j] = ((uint32_t)msg[i+j*4]<<24)|((uint32_t)msg[i+j*4+1]<<16)|
              ((uint32_t)msg[i+j*4+2]<<8)|((uint32_t)msg[i+j*4+3]);
    for (int j = 16; j < 64; j++) {
      uint32_t s0 = rotr32(w[j-15],7)^rotr32(w[j-15],18)^(w[j-15]>>3);
      uint32_t s1 = rotr32(w[j-2],17)^rotr32(w[j-2],19)^(w[j-2]>>10);
      w[j] = w[j-16]+s0+w[j-7]+s1;
    }
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hv=h[7];
    for (int j = 0; j < 64; j++) {
      uint32_t S1  = rotr32(e,6)^rotr32(e,11)^rotr32(e,25);
      uint32_t ch  = (e&f)^(~e&g);
      uint32_t t1  = hv+S1+ch+SHA256_K[j]+w[j];
      uint32_t S0  = rotr32(a,2)^rotr32(a,13)^rotr32(a,22);
      uint32_t maj = (a&b)^(a&c)^(b&c);
      uint32_t t2  = S0+maj;
      hv=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hv;
  }
  array<uint8_t,32> r;
  for (int i = 0; i < 8; i++) {
    r[i*4]=(uint8_t)(h[i]>>24); r[i*4+1]=(uint8_t)(h[i]>>16);
    r[i*4+2]=(uint8_t)(h[i]>>8); r[i*4+3]=(uint8_t)(h[i]);
  }
  return r;
}
static array<uint8_t,32> dsha256(const uint8_t *data, size_t len) {
  auto h1 = sha256(data, len); return sha256(h1.data(), 32);
}

// RIPEMD160 (for P2PK Hash160 computation)
static inline uint32_t rotl32r(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static uint32_t rmd_f(uint32_t x,uint32_t y,uint32_t z,int j) {
  if(j<16)return x^y^z; if(j<32)return(x&y)|(~x&z);
  if(j<48)return(x|~y)^z; if(j<64)return(x&z)|(y&~z); return x^(y|~z);
}
static const uint32_t RMD_K[5] ={0x00000000u,0x5A827999u,0x6ED9EBA1u,0x8F1BBCDCu,0xA953FD4Eu};
static const uint32_t RMD_KP[5]={0x50A28BE6u,0x5C4DD124u,0x6D703EF3u,0x7A6D76E9u,0x00000000u};
static const int RMD_R[80] ={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,7,4,13,1,10,6,15,3,12,0,9,5,2,14,11,8,3,10,14,4,9,15,8,1,2,7,0,6,13,11,5,12,1,9,11,10,0,8,12,4,13,3,7,15,14,5,6,2,4,0,5,9,7,12,2,10,14,1,3,8,11,6,15,13};
static const int RMD_RP[80]={5,14,7,0,9,2,11,4,13,6,15,8,1,10,3,12,6,11,3,7,0,13,5,10,14,15,8,12,4,9,1,2,15,5,1,3,7,14,6,9,11,8,12,2,10,0,4,13,8,6,4,1,3,11,15,0,5,12,2,13,9,7,10,14,12,15,10,4,1,5,8,7,6,2,13,14,0,3,9,11};
static const int RMD_S[80] ={11,14,15,12,5,8,7,9,11,13,14,15,6,7,9,8,7,6,8,13,11,9,7,15,7,12,15,9,11,7,13,12,11,13,6,7,14,9,13,15,14,8,13,6,5,12,7,5,11,12,14,15,14,15,9,8,9,14,5,6,8,6,5,12,9,15,5,11,6,8,13,12,5,12,13,14,11,8,5,6};
static const int RMD_SP[80]={8,9,9,11,13,15,15,5,7,7,8,11,14,14,12,6,9,13,15,7,12,8,9,11,7,7,12,7,6,15,13,11,9,7,15,11,8,6,6,14,12,13,5,14,13,13,7,5,15,5,8,11,14,14,6,14,6,9,12,9,12,5,15,8,8,5,12,9,12,5,14,6,8,13,6,5,15,13,11,11};
static array<uint8_t,20> ripemd160(const uint8_t *data, size_t len) {
  uint32_t h[5]={0x67452301u,0xEFCDAB89u,0x98BADCFEu,0x10325476u,0xC3D2E1F0u};
  uint64_t bitLen=(uint64_t)len*8;
  vector<uint8_t> msg(data,data+len);
  msg.push_back(0x80);
  while((msg.size()%64)!=56) msg.push_back(0);
  for(int i=0;i<8;i++) msg.push_back((uint8_t)(bitLen>>(i*8)));
  for(size_t i=0;i<msg.size();i+=64){
    uint32_t X[16];
    for(int j=0;j<16;j++) X[j]=((uint32_t)msg[i+j*4])|((uint32_t)msg[i+j*4+1]<<8)|((uint32_t)msg[i+j*4+2]<<16)|((uint32_t)msg[i+j*4+3]<<24);
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],ap=h[0],bp=h[1],cp=h[2],dp=h[3],ep=h[4];
    for(int j=0;j<80;j++){
      uint32_t T=rotl32r(a+rmd_f(b,c,d,j)+X[RMD_R[j]]+RMD_K[j/16],RMD_S[j])+e;
      a=e;e=d;d=rotl32r(c,10);c=b;b=T;
      T=rotl32r(ap+rmd_f(bp,cp,dp,79-j)+X[RMD_RP[j]]+RMD_KP[j/16],RMD_SP[j])+ep;
      ap=ep;ep=dp;dp=rotl32r(cp,10);cp=bp;bp=T;
    }
    uint32_t T=h[1]+c+dp;h[1]=h[2]+d+ep;h[2]=h[3]+e+ap;h[3]=h[4]+a+bp;h[4]=h[0]+b+cp;h[0]=T;
  }
  array<uint8_t,20> r;
  for(int i=0;i<5;i++){r[i*4]=(uint8_t)h[i];r[i*4+1]=(uint8_t)(h[i]>>8);r[i*4+2]=(uint8_t)(h[i]>>16);r[i*4+3]=(uint8_t)(h[i]>>24);}
  return r;
}
static array<uint8_t,20> hash160(const uint8_t *data, size_t len) {
  auto s = sha256(data, len); return ripemd160(s.data(), 32);
}

// UTXO map types: key = txid (32 bytes) + vout index (4 bytes)
struct UTXOKey {
  uint8_t data[36];
  bool operator==(const UTXOKey &o) const { return memcmp(data, o.data, 36) == 0; }
};
struct UTXOKeyHash {
  size_t operator()(const UTXOKey &k) const {
    uint64_t h = 0xCBF29CE484222325ull;
    for (int i = 0; i < 36; i++) { h ^= k.data[i]; h *= 0x00000100000001B3ull; }
    return (size_t)h;
  }
};

struct UtxoMapVal {
  array<uint8_t, 32> key32{};
  uint64_t value_sat = 0;
};

using UtxoMap = unordered_map<UTXOKey, UtxoMapVal, UTXOKeyHash>;

// ==============================================================================
// 2. Helper Functions (Decoders, Parsers)
// ==============================================================================
vector<uint8_t> hexToBytes(const string &hex) {
  vector<uint8_t> bytes;
  for (size_t i = 0; i < hex.length(); i += 2) {
    string byteString = hex.substr(i, 2);
    uint8_t byte = (uint8_t)strtol(byteString.c_str(), nullptr, 16);
    bytes.push_back(byte);
  }
  return bytes;
}

string toHex(const vector<uint8_t> &data) {
  string s;
  s.reserve(data.size() * 2);
  static constexpr char hx[] = "0123456789abcdef";
  for (uint8_t b : data) {
    s.push_back(hx[b >> 4]);
    s.push_back(hx[b & 0xF]);
  }
  return s;
}

// Converts a Base58 Bitcoin Address to its raw bytes (removes Base58 encoding)
vector<uint8_t> decodeBase58(const string &b58) {
  static const string ALPHABET =
      "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
  vector<uint8_t> bytes(b58.length() * 733 / 1000 + 1, 0);
  size_t length = 0;

  for (char c : b58) {
    size_t id = ALPHABET.find(c);
    if (id == string::npos)
      return {}; // Invalid character
    uint32_t val = id;

    size_t j = 0;
    for (auto it = bytes.rbegin();
         (val != 0 || j < length) && (it != bytes.rend()); ++it, ++j) {
      val += 58 * (*it);
      *it = val % 256;
      val /= 256;
    }
    length = j;
  }

  int zeros = 0;
  while (zeros < b58.size() && b58[zeros] == '1')
    zeros++;

  auto it = bytes.begin();
  while (it != bytes.end() && *it == 0)
    it++;

  vector<uint8_t> result;
  result.assign(zeros, 0);
  result.insert(result.end(), it, bytes.end());

  return result;
}

uint64_t readVarInt(ifstream &file) {
  uint8_t first;
  if (!file.read(reinterpret_cast<char *>(&first), 1))
    return 0;
  if (first < 0xFD)
    return first;
  if (first == 0xFD) {
    uint16_t v;
    file.read(reinterpret_cast<char *>(&v), 2);
    return v;
  }
  if (first == 0xFE) {
    uint32_t v;
    file.read(reinterpret_cast<char *>(&v), 4);
    return v;
  }
  uint64_t v;
  file.read(reinterpret_cast<char *>(&v), 8);
  return v;
}

// Like readVarInt but also appends the raw bytes to buf (for txid computation)
static uint64_t readVarIntBuf(ifstream &file, vector<uint8_t> &buf) {
  uint8_t first;
  file.read(reinterpret_cast<char *>(&first), 1);
  buf.push_back(first);
  if (first < 0xFD) return first;
  if (first == 0xFD) {
    uint16_t v; file.read(reinterpret_cast<char *>(&v), 2);
    buf.push_back(v & 0xFF); buf.push_back(v >> 8); return v;
  }
  if (first == 0xFE) {
    uint32_t v; file.read(reinterpret_cast<char *>(&v), 4);
    for (int i = 0; i < 4; i++) buf.push_back((v >> (i*8)) & 0xFF); return v;
  }
  uint64_t v; file.read(reinterpret_cast<char *>(&v), 8);
  for (int i = 0; i < 8; i++) buf.push_back((v >> (i*8)) & 0xFF); return v;
}

// ==============================================================================
// 3. Command: Download
// ==============================================================================
void cmdDownload() {
  cout << "Starting download of sample block files...\n";
  string targetDir = "blocks";
  if (!fs::exists(targetDir))
    fs::create_directory(targetDir);

  vector<string> files = {"blk00000.dat", "blk00021.dat", "blk05000.dat"};
  string baseUrl = "http://alexander-thurn.de/temp/blocks/";

  for (const auto &file : files) {
    cout << "Downloading " << file << "...\n";
    string url = baseUrl + file;
    string targetPath = targetDir + "/" + file;
    string command = "curl -L -f -s -o " + targetPath + " " + url;

    int r = system(command.c_str());
    if (r == 0)
      cout << "Successfully downloaded: " << file << "\n";
    else
      cerr << "Error downloading " << file << " (Curl Code: " << r << ")\n";
  }
  cout << "Download process finished.\n";
}

// ==============================================================================
// 4. Command: Parse
// ==============================================================================
void processChunk(int chunk_index, bool debug) {
  int files_per_chunk = 1000;
  int start_file = chunk_index * files_per_chunk;
  int end_file = start_file + files_per_chunk - 1;

  string blocksDir = "blocks";
  string outDir = "chunks";
  string outLogname = outDir + "/chunk_" + to_string(chunk_index) + ".log";

  string filterDir = "filter";

  vector<string> datFiles;
  vector<int> missingFiles;
  int expectedIndex = start_file;

  for (const auto &entry : fs::directory_iterator(blocksDir)) {
    if (entry.path().extension() == ".dat") {
      string filename = entry.path().filename().string();
      if (filename.length() == 12 && filename.substr(0, 3) == "blk") {
        try {
          int fileIndex = stoi(filename.substr(3, 5));
          if (fileIndex >= start_file && fileIndex <= end_file) {
            datFiles.push_back(entry.path().string());
          }
        } catch (...) {
        }
      }
    }
  }
  sort(datFiles.begin(), datFiles.end());

  if (datFiles.empty()) {
    cout << "[WARNING] No .dat files found for Chunk " << chunk_index
         << " (Expected files blk" << setfill('0') << setw(5) << start_file
         << ".dat to blk" << setfill('0') << setw(5) << end_file << ".dat)\n";

    return;
  }

  for (const string &filepath : datFiles) {
    string filename = fs::path(filepath).filename().string();
    int fileIndex = stoi(filename.substr(3, 5));
    if (fileIndex != expectedIndex) {
      for (int i = expectedIndex; i < fileIndex && missingFiles.size() < 3;
           i++) {
        missingFiles.push_back(i);
      }
    }
    expectedIndex = fileIndex + 1;
  }

  if (!missingFiles.empty()) {
    cout << "[WARNING] Missing .dat files detected in this chunk (e.g. blk"
         << setfill('0') << setw(5) << missingFiles[0] << ".dat";
    if (missingFiles.size() > 1) {
      cout << ", blk" << setfill('0') << setw(5) << missingFiles[1] << ".dat";
    }
    cout << "...)\nScript parses available files exactly as they are.\n\n";
  }

  cout << "Extracting Chunk " << chunk_index << " (Files blk" << setfill('0')
       << setw(5) << start_file << ".dat to blk" << setfill('0') << setw(5)
       << end_file << ".dat)\n";

  uint64_t total_payloads_written = 0;
  uint64_t count_p2pkh = 0, count_p2sh = 0, count_p2wpkh = 0, count_p2tr = 0, count_p2wsh = 0, count_p2pk = 0;

  std::mutex statsMutex;
  std::atomic<size_t> fileTaskIdx(0);
  unsigned int numThreads = std::thread::hardware_concurrency();
  if (numThreads == 0)
    numThreads = 4;

  cout << "Parallel processing with " << numThreads << " threads...\n";
  vector<thread> threads;
  for (unsigned int t = 0; t < numThreads; ++t) {
    threads.emplace_back([&]() {
      while (keep_running) {
        size_t idx = fileTaskIdx.fetch_add(1);
        if (idx >= datFiles.size())
          break;

        const string &filepath = datFiles[idx];
        ifstream file(filepath, ios::binary);
        if (!file)
          continue;

        uint64_t lTotal = 0, lP2PKH = 0, lP2SH = 0, lP2WPKH = 0, lP2TR = 0, lP2WSH = 0, lP2PK = 0;
        string filename = fs::path(filepath).filename().string();
        static std::mutex coutMtx;
        if (!debug) {
          std::lock_guard<std::mutex> lock(coutMtx);
          cout << "Parsing file " << filename << "..." << endl;
        }
        bool dbg_p2pkh = false, dbg_p2sh = false, dbg_p2wpkh = false;

        uint32_t magic, blockSize;
        while (keep_running && file.read(reinterpret_cast<char *>(&magic), 4)) {
          if (magic != 0xD9B4BEF9) {
            file.seekg(-3, ios::cur);
            continue;
          }
          if (!file.read(reinterpret_cast<char *>(&blockSize), 4))
            break;
          auto blockStart = file.tellg();
          file.seekg(80, ios::cur);
          uint64_t txCount = readVarInt(file);

          for (uint64_t t_tx = 0; t_tx < txCount && keep_running; ++t_tx) {
            uint32_t version;
            file.read(reinterpret_cast<char *>(&version), 4);
            bool isSW = false;
            uint8_t mCheck[2];
            auto cPos = file.tellg();
            if (file.read(reinterpret_cast<char *>(mCheck), 2)) {
              if (mCheck[0] == 0x00 && mCheck[1] == 0x01)
                isSW = true;
              else
                file.seekg(cPos);
            }

            uint64_t inC = readVarInt(file);
            for (uint64_t i = 0; i < inC; ++i) {
              file.seekg(36, ios::cur);
              uint64_t sLen = readVarInt(file);
              file.seekg(sLen, ios::cur);
              file.seekg(4, ios::cur);
            }

            uint64_t outC = readVarInt(file);
            for (uint64_t i = 0; i < outC; ++i) {
              uint64_t val;
              file.read(reinterpret_cast<char *>(&val), 8);
              uint64_t sLen = readVarInt(file);
              vector<uint8_t> script(sLen);
              file.read(reinterpret_cast<char *>(script.data()), sLen);

              if (val == 0)
                continue;

              {
                TKey kBuf = {};
                bool recognised = false;
                if (sLen==25 && script[0]==0x76 && script[1]==0xa9 && script[2]==0x14 && script[23]==0x88 && script[24]==0xac) {
                  memcpy(kBuf, script.data()+3, 20); lP2PKH++; recognised=true;
                  if (debug && !dbg_p2pkh) { std::lock_guard<std::mutex> lock(coutMtx); cout << "[DEBUG] " << filename << " | P2PKH:  " << toHex(vector<uint8_t>(kBuf,kBuf+20)) << "\n"; dbg_p2pkh=true; }
                } else if (sLen==23 && script[0]==0xa9 && script[1]==0x14 && script[22]==0x87) {
                  memcpy(kBuf, script.data()+2, 20); lP2SH++; recognised=true;
                  if (debug && !dbg_p2sh) { std::lock_guard<std::mutex> lock(coutMtx); cout << "[DEBUG] " << filename << " | P2SH:   " << toHex(vector<uint8_t>(kBuf,kBuf+20)) << "\n"; dbg_p2sh=true; }
                } else if (sLen==22 && script[0]==0x00 && script[1]==0x14) {
                  memcpy(kBuf, script.data()+2, 20); lP2WPKH++; recognised=true;
                  if (debug && !dbg_p2wpkh) { std::lock_guard<std::mutex> lock(coutMtx); cout << "[DEBUG] " << filename << " | P2WPKH: " << toHex(vector<uint8_t>(kBuf,kBuf+20)) << "\n"; dbg_p2wpkh=true; }
                } else if (sLen==34 && script[0]==0x51 && script[1]==0x20) {
                  memcpy(kBuf, script.data()+2, 32); lP2TR++; recognised=true;
                } else if (sLen==34 && script[0]==0x00 && script[1]==0x20) {
                  memcpy(kBuf, script.data()+2, 32); lP2WSH++; recognised=true;
                } else if (sLen==35 && script[0]==0x21 && script[34]==0xac) {
                  auto h=hash160(script.data()+1,33); memcpy(kBuf,h.data(),20); lP2PK++; recognised=true;
                } else if (sLen==67 && script[0]==0x41 && script[66]==0xac) {
                  auto h=hash160(script.data()+1,65); memcpy(kBuf,h.data(),20); lP2PK++; recognised=true;
                }
                if (recognised) {
                  uint64_t fH = SNVByte<0x00000100000001B3ull>(kBuf, 0xCBF29CE484222325ull);
                  for (int f = 0; f < NUM_FILTERS; ++f) {
                    uint64_t h = fH & ((1ull << filterBitsExp[f]) - 1);
                    preLookups[f][h >> tabS].fetch_or(((TTab)1 << (h & tabM)), std::memory_order_relaxed);
                  }
                  lTotal++;
                }
              }
            }

            if (isSW) {
              uint64_t wC = inC;
              for (uint64_t i = 0; i < wC; ++i) {
                uint64_t wItems = readVarInt(file);
                for (uint64_t w = 0; w < wItems; ++w) {
                  uint64_t iLen = readVarInt(file);
                  file.seekg(iLen, ios::cur);
                }
              }
            }
            file.seekg(4, ios::cur);
          }
          file.seekg(blockStart + static_cast<streamoff>(blockSize));
        }

        {
          std::lock_guard<std::mutex> lock(statsMutex);
          total_payloads_written += lTotal;
          count_p2pkh += lP2PKH;
          count_p2sh += lP2SH;
          count_p2wpkh += lP2WPKH;
          count_p2tr += lP2TR;
          count_p2wsh += lP2WSH;
          count_p2pk += lP2PK;
        }
      }
    });
  }

  for (auto &t : threads)
    t.join();

  if (!keep_running) {
    cout << "[WARNING] Aborting chunk " << chunk_index
         << " due to exit signal...\n";
  }

  cout << "\nDone parsing chunk " << chunk_index << "!\n";
  cout << "Saving all master filters to disk...\n";

  for (int idx = 0; idx < NUM_FILTERS; ++idx) {
    string filename =
        filterDir + "/" + to_string(filterSizesMB[idx]) + "mb.bin";
    uint64_t arraySize = 1ull << (filterBitsExp[idx] - tabS);
    ofstream filterOut(filename, ios::binary);
    filterOut.write(reinterpret_cast<const char *>(preLookups[idx]),
                    arraySize * sizeof(TTab));
    filterOut.close();
  }

  cout << "Writing parsing log to " << outLogname << "...\n";
  ofstream logOut(outLogname);
  if (keep_running && datFiles.size() == 1000) {
    logOut << "Completed\n";
  } else if (!keep_running) {
    logOut << "Aborted\n";
  } else {
    logOut << datFiles.size() << "\n";
  }
  logOut << "Chunk: " << chunk_index << "\n";
  logOut << "Processed Files: blk" << setfill('0') << setw(5) << start_file
         << ".dat to blk" << setfill('0') << setw(5) << end_file << ".dat ("
         << datFiles.size() << " files)\n";
  logOut << "Total Payloads Processed: " << total_payloads_written << "\n";
  logOut << "  - P2PKH:  " << count_p2pkh << "\n";
  logOut << "  - P2SH:   " << count_p2sh << "\n";
  logOut << "  - P2WPKH: " << count_p2wpkh << "\n";
  logOut << "  - P2TR:   " << count_p2tr << "\n";
  logOut << "  - P2WSH:  " << count_p2wsh << "\n";
  logOut << "  - P2PK:   " << count_p2pk << "\n";
  logOut.close();

  cout << "Extracted a total of " << total_payloads_written << " entries (32-byte keys)\n";
  cout << "  - P2PKH:  " << count_p2pkh << "\n";
  cout << "  - P2SH:   " << count_p2sh << "\n";
  cout << "  - P2WPKH: " << count_p2wpkh << "\n";
  cout << "  - P2TR:   " << count_p2tr << "\n";
  cout << "  - P2WSH:  " << count_p2wsh << "\n";
  cout << "  - P2PK:   " << count_p2pk << "\n";
}

// ==============================================================================
// 4b. Balance Filter Builder (sequential UTXO pass over all block files)
// ==============================================================================
static const char UTXOCP_MAGIC[8] = {'B', 'T', 'U', 'T', 'C', 'P', '0', '1'};
// v3 checkpoints: same manifest as v2, each UTXO row is 36+32+8 bytes (adds value_sat).
static const uint32_t UTXOCP_VER = 3;
static const size_t UTXOCP_ROW_BYTES = 36 + 32 + 8;

static void buildDatFileManifest(const vector<string> &datFiles,
                                 vector<pair<string, uint64_t>> &out) {
  out.clear();
  out.reserve(datFiles.size());
  error_code ec;
  for (const string &p : datFiles) {
    uint64_t sz = fs::file_size(p, ec);
    if (ec)
      sz = 0;
    out.push_back({p, sz});
  }
}

static bool writeUtxoStateV2(const string &pathTmp, const string &pathFinal,
                             const vector<pair<string, uint64_t>> &manifest,
                             const UtxoMap &utxoMap, uint32_t next_file_index) {
  ofstream f(pathTmp, ios::binary | ios::trunc);
  if (!f)
    return false;
  f.write(UTXOCP_MAGIC, 8);
  f.write(reinterpret_cast<const char *>(&UTXOCP_VER), 4);
  uint32_t nman = static_cast<uint32_t>(manifest.size());
  f.write(reinterpret_cast<const char *>(&nman), 4);
  for (const auto &e : manifest) {
    uint16_t pl = static_cast<uint16_t>(min(e.first.size(), (size_t)65535));
    f.write(reinterpret_cast<const char *>(&pl), 2);
    if (pl)
      f.write(e.first.data(), pl);
    f.write(reinterpret_cast<const char *>(&e.second), 8);
  }
  f.write(reinterpret_cast<const char *>(&next_file_index), 4);
  uint32_t pad = 0;
  f.write(reinterpret_cast<const char *>(&pad), 4);
  uint64_t n = utxoMap.size();
  f.write(reinterpret_cast<const char *>(&n), 8);
  for (const auto &kv : utxoMap) {
    f.write(reinterpret_cast<const char *>(kv.first.data), 36);
    f.write(reinterpret_cast<const char *>(kv.second.key32.data()), 32);
    f.write(reinterpret_cast<const char *>(&kv.second.value_sat), 8);
  }
  f.close();
  if (!f.good()) {
    try {
      fs::remove(pathTmp);
    } catch (...) {
    }
    return false;
  }
  error_code ec;
  fs::rename(pathTmp, pathFinal, ec);
  return !ec;
}

#if BTCSM_HAVE_SQLITE3
static bool exportBalanceUtxoSqlite(const string &stateDir, const string &filterDir,
                                    const UtxoMap &utxoMap) {
  const string stateDbPath = stateDir + "/balance_utxo.sqlite";
  const string filterDbPath = filterDir + "/balance_utxo.sqlite";
  const string tmp = stateDbPath + ".tmp";
  error_code ec;
  sqlite3_stmt *st = nullptr;
  sqlite3 *db = nullptr;
  char *errmsg = nullptr;

  try {
    fs::remove(tmp);
  } catch (...) {
  }

  if (sqlite3_open_v2(tmp.c_str(), &db,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
    cerr << "[WARN] sqlite3_open: " << (db ? sqlite3_errmsg(db) : "?") << "\n";
    if (db)
      sqlite3_close(db);
    return false;
  }

  auto exec = [&](const char *sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
      cerr << "[WARN] sqlite exec: " << (errmsg ? errmsg : "?") << "\n";
      sqlite3_free(errmsg);
      errmsg = nullptr;
      return false;
    }
    return true;
  };

  auto cleanupFail = [&]() {
    if (st) {
      sqlite3_finalize(st);
      st = nullptr;
    }
    if (db) {
      sqlite3_close(db);
      db = nullptr;
    }
    try {
      fs::remove(tmp);
    } catch (...) {
    }
  };

  if (!exec("PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF;")) {
    cleanupFail();
    return false;
  }
  if (!exec("CREATE TABLE utxo(txid BLOB NOT NULL, vout INTEGER NOT NULL, "
            "key32 BLOB NOT NULL, value_sat INTEGER NOT NULL, "
            "PRIMARY KEY(txid, vout));")) {
    cleanupFail();
    return false;
  }
  if (!exec("CREATE INDEX idx_utxo_key32 ON utxo(key32);")) {
    cleanupFail();
    return false;
  }
  if (sqlite3_prepare_v2(db,
                         "INSERT INTO utxo(txid,vout,key32,value_sat) VALUES(?,?,?,?);",
                         -1, &st, nullptr) != SQLITE_OK) {
    cerr << "[WARN] sqlite prepare: " << sqlite3_errmsg(db) << "\n";
    cleanupFail();
    return false;
  }
  if (!exec("BEGIN;")) {
    cleanupFail();
    return false;
  }
  for (const auto &kv : utxoMap) {
    uint32_t vout = 0;
    memcpy(&vout, kv.first.data + 32, 4);
    sqlite3_reset(st);
    sqlite3_bind_blob(st, 1, kv.first.data, 32, SQLITE_STATIC);
    sqlite3_bind_int64(st, 2, static_cast<sqlite3_int64>(vout));
    sqlite3_bind_blob(st, 3, kv.second.key32.data(), 32, SQLITE_STATIC);
    sqlite3_bind_int64(st, 4, static_cast<sqlite3_int64>(kv.second.value_sat));
    if (sqlite3_step(st) != SQLITE_DONE) {
      cerr << "[WARN] sqlite insert: " << sqlite3_errmsg(db) << "\n";
      cleanupFail();
      return false;
    }
  }
  if (!exec("COMMIT;")) {
    cleanupFail();
    return false;
  }
  sqlite3_finalize(st);
  st = nullptr;
  sqlite3_close(db);
  db = nullptr;

  fs::rename(tmp, stateDbPath, ec);
  if (ec) {
    cerr << "[WARN] sqlite rename: " << ec.message() << "\n";
    try {
      fs::remove(tmp);
    } catch (...) {
    }
    return false;
  }
  fs::copy_file(stateDbPath, filterDbPath, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    cerr << "[WARN] sqlite copy to filter dir: " << ec.message() << "\n";
    return false;
  }
  return true;
}
#endif

void cmdParse(int arg_chunk_index, bool debug) {
  string blocksDir = "blocks";
  if (!fs::exists(blocksDir)) {
    cerr << "Directory 'blocks' not found!\n";
    return;
  }

  string outDir = "chunks";
  if (!fs::exists(outDir))
    fs::create_directory(outDir);

  string filterDir = "filter";
  if (!fs::exists(filterDir))
    fs::create_directory(filterDir);

  cout << "Allocating ~16.5 GB for 2 Master Filters in RAM (Thread-safe "
          "Atomics)...\n";
  for (int idx = 0; idx < NUM_FILTERS; ++idx) {
    uint64_t arraySize = 1ull << (filterBitsExp[idx] - tabS);
    preLookups[idx] = new ATTab[arraySize];
    for (uint64_t j = 0; j < arraySize; ++j)
      preLookups[idx][j].store(0, std::memory_order_relaxed);
    string filename =
        filterDir + "/" + to_string(filterSizesMB[idx]) + "mb.bin";
    ifstream filterIn(filename, ios::binary);
    if (filterIn) {
      // Bulk read is safe: atomic<uint64_t> is guaranteed lock-free
      // (static_assert above)
      filterIn.read(reinterpret_cast<char *>(preLookups[idx]),
                    arraySize * sizeof(TTab));
      cout << "  Resumed: loaded existing " << filterSizesMB[idx] << "mb.bin\n";
    }
  }

  if (arg_chunk_index != -1) {
    processChunk(arg_chunk_index, debug);
  } else {
    cout << "Scanning 'blocks' directory to evaluate needed chunks...\n";
    int maxFileIndex = -1;
    for (const auto &entry : fs::directory_iterator(blocksDir)) {
      if (entry.path().extension() == ".dat") {
        string filename = entry.path().filename().string();
        if (filename.length() == 12 && filename.substr(0, 3) == "blk") {
          try {
            int fileIndex = stoi(filename.substr(3, 5));
            maxFileIndex = max(maxFileIndex, fileIndex);
          } catch (...) {
          }
        }
      }
    }

    if (maxFileIndex == -1) {
      cout << "[WARNING] No .dat files found in the 'blocks' directory.\n";
      for (int idx = 0; idx < NUM_FILTERS; ++idx)
        delete[] preLookups[idx];
      return;
    }

    int maxChunk = maxFileIndex / 1000;
    int processed = 0;
    int skipped = 0;

    for (int i = 0; i <= maxChunk; ++i) {
      string outFilename = outDir + "/chunk_" + to_string(i) + ".log";
      bool skipChunk = false;

      if (fs::exists(outFilename)) {
        ifstream logIn(outFilename);
        string firstLine;
        if (getline(logIn, firstLine) && firstLine == "Completed") {
          skipChunk = true;
        }
        logIn.close();
      }

      if (skipChunk) {
        cout << "[SKIP] Chunk " << i << " is completely parsed. Skipping...\n";
        skipped++;
      } else {
        if (!keep_running) {
          cout << "\n[INFO] Gracefully aborting before chunk " << i << "...\n";
          break;
        }
        if (fs::exists(outFilename)) {
          cout << "[REPARSE] Chunk " << i << " was incomplete. Reparsing...\n";
        }
        processChunk(i, debug);
        processed++;
      }
    }
    cout << "\nAll routines finished. Processed: " << processed
         << " Chunks, Skipped: " << skipped << " Chunks.\n";
  }

  for (int idx = 0; idx < NUM_FILTERS; ++idx)
    delete[] preLookups[idx];

  if (!keep_running) {
    cout << "\n[INFO] Skipping balance filter build (parse was interrupted).\n";
  } else {
    cout << "\n[INFO] Balance UTXO artifacts are maintained via chainstate "
            "bootstrap/import only. Run bootstrap_utxo_from_chainstate.sh "
            "(or import_utxo_dump) to refresh balance artifacts.\n";
  }
}

// ==============================================================================
// 4c. Command: import_utxo_dump (bootstrap from chainstate dump CSV)
// ==============================================================================
void cmdImportUtxoDump(const string &csvPath) {
  ifstream in(csvPath);
  if (!in) {
    cerr << "[ERROR] Could not open CSV: " << csvPath << "\n";
    return;
  }

  string blocksDir = "blocks";
  string filterDir = "filter";
  string stateDir = "state";
  if (!fs::exists(filterDir))
    fs::create_directory(filterDir);
  if (!fs::exists(stateDir))
    fs::create_directory(stateDir);

  auto toLowerCopy = [](string s) {
    transform(s.begin(), s.end(), s.begin(),
              [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return s;
  };
  auto trim = [](const string &s) -> string {
    size_t b = 0, e = s.size();
    while (b < e && isspace(static_cast<unsigned char>(s[b])))
      ++b;
    while (e > b && isspace(static_cast<unsigned char>(s[e - 1])))
      --e;
    return s.substr(b, e - b);
  };
  auto splitCsv = [](const string &line) {
    vector<string> out;
    string cur;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
      char c = line[i];
      if (c == '"') {
        if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
          cur.push_back('"');
          ++i;
        } else {
          inQuotes = !inQuotes;
        }
      } else if (c == ',' && !inQuotes) {
        out.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    out.push_back(cur);
    return out;
  };
  auto parseHexStrict = [&](const string &hex, vector<uint8_t> &out) {
    out.clear();
    string h = trim(hex);
    if (h.size() >= 2 && h[0] == '0' && (h[1] == 'x' || h[1] == 'X'))
      h = h.substr(2);
    if (h.size() % 2 != 0)
      return false;
    out.reserve(h.size() / 2);
    for (size_t i = 0; i < h.size(); i += 2) {
      unsigned int b = 0;
      char hi = h[i], lo = h[i + 1];
      if (!isxdigit(static_cast<unsigned char>(hi)) ||
          !isxdigit(static_cast<unsigned char>(lo)))
        return false;
      std::stringstream ss;
      ss << std::hex << h.substr(i, 2);
      ss >> b;
      out.push_back(static_cast<uint8_t>(b & 0xFFu));
    }
    return true;
  };

  string header;
  if (!getline(in, header)) {
    cerr << "[ERROR] Empty CSV.\n";
    return;
  }
  vector<string> cols = splitCsv(header);
  unordered_map<string, size_t> idx;
  for (size_t i = 0; i < cols.size(); ++i)
    idx[toLowerCopy(trim(cols[i]))] = i;

  auto getIdx = [&](const string &name) -> int {
    auto it = idx.find(name);
    return (it == idx.end()) ? -1 : static_cast<int>(it->second);
  };

  const int iTxid = getIdx("txid");
  const int iVout = getIdx("vout");
  const int iAmount = getIdx("amount");
  const int iType = getIdx("type");
  const int iScript = getIdx("script");
  const int iAddress = getIdx("address");

  if (iTxid < 0 || iVout < 0 || iAmount < 0) {
    cerr << "[ERROR] CSV must contain txid,vout,amount columns.\n";
    return;
  }
  if (iType < 0 && iScript < 0 && iAddress < 0) {
    cerr << "[ERROR] CSV needs at least one of type/script/address columns.\n";
    return;
  }

  UtxoMap utxoMap;
  utxoMap.reserve(200000000);
  uint64_t rows = 0, imported = 0, skipped = 0;

  cout << "Importing UTXO dump from " << csvPath << " ...\n";

  string line;
  vector<uint8_t> txidBytes;
  vector<uint8_t> scriptBytes;
  while (keep_running && getline(in, line)) {
    rows++;
    if (line.empty())
      continue;
    vector<string> fields = splitCsv(line);
    auto f = [&](int id) -> string {
      return (id >= 0 && static_cast<size_t>(id) < fields.size()) ? trim(fields[id]) : "";
    };

    string txidHex = f(iTxid);
    string voutStr = f(iVout);
    string amountStr = f(iAmount);
    string type = toLowerCopy(f(iType));
    string scriptHex = f(iScript);
    string address = f(iAddress);

    if (!parseHexStrict(txidHex, txidBytes) || txidBytes.size() != 32) {
      skipped++;
      continue;
    }

    uint64_t amount = 0;
    uint32_t vout = 0;
    try {
      amount = stoull(amountStr);
      unsigned long v = stoul(voutStr);
      if (v > 0xFFFFFFFFul) {
        skipped++;
        continue;
      }
      vout = static_cast<uint32_t>(v);
    } catch (...) {
      skipped++;
      continue;
    }
    if (amount == 0) {
      skipped++;
      continue;
    }

    array<uint8_t, 32> key32{};
    bool ok = false;
    if (iScript >= 0 && parseHexStrict(scriptHex, scriptBytes)) {
      if ((type == "p2pkh" || type == "p2sh" || type == "p2wpkh") && scriptBytes.size() == 20) {
        memcpy(key32.data(), scriptBytes.data(), 20);
        ok = true;
      } else if ((type == "p2wsh" || type == "p2tr") && scriptBytes.size() == 32) {
        memcpy(key32.data(), scriptBytes.data(), 32);
        ok = true;
      } else if (type == "p2pk" && (scriptBytes.size() == 33 || scriptBytes.size() == 65)) {
        auto h = hash160(scriptBytes.data(), scriptBytes.size());
        memcpy(key32.data(), h.data(), 20);
        ok = true;
      } else if (scriptBytes.size() == 25 && scriptBytes[0] == 0x76 && scriptBytes[1] == 0xa9 &&
                 scriptBytes[2] == 0x14 && scriptBytes[23] == 0x88 && scriptBytes[24] == 0xac) {
        memcpy(key32.data(), scriptBytes.data() + 3, 20);
        ok = true;
      } else if (scriptBytes.size() == 23 && scriptBytes[0] == 0xa9 && scriptBytes[1] == 0x14 &&
                 scriptBytes[22] == 0x87) {
        memcpy(key32.data(), scriptBytes.data() + 2, 20);
        ok = true;
      } else if (scriptBytes.size() == 22 && scriptBytes[0] == 0x00 && scriptBytes[1] == 0x14) {
        memcpy(key32.data(), scriptBytes.data() + 2, 20);
        ok = true;
      } else if (scriptBytes.size() == 34 && scriptBytes[0] == 0x51 && scriptBytes[1] == 0x20) {
        memcpy(key32.data(), scriptBytes.data() + 2, 32);
        ok = true;
      } else if (scriptBytes.size() == 34 && scriptBytes[0] == 0x00 && scriptBytes[1] == 0x20) {
        memcpy(key32.data(), scriptBytes.data() + 2, 32);
        ok = true;
      } else if (scriptBytes.size() == 35 && scriptBytes[0] == 0x21 && scriptBytes[34] == 0xac) {
        auto h = hash160(scriptBytes.data() + 1, 33);
        memcpy(key32.data(), h.data(), 20);
        ok = true;
      } else if (scriptBytes.size() == 67 && scriptBytes[0] == 0x41 && scriptBytes[66] == 0xac) {
        auto h = hash160(scriptBytes.data() + 1, 65);
        memcpy(key32.data(), h.data(), 20);
        ok = true;
      }
    }

    if (!ok && iAddress >= 0 && !address.empty()) {
      if (address[0] == '1' || address[0] == '3') {
        vector<uint8_t> decoded = decodeBase58(address);
        if (decoded.size() >= 25) {
          memcpy(key32.data(), decoded.data() + 1, 20);
          ok = true;
        }
      }
    }

    if (!ok) {
      skipped++;
      continue;
    }

    UTXOKey key{};
    memcpy(key.data, txidBytes.data(), 32);
    memcpy(key.data + 32, &vout, 4);
    utxoMap[key] = UtxoMapVal{key32, amount};
    imported++;

    if ((rows % 1000000ull) == 0ull) {
      cout << "  parsed " << rows << " rows, imported " << imported << "\n";
    }
  }

  cout << "CSV read complete: rows=" << rows << " imported=" << imported
       << " skipped=" << skipped << "\n";
  if (utxoMap.empty()) {
    cerr << "[ERROR] No importable UTXO rows found.\n";
    return;
  }

  cout << "Allocating ~16.5 GB for 2 Balance Filters...\n";
  for (int idxB = 0; idxB < NUM_BAL_FILTERS; ++idxB) {
    uint64_t arraySize = 1ull << (balFilterBitsExp[idxB] - tabS);
    balLookups[idxB] = new ATTab[arraySize];
    for (uint64_t j = 0; j < arraySize; ++j)
      balLookups[idxB][j].store(0, std::memory_order_relaxed);
  }

  cout << "Populating balance filters from imported UTXO map...\n";
  for (auto &[key, ent] : utxoMap) {
    TKey kBuf;
    memcpy(kBuf, ent.key32.data(), 32);
    uint64_t fH = SNVByte<0x00000100000001B3ull>(kBuf, 0xCBF29CE484222325ull);
    for (int fidx = 0; fidx < NUM_BAL_FILTERS; ++fidx) {
      uint64_t h = fH & ((1ull << balFilterBitsExp[fidx]) - 1);
      balLookups[fidx][h >> tabS].fetch_or(((TTab)1 << (h & tabM)),
                                           std::memory_order_relaxed);
    }
  }
  for (int idxB = 0; idxB < NUM_BAL_FILTERS; ++idxB) {
    string filename = filterDir + "/" + to_string(balFilterSizesMB[idxB]) + "mb_bal.bin";
    uint64_t arraySize = 1ull << (balFilterBitsExp[idxB] - tabS);
    ofstream out(filename, ios::binary | ios::trunc);
    out.write(reinterpret_cast<const char *>(balLookups[idxB]),
              arraySize * sizeof(TTab));
    delete[] balLookups[idxB];
    balLookups[idxB] = nullptr;
    cout << "  Saved " << filename << "\n";
  }

#if BTCSM_HAVE_SQLITE3
  if (exportBalanceUtxoSqlite(stateDir, filterDir, utxoMap))
    cout << "  Wrote " << (filterDir + "/balance_utxo.sqlite")
         << " (" << utxoMap.size() << " rows).\n";
  else
    cerr << "[WARN] Could not write balance_utxo.sqlite\n";
#else
  cout << "[INFO] sqlite3 header/lib not available: skipping balance_utxo.sqlite export.\n";
#endif

  vector<string> datFiles;
  if (fs::exists(blocksDir)) {
    for (const auto &entry : fs::directory_iterator(blocksDir)) {
      if (entry.path().extension() == ".dat") {
        string fn = entry.path().filename().string();
        if (fn.length() == 12 && fn.substr(0, 3) == "blk") {
          try {
            stoi(fn.substr(3, 5));
            datFiles.push_back(entry.path().string());
          } catch (...) {
          }
        }
      }
    }
  }
  sort(datFiles.begin(), datFiles.end());
  vector<pair<string, uint64_t>> manifest;
  buildDatFileManifest(datFiles, manifest);

  try {
    fs::remove(stateDir + "/balance_utxo_progress.chk");
    fs::remove(stateDir + "/balance_utxo_progress.chk.tmp");
    fs::remove(stateDir + "/balance_utxo_prefix.chk");
    fs::remove(stateDir + "/balance_utxo_prefix.chk.tmp");
  } catch (...) {
  }

  const string tipPath = stateDir + "/balance_utxo_tip.chk";
  if (!writeUtxoStateV2(stateDir + "/balance_utxo_tip.chk.tmp", tipPath, manifest,
                        utxoMap, static_cast<uint32_t>(datFiles.size()))) {
    cerr << "[WARN] Could not write " << tipPath << "\n";
  } else {
    cout << "  Wrote " << tipPath << " (next_file_index=" << datFiles.size() << ")\n";
  }

  cout << "UTXO bootstrap import complete.\n";
}

// ==============================================================================
// 5. Command: Build
// ==============================================================================
void cmdBuild() {
  cout << "[INFO] The 'build' step is no longer necessary!\n";
  cout << "       The 'parse' command now natively injects into the completely "
          "dynamic,\n";
  cout << "       two-tier (512MB + 16GB) filter files in filter/.\n";
  cout << "       Look inside your 'filter/' folder!\n";
}

// ==============================================================================
// 6. Command: Test (Uses the multi-tier filters via O(1) Direct Streaming)
// ==============================================================================
void cmdTest(const string &input) {
  vector<uint8_t> payload;

  // Is it a 40-char Hex String? (Hash160)
  if (input.length() == 40) {
    payload = hexToBytes(input);
    cout << "Detected 20-byte Hash160 (Hex format).\n";
  }
  // Is it a Base58 Address? (P2PKH -> starts with 1, P2SH -> starts with 3)
  else if (input[0] == '1' || input[0] == '3') {
    vector<uint8_t> decoded = decodeBase58(input);
    if (decoded.size() < 25) {
      cerr << "[ERROR] Invalid Base58 address length.\n";
      return;
    }
    // Base58Check has 1 byte version + 20 bytes hash + 4 bytes checksum
    payload.assign(decoded.begin() + 1, decoded.begin() + 21);
    cout << "Detected Base58 Address. Extracted 20-byte payload: "
         << toHex(payload) << "\n";
  }
  // Is it a raw Public Key? (33 bytes compressed = 66 hex chars, 65 bytes
  // uncompressed = 130 hex chars)
  else if (input.length() == 66 || input.length() == 130) {
    cerr << "[INFO] This script expects 20-byte Hash160s (Addresses).\n";
    cerr << "Your input appears to be a raw Public Key. You must compute "
            "SHA256 -> RIPEMD160 \n";
    cerr << "on this Public Key first to get the 20-byte address payload "
            "before testing it.\n";
    return;
  } else {
    cerr << "[ERROR] Unrecognized format. Please pass a Base58 Address (1...) "
            "or a 40-char Hex (Hash160).\n";
    return;
  }

  TKey keyBuffer = {};
  for (int i = 0; i < 20 && i < (int)payload.size(); ++i)
    keyBuffer[i] = payload[i];
  uint64_t fullHash =
      SNVByte<0x00000100000001B3ull>(keyBuffer, 0xCBF29CE484222325ull);

  const int targetNUM = NUM_FILTERS;
  const int *targetSizes = filterSizesMB;

  cout << "\n--- All-addresses filters ---\n";

  for (int idx = 0; idx < targetNUM; ++idx) {
    string filterFilename = "filter/" + to_string(targetSizes[idx]) + "mb.bin";
    ifstream file(filterFilename, ios::binary);
    if (!file)
      continue;

    uint64_t targetBitsExp = filterBitsExp[idx];
    uint64_t h_trunc = fullHash & ((1ull << targetBitsExp) - 1);
    uint64_t byteOffset = (h_trunc >> tabS) * sizeof(TTab);

    file.seekg(byteOffset);
    TTab chunk;
    if (file.read(reinterpret_cast<char *>(&chunk), sizeof(TTab))) {
      bool found = (chunk & ((TTab)1 << (h_trunc & tabM))) != 0;
      cout << setw(5) << filterSizesMB[idx]
           << " MB : " << (found ? "YES" : "NO") << "\n";
    }
    file.close();
  }

  cout << "\n--- Balance filters (has unspent BTC) ---\n";

  for (int idx = 0; idx < NUM_BAL_FILTERS; ++idx) {
    string filterFilename = "filter/" + to_string(balFilterSizesMB[idx]) + "mb_bal.bin";
    ifstream file(filterFilename, ios::binary);
    if (!file)
      continue;

    uint64_t targetBitsExp = balFilterBitsExp[idx];
    uint64_t h_trunc = fullHash & ((1ull << targetBitsExp) - 1);
    uint64_t byteOffset = (h_trunc >> tabS) * sizeof(TTab);

    file.seekg(byteOffset);
    TTab chunk;
    if (file.read(reinterpret_cast<char *>(&chunk), sizeof(TTab))) {
      bool found = (chunk & ((TTab)1 << (h_trunc & tabM))) != 0;
      cout << setw(5) << balFilterSizesMB[idx]
           << " MB : " << (found ? "YES" : "NO") << "\n";
    }
    file.close();
  }
  cout << "----------------------------------------\n";
}

// ==============================================================================
// 7. Command: Count
// ==============================================================================

struct FileCounts {
  uint64_t blocks             = 0;
  uint64_t transactions       = 0;
  uint64_t inputs             = 0;
  uint64_t outputs            = 0;
  uint64_t outputs_zero_val   = 0;
  uint64_t p2pkh              = 0;
  uint64_t p2sh               = 0;
  uint64_t p2wpkh             = 0;
  uint64_t p2wsh              = 0;
  uint64_t p2pk_compressed    = 0;
  uint64_t p2pk_uncompressed  = 0;
  uint64_t p2tr               = 0;
  uint64_t op_return          = 0;
  uint64_t other_output       = 0;
  uint64_t segwit_tx          = 0;
  uint64_t coinbase_tx        = 0;
  uint64_t inputs_with_pubkey = 0;
  uint64_t total_satoshis     = 0;
};

using Hash160 = array<uint8_t, 20>;
static const uint64_t COUNT_MAGIC = 0x544E554F43435442ULL;
static const uint32_t COUNT_VER   = 2;

bool saveCountCp(const string &path, const FileCounts &fc,
                 const vector<Hash160> &hashes) {
  string tmp = path + ".tmp";
  ofstream f(tmp, ios::binary | ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char *>(&COUNT_MAGIC), 8);
  f.write(reinterpret_cast<const char *>(&COUNT_VER), 4);
  f.write(reinterpret_cast<const char *>(&fc), sizeof(fc));
  uint64_t n = hashes.size();
  f.write(reinterpret_cast<const char *>(&n), 8);
  if (n) f.write(reinterpret_cast<const char *>(hashes.data()), n * 20);
  f.close();
  if (!f.good()) { try { fs::remove(tmp); } catch (...) {} return false; }
  fs::rename(tmp, path);
  return true;
}

bool loadCountCp(const string &path, FileCounts &fc,
                 vector<Hash160> &hashes) {
  ifstream f(path, ios::binary);
  if (!f) return false;
  uint64_t magic; uint32_t ver;
  f.read(reinterpret_cast<char *>(&magic), 8);
  f.read(reinterpret_cast<char *>(&ver), 4);
  if (magic != COUNT_MAGIC || ver != COUNT_VER) return false;
  f.read(reinterpret_cast<char *>(&fc), sizeof(fc));
  uint64_t n;
  f.read(reinterpret_cast<char *>(&n), 8);
  hashes.resize(n);
  if (n) f.read(reinterpret_cast<char *>(hashes.data()), n * 20);
  return f.good();
}

FileCounts processCountFile(const string &filepath, vector<Hash160> &hashes) {
  FileCounts fc;
  ifstream file(filepath, ios::binary);
  if (!file) return fc;

  uint32_t magic, blockSize;
  while (file.read(reinterpret_cast<char *>(&magic), 4)) {
    if (magic != 0xD9B4BEF9) { file.seekg(-3, ios::cur); continue; }
    if (!file.read(reinterpret_cast<char *>(&blockSize), 4)) break;
    auto blockStart = file.tellg();
    file.seekg(80, ios::cur);
    fc.blocks++;

    uint64_t txCount = readVarInt(file);
    fc.transactions += txCount;

    for (uint64_t ti = 0; ti < txCount; ++ti) {
      uint32_t version;
      file.read(reinterpret_cast<char *>(&version), 4);

      bool isSW = false;
      {
        auto cPos = file.tellg();
        uint8_t m[2];
        if (file.read(reinterpret_cast<char *>(m), 2)) {
          if (m[0] == 0x00 && m[1] == 0x01) isSW = true;
          else file.seekg(cPos);
        }
      }
      if (isSW) fc.segwit_tx++;

      uint64_t inC = readVarInt(file);
      fc.inputs += inC;
      if (ti == 0) fc.coinbase_tx++;

      for (uint64_t i = 0; i < inC; ++i) {
        file.seekg(36, ios::cur);
        uint64_t sLen = readVarInt(file);
        // For non-coinbase inputs: check if scriptSig ends with a compressed
        // pubkey push (P2PKH spend pattern: ... 0x21 <02/03 + 32 bytes>)
        if (ti != 0 && sLen >= 34) {
          vector<uint8_t> sc(sLen);
          file.read(reinterpret_cast<char *>(sc.data()), sLen);
          size_t off = sLen - 34;
          if (sc[off] == 0x21 && (sc[off+1] == 0x02 || sc[off+1] == 0x03))
            fc.inputs_with_pubkey++;
        } else {
          file.seekg(sLen, ios::cur);
        }
        file.seekg(4, ios::cur); // sequence
      }

      uint64_t outC = readVarInt(file);
      fc.outputs += outC;

      for (uint64_t i = 0; i < outC; ++i) {
        uint64_t val;
        file.read(reinterpret_cast<char *>(&val), 8);
        uint64_t sLen = readVarInt(file);
        vector<uint8_t> sc(sLen);
        file.read(reinterpret_cast<char *>(sc.data()), sLen);

        fc.total_satoshis += val;
        if (val == 0) fc.outputs_zero_val++;

        Hash160 h;
        if (sLen == 25 && sc[0]==0x76 && sc[1]==0xa9 && sc[2]==0x14 &&
            sc[23]==0x88 && sc[24]==0xac) {
          fc.p2pkh++;
          memcpy(h.data(), sc.data()+3, 20); hashes.push_back(h);
        } else if (sLen == 23 && sc[0]==0xa9 && sc[1]==0x14 && sc[22]==0x87) {
          fc.p2sh++;
          memcpy(h.data(), sc.data()+2, 20); hashes.push_back(h);
        } else if (sLen == 22 && sc[0]==0x00 && sc[1]==0x14) {
          fc.p2wpkh++;
          memcpy(h.data(), sc.data()+2, 20); hashes.push_back(h);
        } else if (sLen == 34 && sc[0]==0x00 && sc[1]==0x20) {
          fc.p2wsh++;
        } else if (sLen == 35 && sc[0]==0x21 && sc[34]==0xac) {
          fc.p2pk_compressed++;
        } else if (sLen == 67 && sc[0]==0x41 && sc[66]==0xac) {
          fc.p2pk_uncompressed++;
        } else if (sLen == 34 && sc[0]==0x51 && sc[1]==0x20) {
          fc.p2tr++;
        } else if (sLen >= 1 && sc[0]==0x6a) {
          fc.op_return++;
        } else {
          fc.other_output++;
        }
      }

      if (isSW) {
        for (uint64_t i = 0; i < inC; ++i) {
          uint64_t wItems = readVarInt(file);
          for (uint64_t w = 0; w < wItems; ++w) {
            uint64_t iLen = readVarInt(file);
            file.seekg(iLen, ios::cur);
          }
        }
      }
      file.seekg(4, ios::cur); // locktime
    }
    file.seekg(blockStart + static_cast<streamoff>(blockSize));
  }

  sort(hashes.begin(), hashes.end());
  return fc;
}

static string fmtNum(uint64_t n) {
  string s = to_string(n);
  for (int i = (int)s.size() - 3; i > 0; i -= 3)
    s.insert(s.begin() + i, ',');
  return s;
}

static string fmtBTC(uint64_t sat) {
  uint64_t whole = sat / 100000000ULL;
  uint64_t frac  = sat % 100000000ULL;
  string fs = to_string(frac);
  while (fs.size() < 8) fs = "0" + fs;
  return fmtNum(whole) + "." + fs + " BTC";
}

void cmdCount() {
  string blocksDir = "blocks";
  string countDir  = "counts";

  if (!fs::exists(blocksDir)) { cerr << "'blocks' directory not found\n"; return; }
  if (!fs::exists(countDir)) fs::create_directory(countDir);

  vector<string> datFiles;
  for (const auto &entry : fs::directory_iterator(blocksDir)) {
    if (entry.path().extension() == ".dat") {
      string fn = entry.path().filename().string();
      if (fn.length() == 12 && fn.substr(0, 3) == "blk")
        datFiles.push_back(entry.path().string());
    }
  }
  sort(datFiles.begin(), datFiles.end());

  cout << "Found " << datFiles.size() << " .dat files.\n";
  cout << "Note: checkpoint files in counts/ may use up to ~25 GB of disk.\n\n";

  unsigned int numThreads = thread::hardware_concurrency();
  if (!numThreads) numThreads = 4;
  cout << "Scanning with " << numThreads << " threads...\n";

  // Per-file scalar results (indexed by file, no lock needed)
  vector<FileCounts> fileCounts(datFiles.size());

  // Global hash pool for unique counting (~30 GB peak, pre-reserved as virtual memory)
  vector<Hash160> allHashes;
  try { allHashes.reserve(1500000000ULL); }
  catch (...) { cout << "[WARN] Could not pre-reserve hash buffer — may reallocate.\n"; }

  atomic<size_t> fileIdx(0), nParsed(0), nCached(0);
  mutex hashesMtx, coutMtx;

  vector<thread> threads;
  for (unsigned int t = 0; t < numThreads; ++t) {
    threads.emplace_back([&]() {
      while (true) {
        size_t idx = fileIdx.fetch_add(1);
        if (idx >= datFiles.size()) break;

        const string &fp = datFiles[idx];
        string fn        = fs::path(fp).filename().string();
        string cpPath    = countDir + "/" + fn + ".bin";

        FileCounts fc;
        vector<Hash160> localHashes;

        if (loadCountCp(cpPath, fc, localHashes)) {
          nCached.fetch_add(1);
        } else {
          fc = processCountFile(fp, localHashes);
          saveCountCp(cpPath, fc, localHashes);
          nParsed.fetch_add(1);
          lock_guard<mutex> lk(coutMtx);
          cout << "  parsed " << fn << "\n";
        }

        fileCounts[idx] = fc;
        {
          lock_guard<mutex> lk(hashesMtx);
          allHashes.insert(allHashes.end(),
                           localHashes.begin(), localHashes.end());
        }
      }
    });
  }
  for (auto &t : threads) t.join();

  cout << "\nParsed: " << nParsed << "  Cached: " << nCached << "\n";
  cout << "Aggregating...\n";

  FileCounts total;
  for (const auto &fc : fileCounts) {
    total.blocks             += fc.blocks;
    total.transactions       += fc.transactions;
    total.inputs             += fc.inputs;
    total.outputs            += fc.outputs;
    total.outputs_zero_val   += fc.outputs_zero_val;
    total.p2pkh              += fc.p2pkh;
    total.p2sh               += fc.p2sh;
    total.p2wpkh             += fc.p2wpkh;
    total.p2wsh              += fc.p2wsh;
    total.p2pk_compressed    += fc.p2pk_compressed;
    total.p2pk_uncompressed  += fc.p2pk_uncompressed;
    total.p2tr               += fc.p2tr;
    total.op_return          += fc.op_return;
    total.other_output       += fc.other_output;
    total.segwit_tx          += fc.segwit_tx;
    total.coinbase_tx        += fc.coinbase_tx;
    total.inputs_with_pubkey += fc.inputs_with_pubkey;
    total.total_satoshis     += fc.total_satoshis;
  }

  cout << "Sorting " << fmtNum(allHashes.size()) << " address occurrences...\n";
  sort(allHashes.begin(), allHashes.end());

  uint64_t uniqueAddr = allHashes.empty() ? 0 : 1;
  for (size_t i = 1; i < allHashes.size(); ++i)
    if (allHashes[i] != allHashes[i-1]) uniqueAddr++;
  allHashes.clear();

  cout << "\n";
  cout << "=================================================================\n";
  cout << "  Bitcoin Blockchain Count\n";
  cout << "=================================================================\n";
  cout << "  Blocks:                       " << fmtNum(total.blocks)             << "\n";
  cout << "  Transactions:                 " << fmtNum(total.transactions)       << "\n";
  cout << "    Coinbase:                   " << fmtNum(total.coinbase_tx)        << "\n";
  cout << "    SegWit:                     " << fmtNum(total.segwit_tx)          << "\n";
  cout << "\n";
  cout << "  Inputs:                       " << fmtNum(total.inputs)             << "\n";
  cout << "    w/ visible pubkey:          " << fmtNum(total.inputs_with_pubkey) << "\n";
  cout << "\n";
  cout << "  Outputs:                      " << fmtNum(total.outputs)            << "\n";
  cout << "    zero-value:                 " << fmtNum(total.outputs_zero_val)   << "\n";
  cout << "    P2PKH:                      " << fmtNum(total.p2pkh)              << "\n";
  cout << "    P2SH:                       " << fmtNum(total.p2sh)               << "\n";
  cout << "    P2WPKH:                     " << fmtNum(total.p2wpkh)             << "\n";
  cout << "    P2WSH:                      " << fmtNum(total.p2wsh)              << "\n";
  cout << "    P2PK compressed:            " << fmtNum(total.p2pk_compressed)    << "\n";
  cout << "    P2PK uncompressed:          " << fmtNum(total.p2pk_uncompressed)  << "\n";
  cout << "    P2TR (Taproot):             " << fmtNum(total.p2tr)               << "\n";
  cout << "    OP_RETURN:                  " << fmtNum(total.op_return)          << "\n";
  cout << "    Other:                      " << fmtNum(total.other_output)       << "\n";
  cout << "\n";
  cout << "  Unique addr (P2PKH+P2SH+P2WPKH): " << fmtNum(uniqueAddr)          << "\n";
  cout << "\n";
  cout << "  Total output value:           " << fmtBTC(total.total_satoshis)    << "\n";
  cout << "=================================================================\n";
}

// ==============================================================================
// CLI Parser
// ==============================================================================
void printHelp() {
  cout << "BTC Slot Machine - Preprocessing Tool\n";
  cout << "======================================\n\n";
  cout << "Commands:\n";
  cout << "  download                 Downloads initial blk*.dat files from "
          "the web.\n";
  cout << "  parse <index> [--debug]  Parses a 1000-block chunk and extracts "
          "Hash160 payloads.\n";
  cout << "                           The --debug flag will print every parsed "
          "address.\n";
  cout << "  build                    Compresses all chunks into the final "
          "'final_filter_table.bin'.\n";
  cout << "  count                    Scans all blk*.dat files, counts address\n";
  cout << "                           types, unique addresses, transactions, BTC\n";
  cout << "                           moved. Saves per-file checkpoints to counts/.\n";
  cout << "  import_utxo_dump <csv>   Imports a chainstate UTXO CSV dump (e.g.\n";
  cout << "                           bitcoin-utxo-dump output) into balance UTXO\n";
  cout << "                           state + balance filters + sqlite.\n";
  cout << "  test <address_or_hash>   Loads filter/*.bin tiers and verifies an "
          "address.\n";
  cout << "                           Accepts: Base58 Address (1A1zP...) or "
          "40-char Hex string.\n";
  cout << "\nExamples:\n";
  cout << "  ./main parse 0 --debug\n";
  cout << "  ./main import_utxo_dump state/utxodump.csv\n";
  cout << "  ./main build\n";
  cout << "  ./main test 1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa\n";
}

int main(int argc, char *argv[]) {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  if (argc < 2) {
    printHelp();
    return 1;
  }

  string command = argv[1];

  if (command == "download") {
    cmdDownload();
  } else if (command == "parse") {
    int chunk_index = -1; // Default to all
    bool debug = false;

    for (int i = 2; i < argc; ++i) {
      string arg = argv[i];
      if (arg == "--debug")
        debug = true;
      else {
        try {
          chunk_index = stoi(arg);
        } catch (...) {
        }
      }
    }

    cmdParse(chunk_index, debug);
  } else if (command == "build") {
    cmdBuild();
  } else if (command == "count") {
    cmdCount();
  } else if (command == "import_utxo_dump") {
    if (argc < 3) {
      cerr << "Missing CSV path. Usage: ./main import_utxo_dump <csv_path>\n";
      return 1;
    }
    cmdImportUtxoDump(string(argv[2]));
  } else if (command == "test") {
    if (argc < 3) {
      cerr << "Missing address or hash element to test.\n";
      return 1;
    }
    cmdTest(string(argv[2]));
  } else if (command == "help" || command == "--help" || command == "-h") {
    printHelp();
  } else {
    cerr << "Unknown command: " << command << "\n";
    printHelp();
  }

  return 0;
}
