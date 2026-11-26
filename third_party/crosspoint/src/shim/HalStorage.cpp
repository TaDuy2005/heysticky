// HeySticky shim: HalStorage / HalFile on the Arduino SD library.
#include "HalStorage.h"

#include <SD.h>

HalStorage HalStorage::instance;

class HalFile::Impl {
 public:
  explicit Impl(fs::File f) : file(std::move(f)) {}
  fs::File file;
};

HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> i) : impl(std::move(i)) {}
HalFile::~HalFile() { close(); }
HalFile::HalFile(HalFile&&) = default;
HalFile& HalFile::operator=(HalFile&& o) {
  if (this != &o) {
    close();
    impl = std::move(o.impl);
  }
  return *this;
}

static HalFile wrap(fs::File f) { return f ? HalFile(std::make_unique<HalFile::Impl>(std::move(f))) : HalFile(); }

// oflag -> Arduino mode. "w+" keeps the file readable while it is written (Section builds read pages back).
static const char* mode_of(oflag_t f) {
  if (f & O_APPEND) return FILE_APPEND;
  if (f & O_TRUNC) return "w+";
  if ((f & O_ACCMODE) == O_RDWR) return (f & O_CREAT) ? "w+" : "r+";
  if ((f & O_ACCMODE) == O_WRONLY) return FILE_WRITE;
  return FILE_READ;
}

HalFile HalStorage::open(const char* path, const oflag_t oflag) { return wrap(SD.open(path, mode_of(oflag))); }
bool HalStorage::exists(const char* path) { return SD.exists(path); }
bool HalStorage::remove(const char* path) { return SD.remove(path); }
bool HalStorage::rename(const char* a, const char* b) { return SD.rename(a, b); }
bool HalStorage::rmdir(const char* path) { return SD.rmdir(path); }

bool HalStorage::mkdir(const char* path, const bool pFlag) {
  if (SD.exists(path)) return true;
  if (!pFlag) return SD.mkdir(path);
  String p(path), sub;
  int from = 1;
  while (from > 0) {  // create every missing parent
    const int slash = p.indexOf('/', from);
    sub = slash < 0 ? p : p.substring(0, slash);
    if (sub.length() && !SD.exists(sub) && !SD.mkdir(sub)) return false;
    from = slash < 0 ? -1 : slash + 1;
  }
  return true;
}

bool HalStorage::ensureDirectoryExists(const char* path) { return mkdir(path, true); }

bool HalStorage::replaceFile(const char* tmpPath, const char* path) {
  if (SD.exists(path) && !SD.remove(path)) return false;
  return SD.rename(tmpPath, path);
}

bool HalStorage::removeDir(const char* path) {
  fs::File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return false;
  for (fs::File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const String child = String(path) + (String(path).endsWith("/") ? "" : "/") + f.name();
    const bool isDir = f.isDirectory();
    f.close();
    if (isDir ? !removeDir(child.c_str()) : !SD.remove(child)) return false;
  }
  dir.close();
  return SD.rmdir(path);
}

bool HalStorage::openFileForRead(const char*, const char* path, HalFile& file) {
  fs::File f = SD.open(path, FILE_READ);
  if (!f || f.isDirectory()) {
    file = HalFile();
    return false;
  }
  file = wrap(std::move(f));
  return true;
}
bool HalStorage::openFileForRead(const char* m, const std::string& p, HalFile& f) { return openFileForRead(m, p.c_str(), f); }
bool HalStorage::openFileForWrite(const char*, const char* path, HalFile& file) {
  file = wrap(SD.open(path, "w+"));
  return bool(file);
}
bool HalStorage::openFileForWrite(const char* m, const std::string& p, HalFile& f) { return openFileForWrite(m, p.c_str(), f); }
bool HalStorage::openFileForWrite(const char* m, const String& p, HalFile& f) { return openFileForWrite(m, p.c_str(), f); }

String HalStorage::readFile(const char* path) {
  fs::File f = SD.open(path, FILE_READ);
  if (!f || f.size() > 50000) return String();
  String s;
  s.reserve(f.size());
  while (f.available()) s += char(f.read());
  return s;
}

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  fs::File f = SD.open(path, FILE_READ);
  if (!f) return false;
  uint8_t buf[256];
  const size_t n = chunkSize && chunkSize < sizeof(buf) ? chunkSize : sizeof(buf);
  for (int got; (got = f.read(buf, n)) > 0;) out.write(buf, got);
  return true;
}

bool HalStorage::readFileToString(const char*, const std::string& path, size_t cap, std::string& out) {
  fs::File f = SD.open(path.c_str(), FILE_READ);
  if (!f || f.isDirectory() || f.size() == 0 || f.size() > cap) return false;
  out.resize(f.size());
  return f.read(reinterpret_cast<uint8_t*>(&out[0]), out.size()) == out.size();
}

bool HalStorage::writeFile(const char* path, const String& content) {
  fs::File f = SD.open(path, FILE_WRITE);
  return f && f.print(content) == content.length();
}

// ---------- HalFile ----------
#define F_ (impl->file)
void HalFile::flush() { if (impl) F_.flush(); }
size_t HalFile::getName(char* name, size_t len) {
  if (!impl || !len) return 0;
  const char* n = F_.name();
  const size_t l = strlen(n);
  if (l >= len) { name[0] = 0; return 0; }
  memcpy(name, n, l + 1);
  return l;
}
size_t HalFile::size() { return impl ? F_.size() : 0; }
size_t HalFile::fileSize() { return size(); }
uint64_t HalFile::fileSize64() { return size(); }
uint32_t HalFile::modificationTime() { return impl ? uint32_t(F_.getLastWrite()) : 0; }
bool HalFile::seek(size_t pos) { return impl && F_.seek(pos, fs::SeekSet); }
bool HalFile::seek64(uint64_t pos) { return seek(size_t(pos)); }
bool HalFile::seekSet(size_t pos) { return seek(pos); }
bool HalFile::seekCur(int64_t off) { return impl && F_.seek(int32_t(off), fs::SeekCur); }
bool HalFile::truncate(uint64_t length) {
  // ponytail: Arduino FS has no truncate; only shrinking to 0 is needed by callers, done by reopening "w+".
  if (!impl || length != 0) return false;
  const String p = F_.path();
  F_.close();
  F_ = SD.open(p, "w+");
  return bool(F_);
}
int HalFile::available() const { return impl ? impl->file.available() : 0; }
size_t HalFile::position() const { return impl ? impl->file.position() : 0; }
int HalFile::read(void* buf, size_t count) { return impl ? F_.read(static_cast<uint8_t*>(buf), count) : -1; }
int HalFile::read() { return impl ? F_.read() : -1; }
size_t HalFile::write(const uint8_t* buf, size_t count) { return impl ? F_.write(buf, count) : 0; }
size_t HalFile::write(const void* buf, size_t count) { return write(static_cast<const uint8_t*>(buf), count); }
size_t HalFile::write(uint8_t b) { return impl ? F_.write(b) : 0; }
bool HalFile::isDirectory() const { return impl && impl->file.isDirectory(); }
void HalFile::rewindDirectory() { if (impl) F_.rewindDirectory(); }
bool HalFile::close() {
  if (impl) F_.close();
  impl.reset();
  return true;
}
HalFile HalFile::openNextFile() { return impl ? wrap(F_.openNextFile()) : HalFile(); }
bool HalFile::isOpen() const { return impl && bool(impl->file); }
HalFile::operator bool() const { return isOpen(); }
#undef F_
