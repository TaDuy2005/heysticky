// HeySticky shim: CrossPoint's HalStorage API on the Arduino SD library (HeySticky mounts the card in card.h).
// Same interface as crosspoint-reader lib/hal/HalStorage.h, minus USB drive and the SdFat types.
#pragma once

#include <Arduino.h>
#include <fcntl.h>

#include <memory>
#include <string>

typedef int oflag_t;

class HalFile;

class HalStorage {
 public:
  bool begin() { return true; }  // HeySticky mounts the card itself
  String readFile(const char* path);
  bool readFileToStream(const char* path, Print& out, size_t chunkSize = 256);
  bool readFileToString(const char* moduleName, const std::string& path, size_t cap, std::string& out);
  bool writeFile(const char* path, const String& content);
  bool ensureDirectoryExists(const char* path);

  HalFile open(const char* path, const oflag_t oflag = O_RDONLY);
  bool mkdir(const char* path, const bool pFlag = true);
  bool exists(const char* path);
  bool remove(const char* path);
  bool rename(const char* oldPath, const char* newPath);
  bool replaceFile(const char* tmpPath, const char* path);
  bool rmdir(const char* path);

  bool openFileForRead(const char* moduleName, const char* path, HalFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const char* path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const String& path, HalFile& file);
  bool removeDir(const char* path);

  static HalStorage& getInstance() { return instance; }

 private:
  static HalStorage instance;
};

#define Storage HalStorage::getInstance()

class HalFile : public Print {
  friend class HalStorage;

 public:
  class Impl;
  explicit HalFile(std::unique_ptr<Impl> impl);
  HalFile();
  ~HalFile();
  HalFile(HalFile&&);
  HalFile& operator=(HalFile&&);
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  void flush();
  size_t getName(char* name, size_t len);
  size_t size();
  size_t fileSize();
  uint64_t fileSize64();
  uint32_t modificationTime();
  bool seek(size_t pos);
  bool seek64(uint64_t pos);
  bool seekCur(int64_t offset);
  bool seekSet(size_t offset);
  bool truncate(uint64_t length);
  int available() const;
  size_t position() const;
  int read(void* buf, size_t count);
  int read();
  size_t write(const uint8_t* buf, size_t count) override;
  size_t write(const void* buf, size_t count);
  size_t write(uint8_t b) override;
  bool isDirectory() const;
  void rewindDirectory();
  bool close();
  HalFile openNextFile();
  bool isOpen() const;
  operator bool() const;

 private:
  std::unique_ptr<Impl> impl;
};
