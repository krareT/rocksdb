//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#ifndef ROCKSDB_LITE

#include "rocksdb/env_encryption.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <iostream>

#include "env/env_encryption_ctr.h"
#include "monitoring/perf_context_imp.h"
#include "rocksdb/convenience.h"
#include "util/aligned_buffer.h"
#include "util/coding.h"
#include "util/random.h"
#include "util/string_util.h"

#endif

namespace ROCKSDB_NAMESPACE {

#ifndef ROCKSDB_LITE
static constexpr char kROT13CipherName[] = "ROT13";
static constexpr char kCTRProviderName[] = "CTR";

Status BlockCipher::CreateFromString(const ConfigOptions& /*config_options*/,
                                     const std::string& value,
                                     std::shared_ptr<BlockCipher>* result) {
  std::string id = value;
  size_t colon = value.find(':');
  if (colon != std::string::npos) {
    id = value.substr(0, colon);
  }
  if (id == kROT13CipherName) {
    if (colon != std::string::npos) {
      size_t block_size = ParseSizeT(value.substr(colon + 1));
      result->reset(new ROT13BlockCipher(block_size));
    } else {
      result->reset(new ROT13BlockCipher(32));
    }
    return Status::OK();
  } else {
    return Status::NotSupported("Could not find cipher ", value);
  }
}

Status EncryptionProvider::CreateFromString(
    const ConfigOptions& /*config_options*/, const std::string& value,
    std::shared_ptr<EncryptionProvider>* result) {
  std::string id = value;
  bool is_test = StartsWith(value, "test://");
  Status status = Status::OK();
  if (is_test) {
    id = value.substr(strlen("test://"));
  }
  if (id == kCTRProviderName) {
    result->reset(new CTREncryptionProvider());
  } else if (is_test) {
    result->reset(new CTREncryptionProvider());
  } else {
    return Status::NotSupported("Could not find provider ", value);
  }
  if (status.ok() && is_test) {
    status = result->get()->TEST_Initialize();
  }
  return status;
}

std::shared_ptr<EncryptionProvider> EncryptionProvider::NewCTRProvider(
    const std::shared_ptr<BlockCipher>& cipher) {
  return std::make_shared<CTREncryptionProvider>(cipher);
}

  // Read up to "n" bytes from the file.  "scratch[0..n-1]" may be
  // written by this routine.  Sets "*result" to the data that was
  // read (including if fewer than "n" bytes were successfully read).
  // May set "*result" to point at data in "scratch[0..n-1]", so
  // "scratch[0..n-1]" must be live when "*result" is used.
  // If an error was encountered, returns a non-OK status.
  //
  // REQUIRES: External synchronization
Status EncryptedSequentialFile::Read(size_t n, Slice* result, char* scratch) {
  assert(scratch);
  Status status = file_->Read(n, result, scratch);
  if (!status.ok()) {
    return status;
  }
  {
    PERF_TIMER_GUARD(decrypt_data_nanos);
    status = stream_->Decrypt(offset_, (char*)result->data(), result->size());
  }
  offset_ += result->size();  // We've already ready data from disk, so update
                              // offset_ even if decryption fails.
  return status;
}

// Skip "n" bytes from the file. This is guaranteed to be no
// slower that reading the same data, but may be faster.
//
// If end of file is reached, skipping will stop at the end of the
// file, and Skip will return OK.
//
// REQUIRES: External synchronization
Status EncryptedSequentialFile::Skip(uint64_t n) {
  auto status = file_->Skip(n);
  if (!status.ok()) {
    return status;
  }
  offset_ += n;
  return status;
}

// Indicates the upper layers if the current SequentialFile implementation
// uses direct IO.
bool EncryptedSequentialFile::use_direct_io() const {
  return file_->use_direct_io();
}

// Use the returned alignment value to allocate
// aligned buffer for Direct I/O
size_t EncryptedSequentialFile::GetRequiredBufferAlignment() const {
  return file_->GetRequiredBufferAlignment();
}

  // Remove any kind of caching of data from the offset to offset+length
  // of this file. If the length is 0, then it refers to the end of file.
  // If the system is not caching the file contents, then this is a noop.
Status EncryptedSequentialFile::InvalidateCache(size_t offset, size_t length) {
  return file_->InvalidateCache(offset + prefixLength_, length);
}

  // Positioned Read for direct I/O
  // If Direct I/O enabled, offset, n, and scratch should be properly aligned
Status EncryptedSequentialFile::PositionedRead(uint64_t offset, size_t n,
                                               Slice* result, char* scratch) {
  assert(scratch);
  offset += prefixLength_;  // Skip prefix
  auto status = file_->PositionedRead(offset, n, result, scratch);
  if (!status.ok()) {
    return status;
  }
  offset_ = offset + result->size();
  {
    PERF_TIMER_GUARD(decrypt_data_nanos);
    status = stream_->Decrypt(offset, (char*)result->data(), result->size());
  }
  return status;
}

