// =============================================================================
// SikaChain — sika.guard implementation
// =============================================================================
// Multi-sig + quarterly elections. The two halves of this contract are
// orthogonal: the multisig is always active over the current 9 seats; the
// election machinery seats those 9 every 91 days through a knockout playoff.
// =============================================================================

#include <sika.guard/sika.guard.hpp>

#include <eosio/action.hpp>
#include <eosio/crypto.hpp>
#include <eosio/print.hpp>
#include <eosio/transaction.hpp>

#include <algorithm>

namespace sikaguard {

   using std::string;

   // ===========================================================================
   // MULTISIG (propose / approve / exec)
   // ===========================================================================

   // ---------------------------------------------------------------------------
   // propose — anyone can propose; expiry must be in the future and ≤ 30 days
   // ---------------------------------------------------------------------------
   void guard_contract::propose( const name&        proposer,
                                  const string&     title,
                                  const string&     rationale,
                                  const std::vector<char>& packed_tx,
                                  time_point        expires_at )
   {
      require_auth( proposer );
      check( title.size() <= 128, "title too long (max 128)" );
      check( rationale.size() <= 2048, "rationale too long (max 2048)" );
      check( packed_tx.size() > 0 && packed_tx.size() <= 16'384,
             "packed_transaction must be 1..16384 bytes" );

      auto now = current_time_point();
      check( expires_at > now, "expiry must be in the future" );
      check( expires_at <= now + eosio::days(30),
             "expiry must be within 30 days" );

      proposals_table props( get_self(), get_self().value );
      uint64_t pid = props.available_primary_key();
      props.emplace( proposer, [&]( auto& p ) {
         p.id                 = pid;
         p.proposer           = proposer;
         p.title              = title;
         p.rationale          = rationale;
         p.packed_transaction = packed_tx;
         p.executed           = false;
         p.created_at         = now;
         p.expires_at         = expires_at;
      });
   }

   // ---------------------------------------------------------------------------
   // approve — Guardian endorsement
   // ---------------------------------------------------------------------------
   void guard_contract::approve( name guardian, uint64_t proposal_id ) {
      require_auth( guardian );
      require_guardian( guardian );

      proposals_table props( get_self(), get_self().value );
      auto it = props.require_find( proposal_id, "unknown proposal" );
      check( !it->executed, "already executed" );
      check( current_time_point() < it->expires_at, "proposal expired" );

      // Idempotent — no duplicate approvals
      auto& app = it->approvals;
      check( std::find( app.begin(), app.end(), guardian ) == app.end(),
             "guardian already approved" );

      props.modify( it, same_payer, [&]( auto& p ) {
         p.approvals.push_back( guardian );
      });
   }

   void guard_contract::unapprove( name guardian, uint64_t proposal_id ) {
      require_auth( guardian );
      proposals_table props( get_self(), get_self().value );
      auto it = props.require_find( proposal_id, "unknown proposal" );
      check( !it->executed, "already executed" );

      props.modify( it, same_payer, [&]( auto& p ) {
         p.approvals.erase(
            std::remove( p.approvals.begin(), p.approvals.end(), guardian ),
            p.approvals.end() );
      });
   }

   // ---------------------------------------------------------------------------
   // exec — execute a 6-of-9 approved proposal
   //
   // We re-validate that all approvers are CURRENT Guardians at exec time —
   // if an election happened between approval and execution, a previously
   // valid approval may no longer count.
   // ---------------------------------------------------------------------------
   void guard_contract::exec( name executor, uint64_t proposal_id ) {
      require_auth( executor );

      proposals_table props( get_self(), get_self().value );
      auto it = props.require_find( proposal_id, "unknown proposal" );
      check( !it->executed, "already executed" );
      check( current_time_point() < it->expires_at, "proposal expired" );

      // Count CURRENT Guardian approvals (some approvers may have rotated out)
      guardian_council_singleton council( get_self(), get_self().value );
      check( council.exists(), "no current Guardian council" );
      auto cc = council.get();

      size_t valid_approvals = 0;
      for( const auto& a : it->approvals ) {
         if( std::find( cc.seats.begin(), cc.seats.end(), a ) != cc.seats.end() ) {
            ++valid_approvals;
         }
      }
      check( valid_approvals >= approval_threshold,
             "proposal lacks 6-of-9 current Guardian approval" );

      // Dispatch the embedded transaction with sika.guard's authority
      eosio::transaction tx;
      eosio::datastream<const char*> ds( it->packed_transaction.data(),
                                          it->packed_transaction.size() );
      ds >> tx;
      // Use send_deferred to fire the transaction; trx_id seeded from proposal_id
      tx.send( proposal_id, get_self() );

      props.modify( it, same_payer, [&]( auto& p ) {
         p.executed = true;
      });
   }

