#include "MessagePayload.hpp"

#include <stdexcept>

namespace templar::client {

using templar::proto::Reader;
using templar::proto::Writer;

Bytes MessagePayload::encodeText(const std::string& text) {
  Writer w;
  w.u8(static_cast<uint8_t>(PayloadKind::Text));
  w.str(text);
  return w.take();
}

Bytes MessagePayload::encodeTextReply(const std::string& text, const std::string& replyToSender,
                                      const std::string& replyToText) {
  Writer w;
  w.u8(static_cast<uint8_t>(PayloadKind::TextReply));
  w.str(text);
  w.str(replyToSender);
  w.str(replyToText);
  return w.take();
}

Bytes MessagePayload::encodeFileBlobPointer(const std::string& blobId, const std::string& filename,
                                            uint64_t fileSize, const Bytes& fileKey,
                                            const Bytes& fileHeader) {
  Writer w;
  w.u8(static_cast<uint8_t>(PayloadKind::FileBlobPointer));
  w.str(blobId);
  w.str(filename);
  w.u64(fileSize);
  w.blob(fileKey);
  w.blob(fileHeader);
  return w.take();
}

DecodedPayload MessagePayload::decode(const Bytes& payload) {
  if (payload.empty()) {
    throw std::runtime_error("MessagePayload: payload vacio");
  }

  Reader r(payload);
  auto kind = static_cast<PayloadKind>(r.u8());

  DecodedPayload out;
  out.kind = kind;
  switch (kind) {
    case PayloadKind::Text:
      out.text = r.str();
      break;
    case PayloadKind::TextReply:
      out.text = r.str();
      out.replyToSender = r.str();
      out.replyToText = r.str();
      break;
    case PayloadKind::FileMeta:
      out.filename = r.str();
      out.fileSize = r.u64();
      break;
    case PayloadKind::FileChunk:
      out.chunk = r.blob();
      break;
    case PayloadKind::FileEnd:
      out.sha256Hex = r.str();
      break;
    case PayloadKind::FileBlobPointer:
      out.blobId = r.str();
      out.filename = r.str();
      out.fileSize = r.u64();
      out.fileKey = r.blob();
      out.fileHeader = r.blob();
      break;
    default:
      throw std::runtime_error("MessagePayload: tipo de payload desconocido (mensaje corrupto?)");
  }
  return out;
}

}  // namespace templar::client