  // Read up to "n" bytes from the file starting at "offset".
  // "scratch[0..n-1]" may be written by this routine.  Sets "*result"
  // to the data that was read (including if fewer than "n" bytes were
  // successfully read).  May set "*result" to point at data in
  // "scratch[0..n-1]", so "scratch[0..n-1]" must be live when
  // "*result" is used.  If an error was encountered, returns a non-OK
  // status.
  //
  // Safe for concurrent use by multiple threads.
  // If Direct I/O enabled, offset, n, and scratch should be aligned properly.
Status EncryptedRandomAccessFile::Read(uint64_t offset, size_t n, Slice* result,
                                       char* scratch) const {
  assert(scratch);
  offset += prefixLength_;
  auto status = file_->Read(offset, n, result, scratch);
  if (!status.ok()) {
    return status;
  }
  {
    PERF_TIMER_GUARD(decrypt_data_nanos);
    status = stream_->Decrypt(offset, (char*)result->data(), result->size());
  }
  return status;
}

  // Readahead the file starting from offset by n bytes for caching.
Status EncryptedRandomAccessFile::Prefetch(uint64_t offset, size_t n) {
  // return Status::OK();
  return file_->Prefetch(offset + prefixLength_, n);
}

  // Tries to get an unique ID for this file that will be the same each time
  // the file is opened (and will stay the same while the file is open).
  // Furthermore, it tries to make this ID at most "max_size" bytes. If such an
  // ID can be created this function returns the length of the ID and places it
  // in "id"; otherwise, this function returns 0, in which case "id"
  // may not have been modified.
  //
  // This function guarantees, for IDs from a given environment, two unique ids
  // cannot be made equal to each other by adding arbitrary bytes to one of
  // them. That is, no unique ID is the prefix of another.
  //
  // This function guarantees that the returned ID will not be interpretable as
  // a single varint.
  //
  // Note: these IDs are only valid for the duration of the process.
size_t EncryptedRandomAccessFile::GetUniqueId(char* id, size_t max_size) const {
  return file_->GetUniqueId(id, max_size);
};

void EncryptedRandomAccessFile::Hint(AccessPattern pattern) {
  file_->Hint(pattern);
}

  // Indicates the upper layers if the current RandomAccessFile implementation
  // uses direct IO.
bool EncryptedRandomAccessFile::use_direct_io() const {
  return file_->use_direct_io();
}

  // Use the returned alignment value to allocate
  // aligned buffer for Direct I/O
size_t EncryptedRandomAccessFile::GetRequiredBufferAlignment() const {
  return file_->GetRequiredBufferAlignment();
}

  // Remove any kind of caching of data from the offset to offset+length
  // of this file. If the length is 0, then it refers to the end of file.
  // If the system is not caching the file contents, then this is a noop.
Status EncryptedRandomAccessFile::InvalidateCache(size_t offset,
                                                  size_t length) {
  return file_->InvalidateCache(offset + prefixLength_, length);
}
intptr_t EncryptedRandomAccessFile::FileDescriptor() const {
  return file_->FileDescriptor();
}

// A file abstraction for sequential writing.  The implementation
// must provide buffering since callers may append small fragments
// at a time to the file.
Status EncryptedWritableFile::Append(const Slice& data) {
  AlignedBuffer buf;
  Status status;
  Slice dataToAppend(data);
  if (data.size() > 0) {
    auto offset = file_->GetFileSize();  // size including prefix
    // Encrypt in cloned buffer
    buf.Alignment(GetRequiredBufferAlignment());
    buf.AllocateNewBuffer(data.size());
    // TODO (sagar0): Modify AlignedBuffer.Append to allow doing a memmove
    // so that the next two lines can be replaced with buf.Append().
    memmove(buf.BufferStart(), data.data(), data.size());
    buf.Size(data.size());
    {
      PERF_TIMER_GUARD(encrypt_data_nanos);
      status = stream_->Encrypt(offset, buf.BufferStart(), buf.CurrentSize());
    }
    if (!status.ok()) {
      return status;
    }
    dataToAppend = Slice(buf.BufferStart(), buf.CurrentSize());
  }
  status = file_->Append(dataToAppend);
  if (!status.ok()) {
    return status;
  }
  return status;
}

Status EncryptedWritableFile::PositionedAppend(const Slice& data,
                                               uint64_t offset) {
  AlignedBuffer buf;
  Status status;
  Slice dataToAppend(data);
  offset += prefixLength_;
  if (data.size() > 0) {
    // Encrypt in cloned buffer
    buf.Alignment(GetRequiredBufferAlignment());
    buf.AllocateNewBuffer(data.size());
    memmove(buf.BufferStart(), data.data(), data.size());
    buf.Size(data.size());
    {
      PERF_TIMER_GUARD(encrypt_data_nanos);
      status = stream_->Encrypt(offset, buf.BufferStart(), buf.CurrentSize());
    }
    if (!status.ok()) {
      return status;
    }
    dataToAppend = Slice(buf.BufferStart(), buf.CurrentSize());
  }
  status = file_->PositionedAppend(dataToAppend, offset);
  if (!status.ok()) {
    return status;
  }
  return status;
}

  // Indicates the upper layers if the current WritableFile implementation
  // uses direct IO.
bool EncryptedWritableFile::use_direct_io() const {
  return file_->use_direct_io();
}

