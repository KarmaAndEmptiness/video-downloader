#include "video_downloader.h"
#include <iostream>

void printUsage()
{
  std::cout << "Usage:" << std::endl
            << "1. Download and merge: " << std::endl
            << "   video-downloader" << std::endl
            << "2. Load from local M3U8 file and merge: " << std::endl
            << "   video-downloader -f <m3u8_file_path>" << std::endl
            << "3. Download only: " << std::endl
            << "   video-downloader --download-only" << std::endl
            << "4. Download only from local M3U8: " << std::endl
            << "   video-downloader --download-only -f <m3u8_file_path>" << std::endl
            << "5. Merge only: " << std::endl
            << "   video-downloader --merge-only" << std::endl;
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
  bool success = false;

  if (argc == 1)
  {
    // 原有的完整下载和合并流程
    success = downloader.downloadM3U8(config.url, config.output_name);
  }
  else if (argc == 2 && std::string(argv[1]) == "--download-only")
  {
    // 仅下载
    success = downloader.downloadOnly(config.url);
  }
  else if (argc == 2 && std::string(argv[1]) == "--merge-only")
  {
    // 仅合并
    success = downloader.mergeOnly(config.output_name);
  }
  else if (argc == 3 && std::string(argv[1]) == "-f")
  {
    // 从本地文件完整处理
    success = downloader.loadM3U8FromFile(argv[2], config.output_name);
  }
  else if (argc == 4 && std::string(argv[1]) == "--download-only" && std::string(argv[2]) == "-f")
  {
    // 从本地文件仅下载
    success = downloader.downloadOnly(argv[3], true);
  }
  else
  {
    printUsage();
    return 1;
  }

  if (!success)
  {
    std::cerr << "Operation failed" << std::endl;
    return 1;
  }

  std::cout << "Operation completed successfully" << std::endl;
  return 0;
}
