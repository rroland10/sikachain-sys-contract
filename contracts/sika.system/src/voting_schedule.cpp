// =============================================================================
// SikaChain — elected producer schedule (mirrors eosio.system voting.cpp)
// =============================================================================
// Without set_proposed_producers the chain stays on the genesis producer (sika)
// even when BPs are registered and voted — required for multinode rotation.
// =============================================================================

#include <sika.system/sika.system.hpp>

#include <algorithm>

namespace sikasystem {

   void system_contract::update_elected_producers( block_timestamp block_time ) {
      _gstate.last_producer_schedule_update = block_time;

      auto idx = _producers.get_index<"prototalvote"_n>();

      std::vector< std::pair<eosio::producer_key, uint16_t> > top_producers;
      top_producers.reserve( static_cast<size_t>( top21_count ) );

      for( auto it = idx.cbegin();
           it != idx.cend()
           && top_producers.size() < static_cast<size_t>( top21_count )
           && 0 < it->total_votes
           && it->active();
           ++it )
      {
         top_producers.emplace_back(
            std::pair<eosio::producer_key, uint16_t>(
               {{ it->owner, it->producer_key }, it->location} ) );
      }

      if( top_producers.empty() ) {
         return;
      }

      if( top_producers.size() < _gstate.last_producer_schedule_size ) {
         return;
      }

      std::sort( top_producers.begin(), top_producers.end() );

      std::vector<eosio::producer_key> producers;
      producers.reserve( top_producers.size() );
      for( const auto& item : top_producers ) {
         producers.push_back( item.first );
      }

      if( set_proposed_producers( producers ) >= 0 ) {
         _gstate.last_producer_schedule_size = static_cast<uint16_t>( top_producers.size() );
      }
   }

} // namespace sikasystem