  // Use the returned alignment value to allocate
  // aligned buffer for Direct I/O
size_t EncryptedWritableFile::GetRequiredBufferAlignment() const {
  return file_->GetRequiredBufferAlignment();
}

/*
 * Get the size of valid data in the file.
 */
uint64_t EncryptedWritableFile::GetFileSize() {
  return file_->GetFileSize() - prefixLength_;
}

  // Truncate is necessary to trim the file to the correct size
  // before closing. It is not always possible to keep track of the file
  // size due to whole pages writes. The behavior is undefined if called
  // with other writes to follow.
Status EncryptedWritableFile::Truncate(uint64_t size) {
  return file_->Truncate(size + prefixLength_);
}

    // Remove any kind of caching of data from the offset to offset+length
  // of this file. If the length is 0, then it refers to the end of file.
  // If the system is not caching the file contents, then this is a noop.
  // This call has no effect on dirty pages in the cache.
Status EncryptedWritableFile::InvalidateCache(size_t offset, size_t length) {
  return file_->InvalidateCache(offset + prefixLength_, length);
}

  // Sync a file range with disk.
  // offset is the starting byte of the file range to be synchronized.
  // nbytes specifies the length of the range to be synchronized.
  // This asks the OS to initiate flushing the cached data to disk,
  // without waiting for completion.
  // Default implementation does nothing.
Status EncryptedWritableFile::RangeSync(uint64_t offset, uint64_t nbytes) {
  return file_->RangeSync(offset + prefixLength_, nbytes);
}

  // PrepareWrite performs any necessary preparation for a write
  // before the write actually occurs.  This allows for pre-allocation
  // of space on devices where it can result in less file
  // fragmentation and/or less waste from over-zealous filesystem
  // pre-allocation.
void EncryptedWritableFile::PrepareWrite(size_t offset, size_t len) {
  file_->PrepareWrite(offset + prefixLength_, len);
}

  // Pre-allocates space for a file.
Status EncryptedWritableFile::Allocate(uint64_t offset, uint64_t len) {
  return file_->Allocate(offset + prefixLength_, len);
}

// A file abstraction for random reading and writing.

// Indicates if the class makes use of direct I/O
// If false you must pass aligned buffer to Write()
bool EncryptedRandomRWFile::use_direct_io() const {
  return file_->use_direct_io();
}

  // Use the returned alignment value to allocate
  // aligned buffer for Direct I/O
size_t EncryptedRandomRWFile::GetRequiredBufferAlignment() const {
  return file_->GetRequiredBufferAlignment();
}

  // Write bytes in `data` at  offset `offset`, Returns Status::OK() on success.
  // Pass aligned buffer when use_direct_io() returns true.
Status EncryptedRandomRWFile::Write(uint64_t offset, const Slice& data) {
  AlignedBuffer buf;
  Status status;
  Slice dataToWrite(data);
  offset += prefixLength_;
  if (data.size() > 0) {
    // Encrypt in cloned buffer
    buf.Alignment(GetRequiredBufferAlignment());
    buf.AllocateNewBuffer(data.size());
    memmove(buf.BufferStart(), data.data(), data.size());
    buf.Size(data.size());
    {
      PERF_TIMER_GUARD(encrypt_data_nanos);
      status = stream_->Encrypt(offset, buf.BufferStart(), buf.CurrentSize());
    }
    if (!status.ok()) {
      return status;
    }
    dataToWrite = Slice(buf.BufferStart(), buf.CurrentSize());
  }
  status = file_->Write(offset, dataToWrite);
  return status;
}

  // Read up to `n` bytes starting from offset `offset` and store them in
  // result, provided `scratch` size should be at least `n`.
  // Returns Status::OK() on success.
Status EncryptedRandomRWFile::Read(uint64_t offset, size_t n, Slice* result,
                                   char* scratch) const {
  assert(scratch);
  offset += prefixLength_;
  auto status = file_->Read(offset, n, result, scratch);
  if (!status.ok()) {
    return status;
  }
  {
    PERF_TIMER_GUARD(decrypt_data_nanos);
    status = stream_->Decrypt(offset, (char*)result->data(), result->size());
  }
  return status;
}

Status EncryptedRandomRWFile::Flush() { return file_->Flush(); }

Status EncryptedRandomRWFile::Sync() { return file_->Sync(); }

Status EncryptedRandomRWFile::Fsync() { return file_->Fsync(); }

Status EncryptedRandomRWFile::Close() { return file_->Close(); }

// EncryptedEnv implements an Env wrapper that adds encryption to files stored
// on disk.
class EncryptedEnvImpl : public EnvWrapper {
  // Returns the raw encryption provider that should be used to write the input
  // encrypted file.  If there is no such provider, NotFound is returned.
  Status GetWritableProvider(const std::string& /*fname*/,
                             EncryptionProvider** result) {
    if (provider_) {
      *result = provider_.get();
      return Status::OK();
    } else {
      *result = nullptr;
      return Status::NotFound("No WriteProvider specified");
    }
  }

