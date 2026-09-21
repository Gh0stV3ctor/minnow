#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  // uint32_t addition wraps naturally, which is exactly (zero_point + n) mod 2^32.
  return Wrap32 { zero_point.raw_value_ + static_cast<uint32_t>( n ) };
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  constexpr uint64_t MOD = 1UL << 32;
  constexpr uint64_t HALF = 1UL << 31;

  // Low 32 bits of (this - zero_point), in [0, 2^32).
  const uint64_t abs_seqno = static_cast<uint64_t>( this->raw_value_ - zero_point.raw_value_ );

  // Replace the low 32 bits of checkpoint with abs_seqno: the candidate closest to checkpoint.
  uint64_t n = ( checkpoint & ~( MOD - 1 ) ) | abs_seqno;

  // If checkpoint is more than half a wrap away, the neighbor on the other side is closer.
  if ( n < checkpoint && checkpoint - n > HALF ) {
    n += MOD;
  } else if ( n > checkpoint && n - checkpoint > HALF && n >= MOD ) {
    n -= MOD;
  }

  return n;
}
