#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main() {
  std::vector<std::string> files = {"blk00000.dat", "blk00021.dat",
                                    "blk05000.dat"};

  std::string baseUrl = "http://alexander-thurn.de/temp/blocks/";
  std::string targetDir = "blocks";

  // Ensure the directory exists (requires C++17)
  if (!std::filesystem::exists(targetDir)) {
    std::filesystem::create_directory(targetDir);
  }

  std::cout << "Starting download of block files..." << std::endl;

  for (const auto &file : files) {
    std::cout << "Downloading " << file << "..." << std::endl;

    std::string url = baseUrl + file;
    std::string targetPath = targetDir + "/" + file;

    // Since standard C++ has no native HTTP functions, we use 'curl'.
    // curl is pre-installed on macOS, Linux, and Windows 10/11 by default,
    // making it the simplest cross-platform solution without third-party libs.
    // -L: follow redirects, -f: fail on HTTP errors, -o: output, -s: silent
    std::string command = "curl -L -f -s -o " + targetPath + " " + url;

    int result = std::system(command.c_str());

    if (result == 0) {
      std::cout << "Successfully downloaded: " << file << std::endl;
    } else {
      std::cerr << "Error downloading " << file
                << " (Curl Exit Code: " << result << ")" << std::endl;
    }
  }

  std::cout << "All downloads processed." << std::endl;
  return 0;
}
