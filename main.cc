#include "video_downloader.h"
#include <iostream>

void printUsage()
{
  std::cout << "Usage:" << std::endl
            << "1. Download from URL: " << std::endl
            << "   video-downloader" << std::endl
            << "2. Load from local M3U8 file: " << std::endl
            << "   video-downloader -f <m3u8_file_path>" << std::endl;
}

int main(int argc, char *argv[])
{
  VideoDownloader downloader;
  if (!downloader.loadConfig("config.json"))
  {
    std::cerr << "Failed to load config" << std::endl;
    return 1;
  }

  const auto &config = downloader.getConfig();
  bool success;

  if (argc == 1)
  {
    // No arguments - use URL from config
    success = downloader.downloadM3U8(config.url, config.output_name);
  }
  else if (argc == 3 && std::string(argv[1]) == "-f")
  {
    // Load from local file
    success = downloader.loadM3U8FromFile(argv[2], config.output_name);
  }
  else
  {
    printUsage();
    return 1;
  }

  if (!success)
  {
    std::cerr << "Failed to download video" << std::endl;
    return 1;
  }

  std::cout << "Download completed successfully" << std::endl;
  return 0;
}