  // Returns the raw encryption provider that should be used to read the input
  // encrypted file.  If there is no such provider, NotFound is returned.
  Status GetReadableProvider(const std::string& /*fname*/,
                             EncryptionProvider** result) {
    if (provider_) {
      *result = provider_.get();
      return Status::OK();
    } else {
      *result = nullptr;
      return Status::NotFound("No Provider specified");
    }
  }

  // Creates a CipherStream for the underlying file/name using the options
  // If a writable provider is found and encryption is enabled, uses
  // this provider to create a cipher stream.
  // @param fname         Name of the writable file
  // @param underlying    The underlying "raw" file
  // @param options       Options for creating the file/cipher
  // @param prefix_length Returns the length of the encryption prefix used for
  // this file
  // @param stream        Returns the cipher stream to use for this file if it
  // should be encrypted
  // @return OK on success, non-OK on failure.
  template <class TypeFile>
  Status CreateWritableCipherStream(
      const std::string& fname, const std::unique_ptr<TypeFile>& underlying,
      const EnvOptions& options, size_t* prefix_length,
      std::unique_ptr<BlockAccessCipherStream>* stream) {
    EncryptionProvider* provider = nullptr;
    *prefix_length = 0;
    Status status = GetWritableProvider(fname, &provider);
    if (!status.ok()) {
      return status;
    } else if (provider != nullptr) {
      // Initialize & write prefix (if needed)
      AlignedBuffer buffer;
      Slice prefix;
      *prefix_length = provider->GetPrefixLength();
      if (*prefix_length > 0) {
        // Initialize prefix
        buffer.Alignment(underlying->GetRequiredBufferAlignment());
        buffer.AllocateNewBuffer(*prefix_length);
        status = provider->CreateNewPrefix(fname, buffer.BufferStart(),
                                           *prefix_length);
        if (status.ok()) {
          buffer.Size(*prefix_length);
          prefix = Slice(buffer.BufferStart(), buffer.CurrentSize());
          // Write prefix
          status = underlying->Append(prefix);
        }
        if (!status.ok()) {
          return status;
        }
      }
      // Create cipher stream
      status = provider->CreateCipherStream(fname, options, prefix, stream);
    }
    return status;
  }

  template <class TypeFile>
  Status CreateWritableEncryptedFile(const std::string& fname,
                                     std::unique_ptr<TypeFile>& underlying,
                                     const EnvOptions& options,
                                     std::unique_ptr<TypeFile>* result) {
    // Create cipher stream
    std::unique_ptr<BlockAccessCipherStream> stream;
    size_t prefix_length;
    Status status = CreateWritableCipherStream(fname, underlying, options,
                                               &prefix_length, &stream);
    if (status.ok()) {
      if (stream) {
        result->reset(new EncryptedWritableFile(
            std::move(underlying), std::move(stream), prefix_length));
      } else {
        result->reset(underlying.release());
      }
    }
    return status;
  }

  // Creates a CipherStream for the underlying file/name using the options
  // If a writable provider is found and encryption is enabled, uses
  // this provider to create a cipher stream.
  // @param fname         Name of the writable file
  // @param underlying    The underlying "raw" file
  // @param options       Options for creating the file/cipher
  // @param prefix_length Returns the length of the encryption prefix used for
  // this file
  // @param stream        Returns the cipher stream to use for this file if it
  // should be encrypted
  // @return OK on success, non-OK on failure.
  template <class TypeFile>
  Status CreateRandomWriteCipherStream(
      const std::string& fname, const std::unique_ptr<TypeFile>& underlying,
      const EnvOptions& options, size_t* prefix_length,
      std::unique_ptr<BlockAccessCipherStream>* stream) {
    EncryptionProvider* provider = nullptr;
    *prefix_length = 0;
    Status status = GetWritableProvider(fname, &provider);
    if (!status.ok()) {
      return status;
    } else if (provider != nullptr) {
      // Initialize & write prefix (if needed)
      AlignedBuffer buffer;
      Slice prefix;
      *prefix_length = provider->GetPrefixLength();
      if (*prefix_length > 0) {
        // Initialize prefix
        buffer.Alignment(underlying->GetRequiredBufferAlignment());
        buffer.AllocateNewBuffer(*prefix_length);
        status = provider->CreateNewPrefix(fname, buffer.BufferStart(),
                                           *prefix_length);
        if (status.ok()) {
          buffer.Size(*prefix_length);
          prefix = Slice(buffer.BufferStart(), buffer.CurrentSize());
          // Write prefix
          status = underlying->Write(0, prefix);
        }
        if (!status.ok()) {
          return status;
        }
      }
      // Create cipher stream
      status = provider->CreateCipherStream(fname, options, prefix, stream);
    }
    return status;
  }

