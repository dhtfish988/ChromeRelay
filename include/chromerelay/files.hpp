#pragma once
#include <array>
#include <chromerelay/dom.hpp>
#include <cstdint>
#include <memory>
#include <span>
namespace chromerelay {
class OpenFile {
public:
  explicit OpenFile(int value = -1) : value_(value) {}
  ~OpenFile();
  OpenFile(OpenFile &&other) noexcept;
  OpenFile &operator=(OpenFile &&other) noexcept;
  OpenFile(const OpenFile &) = delete;
  OpenFile &operator=(const OpenFile &) = delete;
  int get() const { return value_; }

private:
  int value_;
};
struct UploadFile {
  std::filesystem::path path;
  std::uint64_t device, inode, size;
  std::array<std::int64_t, 4> times;
  bool directory = false;
  OpenFile handle;
};
class ImageDestination {
public:
  ImageDestination(std::filesystem::path path, OpenFile parent)
      : path_(std::move(path)), parent_(std::move(parent)) {}
  std::string commit(std::span<const std::uint8_t> bytes);

private:
  std::filesystem::path path_;
  OpenFile parent_;
};
class PathAccess {
public:
  explicit PathAccess(std::vector<std::filesystem::path> roots = defaults());
  static std::vector<std::filesystem::path> defaults();
  UploadFile upload(const std::string &path,
                    bool allow_directory = false) const;
  std::vector<UploadFile> directory_files(const UploadFile &folder) const;
  void verify(const UploadFile &file) const;
  ImageDestination output(const std::string &path) const;

private:
  std::filesystem::path resolve(const std::string &path) const;
  OpenFile parent(const std::filesystem::path &path, bool create) const;
  struct Root {
    std::filesystem::path path;
    OpenFile descriptor;
  };
  std::vector<Root> roots_;
};
struct PngPayload {
  std::vector<std::uint8_t> bytes;
  unsigned width, height;
};
PngPayload decode_png(const std::string &base64);
class FileActions {
public:
  FileActions(BrowserWorkspace &browser, PathAccess &paths,
              Milliseconds timeout)
      : browser_(browser), paths_(paths), clock_(timeout) {}
  Json upload(const Json &arguments);
  Json screenshot(const Json &arguments);

private:
  BrowserWorkspace &browser_;
  PathAccess &paths_;
  ActionClock clock_;
};
} // namespace chromerelay
