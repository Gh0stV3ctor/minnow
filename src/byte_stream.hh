#pragma once
// Header guard: ensure this file is included only once per translation unit.

#include <cstdint>     // uint64_t: 64-bit unsigned integer
#include <string>      // std::string: the byte buffer
#include <string_view> // std::string_view: read-only view returned by peek()

class Reader; // Forward declaration
class Writer; // Forward declaration

// ByteStream: an in-memory, flow-controlled FIFO byte stream.
class ByteStream
{
public:
  // Constructor: sets the maximum number of bytes the stream may buffer at once.
  explicit ByteStream( uint64_t capacity );

  // Helper functions (provided) to access the same object through its Reader/Writer views.
  Reader& reader();
  const Reader& reader() const;
  Writer& writer();
  const Writer& writer() const;

  void set_error() { error_ = true; };       // Mark the stream as having errored.
  bool has_error() const { return error_; }; // Has the stream errored?

protected:
  // Add ALL additional state HERE (base class), not in Reader/Writer.
  uint64_t capacity_; // maximum buffered bytes
  bool error_ {};     // error flag

  bool closed_ {};           // whether the writer has ended input
  uint64_t bytes_pushed_ {}; // total bytes ever pushed
  uint64_t bytes_popped_ {}; // total bytes ever popped
  std::string buffer_ {};    // the actual buffered bytes
};

// Writer: the "write side" view of a ByteStream.
class Writer : public ByteStream
{
public:
  void push( std::string data ); // Append data, but only up to available capacity.
  void close();                  // Signal end of input; nothing more may be written.

  bool is_closed() const;              // Has input been ended?
  uint64_t available_capacity() const; // How many bytes can still be pushed?
  uint64_t bytes_pushed() const;       // Cumulative bytes pushed.
};

// Reader: the "read side" view of a ByteStream.
class Reader : public ByteStream
{
public:
  std::string_view peek() const; // View buffered bytes without removing them.
  void pop( uint64_t len );      // Remove len bytes from the front.

  bool is_finished() const;        // Finished = closed AND fully popped?
  uint64_t bytes_buffered() const; // Bytes currently buffered (pushed - popped).
  uint64_t bytes_popped() const;   // Cumulative bytes popped.
};

/*
 * read: a provided helper that peeks and pops up to `len` bytes into `out`.
 */
void read( Reader& reader, uint64_t len, std::string& out );