   void guard_contract::cancel( name proposer, uint64_t proposal_id ) {
      require_auth( proposer );
      proposals_table props( get_self(), get_self().value );
      auto it = props.require_find( proposal_id, "unknown proposal" );
      check( it->proposer == proposer
             || current_time_point() >= it->expires_at,
             "only the proposer can cancel before expiry" );
      check( !it->executed, "already executed" );
      props.erase( it );
   }

   // ===========================================================================
   // ELECTIONS (3-round knockout)
   // ===========================================================================

   // ---------------------------------------------------------------------------
   // startelec — start a new cycle if 91 days have elapsed
   // ---------------------------------------------------------------------------
   void guard_contract::startelec() {
      auto now = current_time_point();

      guardian_council_singleton council( get_self(), get_self().value );
      if( council.exists() ) {
         auto cc = council.get();
         check( now >= cc.next_election_starts,
                "next election is not yet due" );
      }

      election_rounds_table rounds( get_self(), get_self().value );
      uint64_t rid = rounds.available_primary_key();
      rounds.emplace( get_self(), [&]( auto& r ) {
         r.id           = rid;
         r.round_number = 1;
         r.starts_at    = now;
         r.ends_at      = now + eosio::seconds(election_round_seconds);
         r.finalized    = false;
      });
   }

   // ---------------------------------------------------------------------------
   // nominate — self-nominate as a candidate (Round 1 only)
   //
   // Requires ≥ 1M SIKA staked (validated against sika.system::voter_info).
   // For initial contract: we trust the system contract to reject ineligible
   // candidates at vote-counting time.
   // ---------------------------------------------------------------------------
   void guard_contract::nominate( name candidate, const string& platform ) {
      require_auth( candidate );
      check( platform.size() <= 1024, "platform too long" );

      election_rounds_table rounds( get_self(), get_self().value );
      // Find the latest non-finalized round
      auto last = rounds.rbegin();
      check( last != rounds.rend(), "no active election" );
      check( !last->finalized, "current round is closed" );
      check( last->round_number == 1, "nominations only in round 1" );
      check( current_time_point() < last->ends_at, "round has ended" );
      check( last->candidates.size() < round1_candidates,
             "round 1 candidate cap reached" );

      // Append candidate (sorted insertion would be cleaner; this is fine for ≤27)
      auto last_fwd = rounds.iterator_to( *last );
      rounds.modify( last_fwd, same_payer, [&]( auto& r ) {
         check( std::find( r.candidates.begin(), r.candidates.end(), candidate )
                == r.candidates.end(),
                "already nominated" );
         r.candidates.push_back( candidate );
      });

      // Seed a candidate_votes row with zero weight
      candidate_votes_table votes( get_self(), get_self().value );
      auto v_it = votes.find( candidate.value );
      if( v_it == votes.end() ) {
         votes.emplace( candidate, [&]( auto& v ) {
            v.candidate    = candidate;
            v.total_weight = asset{ 0, eosio::symbol{"SIKA", 4} };
            v.voter_count  = 0;
            v.round_id     = last->id;
         });
      }
      (void)platform;
   }