  // Creates a CipherStream for the underlying file/name using the options
  // If a readable provider is found and the file is encrypted, uses
  // this provider to create a cipher stream.
  // @param fname         Name of the writable file
  // @param underlying    The underlying "raw" file
  // @param options       Options for creating the file/cipher
  // @param prefix_length Returns the length of the encryption prefix used for
  // this file
  // @param stream        Returns the cipher stream to use for this file if it
  // is encrypted
  // @return OK on success, non-OK on failure.
  template <class TypeFile>
  Status CreateSequentialCipherStream(
      const std::string& fname, const std::unique_ptr<TypeFile>& underlying,
      const EnvOptions& options, size_t* prefix_length,
      std::unique_ptr<BlockAccessCipherStream>* stream) {
    // Read prefix (if needed)
    AlignedBuffer buffer;
    Slice prefix;
    *prefix_length = provider_->GetPrefixLength();
    if (*prefix_length > 0) {
      // Read prefix
      buffer.Alignment(underlying->GetRequiredBufferAlignment());
      buffer.AllocateNewBuffer(*prefix_length);
      Status status =
          underlying->Read(*prefix_length, &prefix, buffer.BufferStart());
      if (!status.ok()) {
        return status;
      }
      buffer.Size(*prefix_length);
    }
    return provider_->CreateCipherStream(fname, options, prefix, stream);
  }

  // Creates a CipherStream for the underlying file/name using the options
  // If a readable provider is found and the file is encrypted, uses
  // this provider to create a cipher stream.
  // @param fname         Name of the writable file
  // @param underlying    The underlying "raw" file
  // @param options       Options for creating the file/cipher
  // @param prefix_length Returns the length of the encryption prefix used for
  // this file
  // @param stream        Returns the cipher stream to use for this file if it
  // is encrypted
  // @return OK on success, non-OK on failure.
  template <class TypeFile>
  Status CreateRandomReadCipherStream(
      const std::string& fname, const std::unique_ptr<TypeFile>& underlying,
      const EnvOptions& options, size_t* prefix_length,
      std::unique_ptr<BlockAccessCipherStream>* stream) {
    // Read prefix (if needed)
    AlignedBuffer buffer;
    Slice prefix;
    *prefix_length = provider_->GetPrefixLength();
    if (*prefix_length > 0) {
      // Read prefix
      buffer.Alignment(underlying->GetRequiredBufferAlignment());
      buffer.AllocateNewBuffer(*prefix_length);
      Status status =
          underlying->Read(0, *prefix_length, &prefix, buffer.BufferStart());
      if (!status.ok()) {
        return status;
      }
      buffer.Size(*prefix_length);
    }
    return provider_->CreateCipherStream(fname, options, prefix, stream);
  }

 public:
  EncryptedEnvImpl(Env* base_env,
                   const std::shared_ptr<EncryptionProvider>& provider)
      : EnvWrapper(base_env) {
    provider_ = provider;
  }

  // NewSequentialFile opens a file for sequential reading.
  virtual Status NewSequentialFile(const std::string& fname,
                                   std::unique_ptr<SequentialFile>* result,
                                   const EnvOptions& options) override {
    result->reset();
    if (options.use_mmap_reads) {
      return Status::InvalidArgument();
    }
    // Open file using underlying Env implementation
    std::unique_ptr<SequentialFile> underlying;
    auto status = EnvWrapper::NewSequentialFile(fname, &underlying, options);
    if (!status.ok()) {
      return status;
    }
    // Create cipher stream
    std::unique_ptr<BlockAccessCipherStream> stream;
    size_t prefix_length;
    status = CreateSequentialCipherStream(fname, underlying, options,
                                          &prefix_length, &stream);
    if (status.ok()) {
      result->reset(new EncryptedSequentialFile(
          std::move(underlying), std::move(stream), prefix_length));
    }
    return status;
  }

  // NewRandomAccessFile opens a file for random read access.
  virtual Status NewRandomAccessFile(const std::string& fname,
                                     std::unique_ptr<RandomAccessFile>* result,
                                     const EnvOptions& options) override {
    result->reset();
    if (options.use_mmap_reads) {
      return Status::InvalidArgument();
    }
    // Open file using underlying Env implementation
    std::unique_ptr<RandomAccessFile> underlying;
    auto status = EnvWrapper::NewRandomAccessFile(fname, &underlying, options);
    if (!status.ok()) {
      return status;
    }
    std::unique_ptr<BlockAccessCipherStream> stream;
    size_t prefix_length;
    status = CreateRandomReadCipherStream(fname, underlying, options,
                                          &prefix_length, &stream);
    if (status.ok()) {
      if (stream) {
        result->reset(new EncryptedRandomAccessFile(
            std::move(underlying), std::move(stream), prefix_length));
      } else {
        result->reset(underlying.release());
      }
    }
    return status;
  }

  // NewWritableFile opens a file for sequential writing.
  virtual Status NewWritableFile(const std::string& fname,
                                 std::unique_ptr<WritableFile>* result,
                                 const EnvOptions& options) override {
    result->reset();
    if (options.use_mmap_writes) {
      return Status::InvalidArgument();
    }
    // Open file using underlying Env implementation
    std::unique_ptr<WritableFile> underlying;
    Status status = EnvWrapper::NewWritableFile(fname, &underlying, options);
    if (!status.ok()) {
      return status;
    }
    return CreateWritableEncryptedFile(fname, underlying, options, result);
  }

