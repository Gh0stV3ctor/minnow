#include "tcp_receiver.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  if ( message.RST ) {
    reassembler_.reader().set_error();
    return;
  }

  if ( message.SYN ) {
    isn_ = message.seqno;
  }

  if ( not isn_.has_value() ) {
    return; // ignore everything until SYN is received
  }

  // Absolute sequence number of the first unit in this segment.
  // checkpoint = next expected absolute seqno = (bytes already pushed) + 1 (for SYN).
  const uint64_t checkpoint = reassembler_.writer().bytes_pushed() + 1;
  const uint64_t abs_seqno = message.seqno.unwrap( *isn_, checkpoint );

  // Stream index of the payload's first byte. SYN occupies one sequence number:
  //   - if SYN is set, seqno points to SYN, so payload begins one seqno later → index = abs_seqno
  //   - otherwise, seqno points to the payload → index = abs_seqno - 1
  const uint64_t first_index = message.SYN ? abs_seqno : abs_seqno - 1;

  reassembler_.insert( first_index, std::move( message.payload ), message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  TCPReceiverMessage msg;
  msg.RST = reassembler_.reader().has_error();

  const uint64_t available = reassembler_.writer().available_capacity();
  msg.window_size = available > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>( available );

  if ( isn_.has_value() ) {
    uint64_t abs_ackno = reassembler_.writer().bytes_pushed() + 1; // +1 for SYN
    if ( reassembler_.writer().is_closed() ) {
      abs_ackno += 1; // +1 for FIN
    }
    msg.ackno = Wrap32::wrap( abs_ackno, *isn_ );
  }

  return msg;
}
