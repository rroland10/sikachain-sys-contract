// =============================================================================
// SikaChain — sika.rules implementation
// =============================================================================

#include <sika.rules/sika.rules.hpp>
#include <sika.accounts.hpp>

#include <eosio/action.hpp>
#include <eosio/print.hpp>
#include <eosio/transaction.hpp>
#include <eosio/crypto.hpp>

#include <algorithm>

namespace sikarules {

   using std::string;

   // Mirror of sika.system::producer_info for cross-contract table reads.
   struct [[eosio::table]] producer_row {
      name              owner;
      double            total_votes        = 0;
      eosio::public_key producer_key;
      bool              is_active          = true;
      std::string       url;
      uint32_t          unpaid_blocks      = 0;
      eosio::time_point last_claim_time;
      uint16_t          location           = 0;
      uint16_t          uptime_bps         = 10000;
      bool              has_public_rpc     = false;
      bool              upgrade_on_time    = true;
      bool              issues_within_sla  = true;
      eosio::time_point open_issue_at;
      uint8_t           probation_flags    = 0;
      eosio::time_point probation_started_at;

      uint64_t primary_key() const { return owner.value; }

      EOSLIB_SERIALIZE( producer_row,
         (owner)(total_votes)(producer_key)(is_active)(url)(unpaid_blocks)
         (last_claim_time)(location)
         (uptime_bps)(has_public_rpc)(upgrade_on_time)(issues_within_sla)(open_issue_at)
         (probation_flags)(probation_started_at) )
   };
   using producers_table = eosio::multi_index<"producers"_n, producer_row>;

   // ---------------------------------------------------------------------------
   // propose — any account creates a new amendment
   // ---------------------------------------------------------------------------
   void rules_contract::propose( const name& proposer,
                                  const string& article,
                                  const string& rationale,
                                  const std::vector<char>& packed_tx )
   {
      require_auth( proposer );
      check( article.size()   <= 256,    "article label too long" );
      check( rationale.size() <= 4096,   "rationale too long" );
      check( packed_tx.size() <= 32'768,
             "packed_transaction must be at most 32768 bytes" );

      amendments_table tbl( get_self(), get_self().value );
      uint64_t aid = tbl.available_primary_key();

      auto now = current_time_point();
      tbl.emplace( get_self(), [&]( auto& a ) {
         a.id                 = aid;
         a.proposer           = proposer;
         a.article            = article;
         a.rationale          = rationale;
         a.packed_transaction = packed_tx;
         a.status             = static_cast<uint8_t>(amendment_status::PROPOSED);
         a.created_at         = now;
         a.expires_at         = now + eosio::seconds(max_proposal_lifetime);
      });

      eosio::print( "Amendment #", aid, " proposed for ", article, "\n" );
   }

   void rules_contract::approve( name bp, uint64_t amendment_id ) {
      require_auth( bp );

      auto top21 = current_top21();
      check( std::find( top21.begin(), top21.end(), bp ) != top21.end(),
             "only current Top 21 BPs may approve amendments" );

      amendments_table tbl( get_self(), get_self().value );
      auto it = tbl.require_find( amendment_id, "unknown amendment" );
      check( current_time_point() < it->expires_at, "amendment expired" );
      check( it->status == static_cast<uint8_t>(amendment_status::PROPOSED),
             "amendment is no longer accepting approvals" );

      check( std::find( it->bp_approvals.begin(), it->bp_approvals.end(), bp )
             == it->bp_approvals.end(),
             "BP already approved" );

      tbl.modify( it, get_self(), [&]( auto& a ) {
         a.bp_approvals.push_back( bp );

         if( count_valid_approvals( a.bp_approvals ) >= bp_approval_threshold ) {
            a.status        = static_cast<uint8_t>(amendment_status::APPROVED);
            a.approved_at   = current_time_point();
            a.executable_at = current_time_point()
                            + eosio::seconds(veto_window_seconds);
            eosio::print( "Amendment #", amendment_id,
                          " APPROVED. Veto window: 7 days.\n" );
         }
      });
   }

