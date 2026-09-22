#include "tcp_sender.hh"
#include "tcp_config.hh"

using namespace std;

uint64_t TCPSender::sequence_numbers_in_flight() const
{ return next_seqno_ - acked_seqno_; }

uint64_t TCPSender::consecutive_retransmissions() const
{ return consecutive_retransmissions_; }

void TCPSender::push( const TransmitFunction& transmit )
{
  const uint64_t effective_window = receiver_window_size_ == 0 ? 1 : receiver_window_size_;

  while ( sequence_numbers_in_flight() < effective_window and not fin_sent_ ) {
    const uint64_t window_remaining = effective_window - sequence_numbers_in_flight();
    TCPSenderMessage message = make_empty_message();

    if ( not syn_sent_ ) {
      message.SYN = true;
      syn_sent_ = true;
    }

    const uint64_t payload_capacity
      = min<uint64_t>( TCPConfig::MAX_PAYLOAD_SIZE, window_remaining - message.sequence_length() );
    read( input_.reader(), payload_capacity, message.payload );

    if ( not fin_sent_ and input_.reader().is_finished() and message.sequence_length() < window_remaining ) {
      message.FIN = true;
      fin_sent_ = true;
    }

    const uint64_t message_length = message.sequence_length();
    if ( message_length == 0 ) {
      break;
    }

    const bool timer_was_stopped = outstanding_.empty();
    transmit( message );
    next_seqno_ += message_length;
    outstanding_.push_back( { std::move( message ), next_seqno_ } );

    if ( timer_was_stopped ) {
      timer_elapsed_ms_ = 0;
    }
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{ return { .seqno = Wrap32::wrap( next_seqno_, isn_ ), .RST = input_.reader().has_error() }; }

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if ( msg.RST ) {
    input_.writer().set_error();
    return;
  }

  receiver_window_size_ = msg.window_size;

  if ( not msg.ackno.has_value() ) {
    return;
  }

  const uint64_t absolute_ackno = msg.ackno->unwrap( isn_, next_seqno_ );
  if ( absolute_ackno > next_seqno_ or absolute_ackno <= acked_seqno_ ) {
    return;
  }

  acked_seqno_ = absolute_ackno;
  while ( not outstanding_.empty() and outstanding_.front().end_seqno <= absolute_ackno ) {
    outstanding_.pop_front();
  }

  current_RTO_ms_ = initial_RTO_ms_;
  timer_elapsed_ms_ = 0;
  consecutive_retransmissions_ = 0;
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( outstanding_.empty() ) {
    return;
  }

  timer_elapsed_ms_ += ms_since_last_tick;
  if ( timer_elapsed_ms_ < current_RTO_ms_ ) {
    return;
  }

  transmit( outstanding_.front().message );
  timer_elapsed_ms_ = 0;

  if ( receiver_window_size_ > 0 ) {
    ++consecutive_retransmissions_;
    current_RTO_ms_ *= 2;
  }
}