  // Create an object that writes to a new file with the specified
  // name.  Deletes any existing file with the same name and creates a
  // new file.  On success, stores a pointer to the new file in
  // *result and returns OK.  On failure stores nullptr in *result and
  // returns non-OK.
  //
  // The returned file will only be accessed by one thread at a time.
  virtual Status ReopenWritableFile(const std::string& fname,
                                    std::unique_ptr<WritableFile>* result,
                                    const EnvOptions& options) override {
    result->reset();
    if (options.use_mmap_writes) {
      return Status::InvalidArgument();
    }
    // Open file using underlying Env implementation
    std::unique_ptr<WritableFile> underlying;
    Status status = EnvWrapper::ReopenWritableFile(fname, &underlying, options);
    if (!status.ok()) {
      return status;
    }
    return CreateWritableEncryptedFile(fname, underlying, options, result);
  }

  // Reuse an existing file by renaming it and opening it as writable.
  virtual Status ReuseWritableFile(const std::string& fname,
                                   const std::string& old_fname,
                                   std::unique_ptr<WritableFile>* result,
                                   const EnvOptions& options) override {
    result->reset();
    if (options.use_mmap_writes) {
      return Status::InvalidArgument();
    }
    // Open file using underlying Env implementation
    std::unique_ptr<WritableFile> underlying;
    Status status =
        EnvWrapper::ReuseWritableFile(fname, old_fname, &underlying, options);
    if (!status.ok()) {
      return status;
    }
    return CreateWritableEncryptedFile(fname, underlying, options, result);
  }

  // Open `fname` for random read and write, if file doesn't exist the file
  // will be created.  On success, stores a pointer to the new file in
  // *result and returns OK.  On failure returns non-OK.
  //
  // The returned file will only be accessed by one thread at a time.
  virtual Status NewRandomRWFile(const std::string& fname,
                                 std::unique_ptr<RandomRWFile>* result,
                                 const EnvOptions& options) override {
    result->reset();
    if (options.use_mmap_reads || options.use_mmap_writes) {
      return Status::InvalidArgument();
    }
    // Check file exists
    bool isNewFile = !FileExists(fname).ok();

    // Open file using underlying Env implementation
    std::unique_ptr<RandomRWFile> underlying;
    Status status = EnvWrapper::NewRandomRWFile(fname, &underlying, options);
    if (!status.ok()) {
      return status;
    }
    // Create cipher stream
    std::unique_ptr<BlockAccessCipherStream> stream;
    size_t prefix_length = 0;
    if (!isNewFile) {
      // File already exists, read prefix
      status = CreateRandomReadCipherStream(fname, underlying, options,
                                            &prefix_length, &stream);
    } else {
      status = CreateRandomWriteCipherStream(fname, underlying, options,
                                             &prefix_length, &stream);
    }
    if (status.ok()) {
      if (stream) {
        result->reset(new EncryptedRandomRWFile(
            std::move(underlying), std::move(stream), prefix_length));
      } else {
        result->reset(underlying.release());
      }
    }
    return status;
  }

  // Store in *result the attributes of the children of the specified
  // directory.
  // In case the implementation lists the directory prior to iterating the
  // files
  // and files are concurrently deleted, the deleted files will be omitted
  // from
  // result.
  // The name attributes are relative to "dir".
  // Original contents of *results are dropped.
  // Returns OK if "dir" exists and "*result" contains its children.
  //         NotFound if "dir" does not exist, the calling process does not
  //         have
  //                  permission to access "dir", or if "dir" is invalid.
  //         IOError if an IO Error was encountered
  virtual Status GetChildrenFileAttributes(
      const std::string& dir, std::vector<FileAttributes>* result) override {
    auto status = EnvWrapper::GetChildrenFileAttributes(dir, result);
    if (!status.ok()) {
      return status;
    }
    for (auto it = std::begin(*result); it != std::end(*result); ++it) {
      // assert(it->size_bytes >= prefixLength);
      //  breaks env_basic_test when called on directory containing
      //  directories
      // which makes subtraction of prefixLength worrisome since
      // FileAttributes does not identify directories
      EncryptionProvider* provider;
      status = GetReadableProvider(it->name, &provider);
      if (!status.ok()) {
        return status;
      } else if (provider != nullptr) {
        it->size_bytes -= provider->GetPrefixLength();
      }
    }
    return Status::OK();
  }

  // Store the size of fname in *file_size.
  virtual Status GetFileSize(const std::string& fname,
                             uint64_t* file_size) override {
    auto status = EnvWrapper::GetFileSize(fname, file_size);
    if (!status.ok()) {
      return status;
    }
    EncryptionProvider* provider;
    status = GetReadableProvider(fname, &provider);
    if (provider != nullptr && status.ok()) {
      size_t prefixLength = provider->GetPrefixLength();
      assert(*file_size >= prefixLength);
      *file_size -= prefixLength;
    }
    return status;
  }