   void rules_contract::unapprove( name bp, uint64_t amendment_id ) {
      require_auth( bp );
      amendments_table tbl( get_self(), get_self().value );
      auto it = tbl.require_find( amendment_id, "unknown amendment" );

      check( it->status == static_cast<uint8_t>(amendment_status::PROPOSED),
             "amendment already locked in" );

      tbl.modify( it, get_self(), [&]( auto& a ) {
         a.bp_approvals.erase(
            std::remove( a.bp_approvals.begin(), a.bp_approvals.end(), bp ),
            a.bp_approvals.end() );
      });
   }

   void rules_contract::veto( uint64_t amendment_id, const string& rationale ) {
      require_auth( "sika.guard"_n );
      check( rationale.size() <= 2048, "rationale too long" );

      amendments_table tbl( get_self(), get_self().value );
      auto it = tbl.require_find( amendment_id, "unknown amendment" );
      check( it->status == static_cast<uint8_t>(amendment_status::APPROVED),
             "veto only applies during the 7-day window after approval" );
      check( current_time_point() < it->executable_at,
             "veto window has expired" );

      tbl.modify( it, get_self(), [&]( auto& a ) {
         a.status = static_cast<uint8_t>(amendment_status::VETOED);
      });

      eosio::print( "Amendment #", amendment_id, " VETOED by Guardians: ",
                    rationale, "\n" );
   }

   void rules_contract::execute( name executor, uint64_t amendment_id ) {
      require_auth( executor );

      amendments_table tbl( get_self(), get_self().value );
      auto it = tbl.require_find( amendment_id, "unknown amendment" );

      if( it->status == static_cast<uint8_t>(amendment_status::VETOED) ) {
         check( false, "amendment has been vetoed" );
      }
      check( it->status == static_cast<uint8_t>(amendment_status::APPROVED),
             "amendment not yet executable" );
      check( current_time_point() >= it->executable_at,
             "Guardian veto window has not expired" );

      check( count_valid_approvals( it->bp_approvals ) >= bp_approval_threshold,
             "approvals dropped below 17-of-21 due to BP rotation" );

      eosio::transaction tx;
      if( !it->packed_transaction.empty() ) {
         eosio::datastream<const char*> ds( it->packed_transaction.data(),
                                             it->packed_transaction.size() );
         ds >> tx;
         if( !tx.actions.empty() ) {
            tx.send( amendment_id, get_self() );
         }
      }

      tbl.modify( it, get_self(), [&]( auto& a ) {
         a.status = static_cast<uint8_t>(amendment_status::EXECUTED);
      });

      eosio::print( "Amendment #", amendment_id, " EXECUTED by ", executor, "\n" );
   }

   void rules_contract::gc( uint64_t amendment_id ) {
      amendments_table tbl( get_self(), get_self().value );
      auto it = tbl.require_find( amendment_id, "unknown amendment" );

      auto now = current_time_point();
      auto status = static_cast<amendment_status>(it->status);

      bool can_gc =
         (status == amendment_status::EXECUTED   && now > it->approved_at + eosio::days(90)) ||
         (status == amendment_status::VETOED     && now > it->approved_at + eosio::days(90)) ||
         (status == amendment_status::EXPIRED                                            ) ||
         (status == amendment_status::PROPOSED   && now > it->expires_at                 );

      check( can_gc, "amendment not eligible for garbage collection yet" );
      tbl.erase( it );
   }

   size_t rules_contract::count_valid_approvals( const std::vector<name>& approvals ) {
      auto top21 = current_top21();
      size_t valid = 0;
      for( const auto& a : approvals ) {
         if( std::find( top21.begin(), top21.end(), a ) != top21.end() ) ++valid;
      }
      return valid;
   }

   std::vector<name> rules_contract::current_top21() {
      producers_table producers( sikaaccounts::SYSTEM, sikaaccounts::SYSTEM.value );

      std::vector<std::pair<double, name>> ranked;
      ranked.reserve( 32 );
      for( auto it = producers.begin(); it != producers.end(); ++it ) {
         if( it->is_active ) {
            ranked.emplace_back( it->total_votes, it->owner );
         }
      }

      std::sort( ranked.begin(), ranked.end(),
                 []( const auto& a, const auto& b ) { return a.first > b.first; } );

      std::vector<name> top;
      for( size_t i = 0; i < ranked.size() && i < bp_active_count; ++i ) {
         top.push_back( ranked[i].second );
      }
      return top;
   }

} // namespace sikarules
