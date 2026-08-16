#include "templar/Protocol.hpp"

#include <stdexcept>

namespace templar::proto {

Bytes encodeFrame(MsgType type, const Bytes& payload) {
  uint32_t innerLen = static_cast<uint32_t>(payload.size() + 1);  // +1 por el byte de tipo

  Bytes out;
  out.reserve(4 + innerLen);
  out.push_back(static_cast<uint8_t>(innerLen >> 24));
  out.push_back(static_cast<uint8_t>(innerLen >> 16));
  out.push_back(static_cast<uint8_t>(innerLen >> 8));
  out.push_back(static_cast<uint8_t>(innerLen));
  out.push_back(static_cast<uint8_t>(type));
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

void FrameParser::feed(const uint8_t* data, size_t len) {
  buf_.insert(buf_.end(), data, data + len);
}

std::optional<Frame> FrameParser::next() {
  if (buf_.size() < 4) return std::nullopt;

  uint32_t innerLen = (uint32_t(buf_[0]) << 24) | (uint32_t(buf_[1]) << 16) |
                       (uint32_t(buf_[2]) << 8) | uint32_t(buf_[3]);

  if (innerLen == 0 || innerLen > kMaxFrameSize) {
    throw std::runtime_error(
        "FrameParser: longitud de frame invalida (posible corrupcion o intento de DoS)");
  }

  if (buf_.size() < 4 + innerLen) return std::nullopt;  // aun no llego completo

  Frame f;
  f.type = static_cast<MsgType>(buf_[4]);
  f.payload.assign(buf_.begin() + 5, buf_.begin() + 4 + innerLen);

  buf_.erase(buf_.begin(), buf_.begin() + 4 + innerLen);
  return f;
}

}  // namespace templar::proto
