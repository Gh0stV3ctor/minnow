#include "byte_stream.hh"

using namespace std;

// Constructor: store the capacity; all other members are default-initialized.
ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

bool Writer::is_closed() const
{
  return closed_; // just read the flag
}

void Writer::push( string data )
{
  // After close(), no more bytes may be written.
  if ( closed_ ) {
    return;
  }

  // Decide how many bytes we can actually accept this time.
  uint64_t n = data.size();
  if ( n > available_capacity() ) {
    n = available_capacity(); // clamp down to the remaining capacity
  }

  data.resize( n );   // shrink data to the accepted prefix
  buffer_ += data;    // append the accepted bytes
  bytes_pushed_ += n; // count only the bytes actually accepted
}

void Writer::close()
{
  closed_ = true; // mark the stream as ended
}

uint64_t Writer::available_capacity() const
{
  // capacity minus the bytes still buffered = room left
  return capacity_ - ( bytes_pushed_ - bytes_popped_ );
}

uint64_t Writer::bytes_pushed() const
{ return bytes_pushed_; }

bool Reader::is_finished() const
{
  // finished = input ended AND buffer drained
  return closed_ && bytes_buffered() == 0;
}

uint64_t Reader::bytes_popped() const
{ return bytes_popped_; }

string_view Reader::peek() const
{
  // view the whole remaining buffer without copying it
  return string_view( buffer_ );
}

void Reader::pop( uint64_t len )
{
  // Never pop more bytes than are currently buffered.
  if ( len > bytes_buffered() ) {
    len = bytes_buffered();
  }

  buffer_.erase( 0, len ); // remove len bytes from the front
  bytes_popped_ += len;    // account for the bytes removed
}

uint64_t Reader::bytes_buffered() const
{
  // pushed minus popped = currently buffered
  return bytes_pushed_ - bytes_popped_;
}