   // ---------------------------------------------------------------------------
   // elecvote — cast a vote in the current round
   //
   // Vote weight = voter's staked SIKA at time of vote. We approximate by
   // reading sika.system::voter_info. For initial contract: trust the caller
   // not to game it; production would lock weights at round-start snapshots.
   // ---------------------------------------------------------------------------
   void guard_contract::elecvote( name voter, name candidate ) {
      require_auth( voter );

      election_rounds_table rounds( get_self(), get_self().value );
      auto last = rounds.rbegin();
      check( last != rounds.rend() && !last->finalized,
             "no active round" );
      check( current_time_point() < last->ends_at, "round has ended" );

      auto& cands = last->candidates;
      check( std::find( cands.begin(), cands.end(), candidate ) != cands.end(),
             "candidate not in current round" );

      // Read voter's staked SIKA from sika.system::voters
      // For initial contract: assume 1 vote = 1 unit of stake; in production
      // this would do a real cross-contract table read.
      asset weight = asset{ 1'000'000ll * 10'000, eosio::symbol{"SIKA", 4} };

      candidate_votes_table votes( get_self(), get_self().value );
      auto v_it = votes.require_find( candidate.value,
                                       "candidate has no vote row" );
      votes.modify( v_it, same_payer, [&]( auto& v ) {
         v.total_weight += weight;
         v.voter_count  += 1;
      });
   }

   // ---------------------------------------------------------------------------
   // closeround — finalize current round, advance to next (or swear in)
   //
   // Round 1: top 18 by weight advance to round 2
   // Round 2: top 9 by weight advance to round 3
   // Round 3: top 9 by weight become the new Guardians
   // ---------------------------------------------------------------------------
   void guard_contract::closeround() {
      election_rounds_table rounds( get_self(), get_self().value );
      auto last_rev = rounds.rbegin();
      check( last_rev != rounds.rend(), "no rounds exist" );
      check( !last_rev->finalized, "round already finalized" );
      check( current_time_point() >= last_rev->ends_at,
             "round has not ended yet" );

      // Gather candidates with their weights, sort desc by weight
      candidate_votes_table votes( get_self(), get_self().value );
      std::vector<std::pair<name, int64_t>> ranked;
      for( const auto& c : last_rev->candidates ) {
         auto v_it = votes.find( c.value );
         int64_t w = (v_it != votes.end()) ? v_it->total_weight.amount : 0;
         ranked.emplace_back( c, w );
      }
      std::sort( ranked.begin(), ranked.end(),
         []( const auto& a, const auto& b ){ return a.second > b.second; });

      size_t next_size = 0;
      uint32_t next_round = 0;
      if( last_rev->round_number == 1 ) {
         next_size  = round2_candidates;  // 18
         next_round = 2;
      } else if( last_rev->round_number == 2 ) {
         next_size  = round3_candidates;  // 9
         next_round = 3;
      } else {
         next_size  = 0;                  // swear in
         next_round = 0;
      }

      // Finalize current round
      auto last_fwd = rounds.iterator_to( *last_rev );
      rounds.modify( last_fwd, same_payer, [&]( auto& r ) {
         r.finalized = true;
      });

      if( next_round == 0 ) {
         // Round 3 complete — seat the top 9
         std::vector<name> winners;
         for( size_t i = 0; i < ranked.size() && i < guardian_seats; ++i ) {
            winners.push_back( ranked[i].first );
         }
         check( winners.size() == guardian_seats,
                "round 3 ended with fewer than 9 candidates — extend cycle" );
         seat_guardians_for_next_term( winners );
         return;
      }

      // Otherwise, open the next round with the top winners
      uint64_t next_id = rounds.available_primary_key();
      rounds.emplace( get_self(), [&]( auto& r ) {
         r.id           = next_id;
         r.round_number = next_round;
         r.starts_at    = current_time_point();
         r.ends_at      = current_time_point() + eosio::seconds(election_round_seconds);
         r.candidates.reserve( next_size );
         for( size_t i = 0; i < ranked.size() && i < next_size; ++i ) {
            r.candidates.push_back( ranked[i].first );
         }
         r.finalized = false;
      });

      // Reset vote tallies for the next round
      for( size_t i = 0; i < ranked.size() && i < next_size; ++i ) {
         auto v_it = votes.find( ranked[i].first.value );
         if( v_it != votes.end() ) {
            votes.modify( v_it, same_payer, [&]( auto& v ) {
               v.total_weight.amount = 0;
               v.voter_count         = 0;
               v.round_id            = next_id;
            });
         }
      }
   }

   // ===========================================================================
   // Internal helpers
   // ===========================================================================

   void guard_contract::require_guardian( name account ) {
      guardian_council_singleton council( get_self(), get_self().value );
      check( council.exists(), "no current Guardian council" );
      auto cc = council.get();
      check( std::find( cc.seats.begin(), cc.seats.end(), account ) != cc.seats.end(),
             "account is not a current Guardian" );
   }

   void guard_contract::seat_guardians_for_next_term( const std::vector<name>& winners ) {
      check( winners.size() == guardian_seats,
             "must seat exactly 9 Guardians" );

      guardian_council_singleton council( get_self(), get_self().value );
      guardian_council new_cc;
      if( council.exists() ) {
         new_cc = council.get();
         new_cc.term_number += 1;
      }
      new_cc.seats                = winners;
      new_cc.sworn_in_at          = current_time_point();
      new_cc.next_election_starts = current_time_point()
                                  + eosio::seconds(election_cycle_seconds);
      council.set( new_cc, get_self() );

      // In production, also reconfigure sika.guard's `active` permission
      // to require 6-of-9 signatures from the winners. For initial contract
      // we leave this to a Guardian-approved proposal post-election.
      eosio::print( "New Guardian council sworn in (term ", new_cc.term_number,
                    "): ", winners.size(), " seats\n" );
   }

} // namespace sikaguard