 private:
  std::shared_ptr<EncryptionProvider> provider_;
};

// Returns an Env that encrypts data when stored on disk and decrypts data when
// read from disk.
Env* NewEncryptedEnv(Env* base_env,
                     const std::shared_ptr<EncryptionProvider>& provider) {
  return new EncryptedEnvImpl(base_env, provider);
}

// Encrypt one or more (partial) blocks of data at the file offset.
// Length of data is given in dataSize.
Status BlockAccessCipherStream::Encrypt(uint64_t fileOffset, char *data, size_t dataSize) {
  // Calculate block index
  auto blockSize = BlockSize();
  uint64_t blockIndex = fileOffset / blockSize;
  size_t blockOffset = fileOffset % blockSize;
  std::unique_ptr<char[]> blockBuffer;

  std::string scratch;
  AllocateScratch(scratch);

  // Encrypt individual blocks.
  while (1) {
    char *block = data;
    size_t n = std::min(dataSize, blockSize - blockOffset);
    if (n != blockSize) {
      // We're not encrypting a full block.
      // Copy data to blockBuffer
      if (!blockBuffer.get()) {
        // Allocate buffer
        blockBuffer = std::unique_ptr<char[]>(new char[blockSize]);
      }
      block = blockBuffer.get();
      // Copy plain data to block buffer
      memmove(block + blockOffset, data, n);
    }
    auto status = EncryptBlock(blockIndex, block, (char*)scratch.data());
    if (!status.ok()) {
      return status;
    }
    if (block != data) {
      // Copy encrypted data back to `data`.
      memmove(data, block + blockOffset, n);
    }
    dataSize -= n;
    if (dataSize == 0) {
      return Status::OK();
    }
    data += n;
    blockOffset = 0;
    blockIndex++;
  }
}

// Decrypt one or more (partial) blocks of data at the file offset.
// Length of data is given in dataSize.
Status BlockAccessCipherStream::Decrypt(uint64_t fileOffset, char *data, size_t dataSize) {
  // Calculate block index
  auto blockSize = BlockSize();
  uint64_t blockIndex = fileOffset / blockSize;
  size_t blockOffset = fileOffset % blockSize;
  std::unique_ptr<char[]> blockBuffer;

  std::string scratch;
  AllocateScratch(scratch);

  // Decrypt individual blocks.
  while (1) {
    char *block = data;
    size_t n = std::min(dataSize, blockSize - blockOffset);
    if (n != blockSize) {
      // We're not decrypting a full block.
      // Copy data to blockBuffer
      if (!blockBuffer.get()) {
        // Allocate buffer
        blockBuffer = std::unique_ptr<char[]>(new char[blockSize]);
      }
      block = blockBuffer.get();
      // Copy encrypted data to block buffer
      memmove(block + blockOffset, data, n);
    }
    auto status = DecryptBlock(blockIndex, block, (char*)scratch.data());
    if (!status.ok()) {
      return status;
    }
    if (block != data) {
      // Copy decrypted data back to `data`.
      memmove(data, block + blockOffset, n);
    }

    // Simply decrementing dataSize by n could cause it to underflow,
    // which will very likely make it read over the original bounds later
    assert(dataSize >= n);
    if (dataSize < n) {
      return Status::Corruption("Cannot decrypt data at given offset");
    }

    dataSize -= n;
    if (dataSize == 0) {
      return Status::OK();
    }
    data += n;
    blockOffset = 0;
    blockIndex++;
  }
}

const char* ROT13BlockCipher::Name() const { return kROT13CipherName; }

// Encrypt a block of data.
// Length of data is equal to BlockSize().
Status ROT13BlockCipher::Encrypt(char* data) {
  for (size_t i = 0; i < blockSize_; ++i) {
    data[i] += 13;
  }
  return Status::OK();
}

// Decrypt a block of data.
// Length of data is equal to BlockSize().
Status ROT13BlockCipher::Decrypt(char* data) { return Encrypt(data); }

// Allocate scratch space which is passed to EncryptBlock/DecryptBlock.
void CTRCipherStream::AllocateScratch(std::string& scratch) {
  auto blockSize = cipher_->BlockSize();
  scratch.reserve(blockSize);
}

// Encrypt a block of data at the given block index.
// Length of data is equal to BlockSize();
Status CTRCipherStream::EncryptBlock(uint64_t blockIndex, char* data,
                                     char* scratch) {
  // Create nonce + counter
  auto blockSize = cipher_->BlockSize();
  memmove(scratch, iv_.data(), blockSize);
  EncodeFixed64(scratch, blockIndex + initialCounter_);

  // Encrypt nonce+counter
  auto status = cipher_->Encrypt(scratch);
  if (!status.ok()) {
    return status;
  }

  // XOR data with ciphertext.
  for (size_t i = 0; i < blockSize; i++) {
    data[i] = data[i] ^ scratch[i];
  }
  return Status::OK();
}

// Decrypt a block of data at the given block index.
// Length of data is equal to BlockSize();
Status CTRCipherStream::DecryptBlock(uint64_t blockIndex, char* data,
                                     char* scratch) {
  // For CTR decryption & encryption are the same
  return EncryptBlock(blockIndex, data, scratch);
}

const char* CTREncryptionProvider::Name() const { return kCTRProviderName; }

// GetPrefixLength returns the length of the prefix that is added to every file
// and used for storing encryption options.
// For optimal performance, the prefix length should be a multiple of
// the page size.
size_t CTREncryptionProvider::GetPrefixLength() const {
  return defaultPrefixLength;
}

Status CTREncryptionProvider::TEST_Initialize() {
  if (!cipher_) {
    return BlockCipher::CreateFromString(
        ConfigOptions(), std::string(kROT13CipherName) + ":32", &cipher_);
  }
  return Status::OK();
}

Status CTREncryptionProvider::AddCipher(const std::string& /*descriptor*/,
                                        const char* cipher, size_t len,
                                        bool /*for_write*/) {
  if (cipher_) {
    return Status::NotSupported("Cannot add keys to CTREncryptionProvider");
  } else if (strcmp(kROT13CipherName, cipher) == 0) {
    cipher_.reset(new ROT13BlockCipher(len));
    return Status::OK();
  } else {
    return BlockCipher::CreateFromString(ConfigOptions(), std::string(cipher),
                                         &cipher_);
  }
}

// decodeCTRParameters decodes the initial counter & IV from the given
// (plain text) prefix.
static void decodeCTRParameters(const char* prefix, size_t blockSize,
                                uint64_t& initialCounter, Slice& iv) {
  // First block contains 64-bit initial counter
  initialCounter = DecodeFixed64(prefix);
  // Second block contains IV
  iv = Slice(prefix + blockSize, blockSize);
}

// CreateNewPrefix initialized an allocated block of prefix memory
// for a new file.
Status CTREncryptionProvider::CreateNewPrefix(const std::string& /*fname*/,
                                              char* prefix,
                                              size_t prefixLength) const {
  if (!cipher_) {
    return Status::InvalidArgument("Encryption Cipher is missing");
  }
  // Create & seed rnd.
  Random rnd((uint32_t)Env::Default()->NowMicros());
  // Fill entire prefix block with random values.
  for (size_t i = 0; i < prefixLength; i++) {
    prefix[i] = rnd.Uniform(256) & 0xFF;
  }
  // Take random data to extract initial counter & IV
  auto blockSize = cipher_->BlockSize();
  uint64_t initialCounter;
  Slice prefixIV;
  decodeCTRParameters(prefix, blockSize, initialCounter, prefixIV);

  // Now populate the rest of the prefix, starting from the third block.
  PopulateSecretPrefixPart(prefix + (2 * blockSize),
                           prefixLength - (2 * blockSize), blockSize);

  // Encrypt the prefix, starting from block 2 (leave block 0, 1 with initial
  // counter & IV unencrypted)
  CTRCipherStream cipherStream(cipher_, prefixIV.data(), initialCounter);
  Status status;
  {
    PERF_TIMER_GUARD(encrypt_data_nanos);
    status = cipherStream.Encrypt(0, prefix + (2 * blockSize),
                                  prefixLength - (2 * blockSize));
  }
  if (!status.ok()) {
    return status;
  }
  return Status::OK();
}

// PopulateSecretPrefixPart initializes the data into a new prefix block
// in plain text.
// Returns the amount of space (starting from the start of the prefix)
// that has been initialized.
size_t CTREncryptionProvider::PopulateSecretPrefixPart(
    char* /*prefix*/, size_t /*prefixLength*/, size_t /*blockSize*/) const {
  // Nothing to do here, put in custom data in override when needed.
  return 0;
}

Status CTREncryptionProvider::CreateCipherStream(
    const std::string& fname, const EnvOptions& options, Slice& prefix,
    std::unique_ptr<BlockAccessCipherStream>* result) {
  if (!cipher_) {
    return Status::InvalidArgument("Encryption Cipher is missing");
  }
  // Read plain text part of prefix.
  auto blockSize = cipher_->BlockSize();
  uint64_t initialCounter;
  Slice iv;
  decodeCTRParameters(prefix.data(), blockSize, initialCounter, iv);

  // If the prefix is smaller than twice the block size, we would below read a
  // very large chunk of the file (and very likely read over the bounds)
  assert(prefix.size() >= 2 * blockSize);
  if (prefix.size() < 2 * blockSize) {
    return Status::Corruption("Unable to read from file " + fname +
                              ": read attempt would read beyond file bounds");
  }

  // Decrypt the encrypted part of the prefix, starting from block 2 (block 0, 1
  // with initial counter & IV are unencrypted)
  CTRCipherStream cipherStream(cipher_, iv.data(), initialCounter);
  Status status;
  {
    PERF_TIMER_GUARD(decrypt_data_nanos);
    status = cipherStream.Decrypt(0, (char*)prefix.data() + (2 * blockSize),
                                  prefix.size() - (2 * blockSize));
  }
  if (!status.ok()) {
    return status;
  }

  // Create cipher stream
  return CreateCipherStreamFromPrefix(fname, options, initialCounter, iv,
                                      prefix, result);
}

// CreateCipherStreamFromPrefix creates a block access cipher stream for a file
// given given name and options. The given prefix is already decrypted.
Status CTREncryptionProvider::CreateCipherStreamFromPrefix(
    const std::string& /*fname*/, const EnvOptions& /*options*/,
    uint64_t initialCounter, const Slice& iv, const Slice& /*prefix*/,
    std::unique_ptr<BlockAccessCipherStream>* result) {
  (*result) = std::unique_ptr<BlockAccessCipherStream>(
      new CTRCipherStream(cipher_, iv.data(), initialCounter));
  return Status::OK();
}

#endif // ROCKSDB_LITE

}  // namespace ROCKSDB_NAMESPACE
