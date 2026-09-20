#include "reassembler.hh"

using namespace std;

void Reassembler::merge_into_pending( uint64_t first_index, string data )
{
  if ( data.empty() ) {
    return;
  }

  uint64_t begin = first_index;
  uint64_t end = first_index + data.size();

  // Merge with the piece immediately before (or overlapping) `begin`.
  auto it = pending_.lower_bound( begin );
  if ( it != pending_.begin() ) {
    auto prev = std::prev( it );
    const uint64_t prev_end = prev->first + prev->second.size();
    if ( prev_end >= begin ) {
      if ( prev_end >= end ) {
        return; // new piece is fully contained in prev
      }
      data = prev->second + data.substr( prev_end - begin );
      begin = prev->first;
      pending_.erase( prev );
    }
  }

  // Merge forward over any piece that overlaps or touches [begin, end).
  it = pending_.lower_bound( begin );
  while ( it != pending_.end() && it->first <= end ) {
    const uint64_t it_end = it->first + it->second.size();
    if ( it_end > end ) {
      data += it->second.substr( end - it->first );
      end = it_end;
    }
    it = pending_.erase( it );
  }

  pending_[begin] = std::move( data );
}

void Reassembler::flush_pending()
{
  while ( !pending_.empty() ) {
    auto it = pending_.begin();
    const uint64_t idx = it->first;

    if ( idx > next_index_ ) {
      break; // gap remains
    }

    string& data = it->second;
    if ( idx + data.size() <= next_index_ ) {
      pending_.erase( it ); // entirely already written
      continue;
    }

    const uint64_t overlap = next_index_ - idx;
    if ( overlap > 0 ) {
      data.erase( 0, overlap );
    }

    output_.writer().push( data );
    next_index_ += data.size();
    pending_.erase( it );
  }
}

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  if ( is_last_substring ) {
    eof_ = true;
    eof_index_ = first_index + data.size();
  }

  // Discard bytes that have already been written.
  if ( first_index < next_index_ ) {
    const uint64_t overlap = next_index_ - first_index;
    if ( overlap >= data.size() ) {
      if ( eof_ && next_index_ >= eof_index_ ) {
        output_.writer().close();
      }
      return;
    }
    data.erase( 0, overlap );
    first_index = next_index_;
  }

  // Discard bytes beyond the capacity window.
  const uint64_t first_unacceptable = next_index_ + output_.writer().available_capacity();
  if ( first_index >= first_unacceptable ) {
    if ( eof_ && next_index_ >= eof_index_ ) {
      output_.writer().close();
    }
    return;
  }
  if ( first_index + data.size() > first_unacceptable ) {
    data.resize( first_unacceptable - first_index );
  }

  if ( data.empty() ) {
    if ( eof_ && next_index_ >= eof_index_ ) {
      output_.writer().close();
    }
    return;
  }

  merge_into_pending( first_index, std::move( data ) );
  flush_pending();

  if ( eof_ && next_index_ >= eof_index_ ) {
    output_.writer().close();
  }
}

uint64_t Reassembler::bytes_pending() const
{
  uint64_t total = 0;
  for ( const auto& entry : pending_ ) {
    total += entry.second.size();
  }
  return total;
}
