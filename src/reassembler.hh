#pragma once

#include "byte_stream.hh"

#include <map>

class Reassembler
{
public:
  // Construct Reassembler to write into given ByteStream.
  explicit Reassembler( ByteStream&& output ) : output_( std::move( output ) ) {}

  /*
   * Insert a new substring to be reassembled into a ByteStream.
   *   `first_index`: the index of the first byte of the substring
   *   `data`: the substring itself
   *   `is_last_substring`: this substring represents the end of the stream
   */
  void insert( uint64_t first_index, std::string data, bool is_last_substring );

  // How many bytes are stored in the Reassembler itself?
  uint64_t bytes_pending() const;

  // Access output stream reader
  Reader& reader() { return output_.reader(); }
  const Reader& reader() const { return output_.reader(); }

  // Access output stream writer, but const-only (can't write from outside)
  const Writer& writer() const { return output_.writer(); }

private:
  ByteStream output_;

  uint64_t next_index_ { 0 };                     // next byte index to write into output_
  bool eof_ { false };                            // have we learned the end of the stream?
  uint64_t eof_index_ { 0 };                      // total length of the stream (once known)
  std::map<uint64_t, std::string> pending_ {};    // out-of-order pieces, keyed by start index, kept disjoint

  // Merge [first_index, first_index + data.size()) into pending_, deduplicating overlaps.
  void merge_into_pending( uint64_t first_index, std::string data );

  // Push every pending byte that has become contiguous.
  void flush_pending();
};
