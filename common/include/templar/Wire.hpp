#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace templar::proto {

using Bytes = std::vector<uint8_t>;

// Serializador binario big-endian para el contenido de un payload (dentro de un Frame).
class Writer {
 public:
  void u8(uint8_t v) { buf_.push_back(v); }

  void u32(uint32_t v) {
    buf_.push_back(static_cast<uint8_t>(v >> 24));
    buf_.push_back(static_cast<uint8_t>(v >> 16));
    buf_.push_back(static_cast<uint8_t>(v >> 8));
    buf_.push_back(static_cast<uint8_t>(v));
  }

  void u64(uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8) {
      buf_.push_back(static_cast<uint8_t>(v >> shift));
    }
  }

  // Blob con prefijo de longitud (u32 + bytes). Sirve tanto para strings como
  // para claves/blobs binarios (claves públicas, firmas, ciphertexts...).
  void blob(const void* data, size_t len) {
    u32(static_cast<uint32_t>(len));
    const uint8_t* p = static_cast<const uint8_t*>(data);
    buf_.insert(buf_.end(), p, p + len);
  }

  void blob(const Bytes& b) { blob(b.data(), b.size()); }
  void str(const std::string& s) { blob(s.data(), s.size()); }

  const Bytes& bytes() const { return buf_; }
  Bytes take() { return std::move(buf_); }

 private:
  Bytes buf_;
};

// Deserializador correspondiente. Lanza std::runtime_error si el buffer
// termina antes de lo esperado (payload corrupto o forjado).
class Reader {
 public:
  explicit Reader(const Bytes& b) : data_(b.data()), len_(b.size()) {}
  Reader(const uint8_t* data, size_t len) : data_(data), len_(len) {}

  uint8_t u8() {
    need(1);
    return data_[pos_++];
  }

  uint32_t u32() {
    need(4);
    uint32_t v = (uint32_t(data_[pos_]) << 24) | (uint32_t(data_[pos_ + 1]) << 16) |
                 (uint32_t(data_[pos_ + 2]) << 8) | uint32_t(data_[pos_ + 3]);
    pos_ += 4;
    return v;
  }

  uint64_t u64() {
    need(8);
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | data_[pos_ + i];
    pos_ += 8;
    return v;
  }

  Bytes blob() {
    uint32_t len = u32();
    need(len);
    Bytes out(data_ + pos_, data_ + pos_ + len);
    pos_ += len;
    return out;
  }

  std::string str() {
    Bytes b = blob();
    return std::string(b.begin(), b.end());
  }

  bool empty() const { return pos_ >= len_; }

 private:
  void need(size_t n) const {
    if (pos_ + n > len_) {
      throw std::runtime_error("Wire::Reader: buffer truncado (payload corrupto)");
    }
  }

  const uint8_t* data_;
  size_t len_;
  size_t pos_ = 0;
};

}  // namespace templar::proto
