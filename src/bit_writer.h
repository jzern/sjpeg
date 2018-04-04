// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//  Utility for writing bits
//
// Author: Skal (pascal.massimino@gmail.com)

#ifndef SJPEG_BIT_WRITER_H_
#define SJPEG_BIT_WRITER_H_

#include <assert.h>
#include <stdint.h>
#include <string.h>   // for memcpy
#include <string>

#include "sjpeg.h"

namespace sjpeg {

////////////////////////////////////////////////////////////////////////////////
// Generic byte-sink

// Protocol:
//  . Commit(used_size, extra_size): specify that 'used_size' bytes were
//       used since the last call to Commit(). Also reserve 'extra_size' bytes
//       for the next cycle. 'extra_size' can be 0. Most of the time (except
//       during header writing), 'extra_size' will be less than 2048.
//  . Finalize(): indicates that calls to Commit() are finished until the
//       destruction (and the assembled byte-stream can be grabbed).
//  . Reset(): releases memory (called in case of error or at destruction).

struct ByteSink {
 public:
  virtual ~ByteSink() {}
  // returns nullptr in case of error:
  virtual uint8_t* Commit(size_t used_size, size_t extra_size) = 0;
  virtual bool Finalize() = 0;            // returns false in case of error
  virtual void Reset() = 0;               // called in case of error
};

///////////////////////////////////////////////////////////////////////////////
// Memory-Sink

class MemorySink : public ByteSink {
 public:
  explicit MemorySink(size_t expected_size);
  virtual ~MemorySink();
  virtual uint8_t* Commit(size_t used_size, size_t extra_size);
  virtual bool Finalize() { /* nothing to do */ return true; }
  virtual void Reset();
  void Release(uint8_t** buf_ptr, size_t* size_ptr);

 private:
  uint8_t* buf_;
  size_t pos_, max_pos_;
};

///////////////////////////////////////////////////////////////////////////////
// String-Sink

class StringSink : public ByteSink {
 public:
  explicit StringSink(std::string* output) : str_(output), pos_(0) {}
  virtual ~StringSink() {}
  virtual uint8_t* Commit(size_t used_size, size_t extra_size);
  virtual bool Finalize() { str_->resize(pos_); return true; }
  virtual void Reset() { str_->clear(); }

 private:
  std::string* const str_;
  size_t pos_;
};

///////////////////////////////////////////////////////////////////////////////
// BitWriter

class BitWriter {
 public:
  explicit BitWriter(ByteSink* const sink);

  // Verifies the that output buffer can store at least 'size' more bytes,
  // growing if needed. Returns a buffer pointer to 'size' writable bytes.
  // The returned pointer is likely to change if Reserve() is called again.
  // Hence it should be used as quickly as possible.
  bool Reserve(size_t size) {
    buf_ = sink_->Commit(byte_pos_, size);
    byte_pos_ = 0;
    return (buf_ != nullptr);
  }

  // Make sure we can write 24 bits by flushing the past ones.
  // WARNING! There's no check for buffer overwrite. Use Reserve() before
  // calling this function.
  void FlushBits() {
    // worst case: 3 escaped codes = 6 bytes
    while (nb_bits_ >= 8) {
      const uint8_t tmp = bits_ >> 24;
      buf_[byte_pos_++] = tmp;
      if (tmp == 0xff) {   // escaping
        buf_[byte_pos_++] = 0x00;
      }
      bits_ <<= 8;
      nb_bits_ -= 8;
    }
  }
  // Writes the sequence 'bits' of length 'nb_bits' (less than 24).
  // WARNING! There's no check for buffer overwrite. Use Reserve() before
  // calling this function.
  void PutBits(uint32_t bits, int nb) {
    assert(nb <= 24 && nb > 0);
    assert((bits & ~((1 << nb) - 1)) == 0);
    FlushBits();    // make room for a least 24bits
    nb_bits_+= nb;
    bits_ |= bits << (32 - nb_bits_);
  }
  // Append one byte to buffer. FlushBits() must have been called before.
  // WARNING! There's no check for buffer overwrite. Use Reserve() before
  // calling this function.
  // Also: no 0xff escaping is performed by this function.
  void PutByte(uint8_t value) {
    assert(nb_bits_ == 0);
    buf_[byte_pos_++] = value;
  }
  // Same as multiply calling PutByte().
  void PutBytes(const uint8_t* buf, size_t size) {
    assert(nb_bits_ == 0);
    assert(buf != NULL);
    assert(size > 0);
    memcpy(buf_ + byte_pos_, buf, size);
    byte_pos_ += size;
  }

  // Handy helper to write a packed code in one call.
  void PutPackedCode(uint32_t code) { PutBits(code >> 16, code & 0xff); }

  // Write pending bits, and align bitstream with extra '1' bits.
  void Flush();

  // To be called last.
  bool Finalize() { return Reserve(0) && sink_->Finalize(); }

 private:
  ByteSink* sink_;

  int nb_bits_;      // number of unwritten bits
  uint32_t bits_;    // accumulator for unwritten bits
  size_t byte_pos_;  // write position, in bytes
  uint8_t* buf_;     // destination buffer (don't access directly!)
};

// Class for counting bits, including the 0xff escape
struct BitCounter {
  BitCounter() : bits_(0), bit_pos_(0), size_(0) {}

  void AddPackedCode(const uint32_t code) { AddBits(code >> 16, code & 0xff); }
  void AddBits(const uint32_t bits, size_t nbits);
  size_t Size() const { return size_; }

 private:
  uint32_t bits_;
  size_t bit_pos_;
  size_t size_;
};

}   // namespace sjpeg

#endif    // SJPEG_BIT_WRITER_H_
