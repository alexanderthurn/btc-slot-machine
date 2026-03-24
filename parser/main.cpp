#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
const int NUM_FILTERS = 7;
const int filterSizesMB[] = {32, 64, 128, 256, 512, 1024, 2048};
const int filterBitsExp[] = {28, 29, 30, 31, 32, 33, 34};

typedef std::atomic<uint64_t> ATTab;
static_assert(
    ATTab::is_always_lock_free,
    "std::atomic<uint64_t> must be lock-free for safe bulk binary I/O");
ATTab *preLookups[NUM_FILTERS];

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
  uint64_t count_p2pkh = 0, count_p2sh = 0, count_p2wpkh = 0;

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

        uint64_t lTotal = 0, lP2PKH = 0, lP2SH = 0, lP2WPKH = 0;
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

              if (sLen == 25 && script[0] == 0x76 && script[1] == 0xa9 &&
                  script[2] == 0x14 && script[23] == 0x88 &&
                  script[24] == 0xac) {
                TKey kBuf;
                memcpy(kBuf, script.data() + 3, 20);
                uint64_t fH =
                    SNVByte<0x00000100000001B3ull>(kBuf, 0xCBF29CE484222325ull);
                for (int f = 0; f < NUM_FILTERS; ++f) {
                  uint64_t h = fH & ((1ull << filterBitsExp[f]) - 1);
                  preLookups[f][h >> tabS].fetch_or(((TTab)1 << (h & tabM)),
                                                    std::memory_order_relaxed);
                }
                lP2PKH++;
                lTotal++;
                if (debug && !dbg_p2pkh) {
                  std::lock_guard<std::mutex> lock(coutMtx);
                  cout << "[DEBUG] " << filename << " | P2PKH:  "
                       << toHex(vector<uint8_t>(kBuf, kBuf + 20)) << "\n";
                  dbg_p2pkh = true;
                }
              } else if (sLen == 23 && script[0] == 0xa9 && script[1] == 0x14 &&
                         script[22] == 0x87) {
                TKey kBuf;
                memcpy(kBuf, script.data() + 2, 20);
                uint64_t fH =
                    SNVByte<0x00000100000001B3ull>(kBuf, 0xCBF29CE484222325ull);
                for (int f = 0; f < NUM_FILTERS; ++f) {
                  uint64_t h = fH & ((1ull << filterBitsExp[f]) - 1);
                  preLookups[f][h >> tabS].fetch_or(((TTab)1 << (h & tabM)),
                                                    std::memory_order_relaxed);
                }
                lP2SH++;
                lTotal++;
                if (debug && !dbg_p2sh) {
                  std::lock_guard<std::mutex> lock(coutMtx);
                  cout << "[DEBUG] " << filename << " | P2SH:   "
                       << toHex(vector<uint8_t>(kBuf, kBuf + 20)) << "\n";
                  dbg_p2sh = true;
                }
              } else if (sLen == 22 && script[0] == 0x00 && script[1] == 0x14) {
                TKey kBuf;
                memcpy(kBuf, script.data() + 2, 20);
                uint64_t fH =
                    SNVByte<0x00000100000001B3ull>(kBuf, 0xCBF29CE484222325ull);
                for (int f = 0; f < NUM_FILTERS; ++f) {
                  uint64_t h = fH & ((1ull << filterBitsExp[f]) - 1);
                  preLookups[f][h >> tabS].fetch_or(((TTab)1 << (h & tabM)),
                                                    std::memory_order_relaxed);
                }
                lP2WPKH++;
                lTotal++;
                if (debug && !dbg_p2wpkh) {
                  std::lock_guard<std::mutex> lock(coutMtx);
                  cout << "[DEBUG] " << filename << " | P2WPKH: "
                       << toHex(vector<uint8_t>(kBuf, kBuf + 20)) << "\n";
                  dbg_p2wpkh = true;
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
  cout << "Saving all 7 filters to disk (total ~4 GB)...\n";

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
  if (datFiles.size() == 1000) {
    logOut << "Completed\n";
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
  logOut.close();

  cout << "Extracted a total of " << total_payloads_written
       << " addresses (20-byte chunks)\n";
  cout << "  - P2PKH:  " << count_p2pkh << "\n";
  cout << "  - P2SH:   " << count_p2sh << "\n";
  cout << "  - P2WPKH: " << count_p2wpkh << "\n";
}

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

  cout << "Allocating ~4 GB for all 7 Master Filters in RAM (Thread-safe "
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
        if (getline(logIn, firstLine) &&
            firstLine.find("Completed") != string::npos) {
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
}

// ==============================================================================
// 5. Command: Build
// ==============================================================================
void cmdBuild() {
  cout << "[INFO] The 'build' step is no longer necessary!\n";
  cout << "       The 'parse' command now natively injects into the completely "
          "dynamic,\n";
  cout << "       multi-tiered 2^n (32MB, 64MB ... 2048MB) filter array "
          "system.\n";
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

  TKey keyBuffer;
  for (int i = 0; i < 20; ++i)
    keyBuffer[i] = payload[i];
  uint64_t fullHash =
      SNVByte<0x00000100000001B3ull>(keyBuffer, 0xCBF29CE484222325ull);

  const int targetNUM = NUM_FILTERS;
  const int *targetSizes = filterSizesMB;

  cout << "\n----------------------------------------\n";

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
      cout << setw(4) << filterSizesMB[idx]
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
  cout << "  test <address_or_hash>   Loads the 512MB filter into RAM and "
          "verifies an address.\n";
  cout << "                           Accepts: Base58 Address (1A1zP...) or "
          "40-char Hex string.\n";
  cout << "\nExamples:\n";
  cout << "  ./main parse 0 --debug\n";
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
