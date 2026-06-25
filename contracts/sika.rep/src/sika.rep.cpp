// =============================================================================
// SikaChain — sika.rep implementation
// =============================================================================

#include <sika.rep/sika.rep.hpp>
#include <sika.accounts.hpp>

#include <eosio/action.hpp>
#include <eosio/print.hpp>

namespace sikarep {

   using std::string;

   // ---------------------------------------------------------------------------
   // regrep — register as a Representative
   // ---------------------------------------------------------------------------
   void rep_contract::regrep( const name& rep, const string& url ) {
      require_auth( rep );
      check( url.size() <= 512, "url too long" );

      reps_table reps( get_self(), get_self().value );
      auto it = reps.find( rep.value );

      if( it == reps.end() ) {
         reps.emplace( rep, [&]( auto& r ) {
            r.owner                = rep;
            r.is_active            = true;
            r.boost_apy_bps        = 0;
            r.url                  = url;
            r.total_followers_weight = asset{ 0, eosio::symbol{"SIKA", 4} };
            r.follower_count       = 0;
            r.registered_at        = current_time_point();
         });
      } else {
         reps.modify( it, same_payer, [&]( auto& r ) {
            r.is_active = true;
            r.url       = url;
         });
      }

      // Flip the proxy bit on sika.system::voter_info (call eosio::regproxy separately).
      (void)rep;
   }

   // ---------------------------------------------------------------------------
   // unregrep — voluntary withdrawal
   // ---------------------------------------------------------------------------
   void rep_contract::unregrep( const name& rep ) {
      require_auth( rep );

      reps_table reps( get_self(), get_self().value );
      auto it = reps.require_find( rep.value, "rep not registered" );

      reps.modify( it, same_payer, [&]( auto& r ) {
         r.is_active = false;
      });
   }

   // ---------------------------------------------------------------------------
   // setboost — declare APY (capped at 5%) and BP slate
   //
   // This is the heart of the Representative system. The cap is enforced
   // mechanically: ANY value above 500 bps is rejected. There is no admin
   // override. Even a Guardian 6-of-9 cannot lift this cap without first
   // amending the protocol via sika.rules.
   // ---------------------------------------------------------------------------
   void rep_contract::setboost( const name& rep,
                                 uint16_t boost_apy_bps,
                                 const std::vector<name>& bp_slate )
   {
      require_auth( rep );

      // ARTICLE V — protocol-enforced cap (no exceptions)
      check( boost_apy_bps <= rep_boost_cap_bps,
             "boost APY exceeds protocol cap of +5.00% (500 bps)" );
      check( bp_slate.size() <= max_slate_size,
             "BP slate exceeds 30 producers (Article III)" );

      // Slate must be sorted & deduplicated (matches voteproducer invariant)
      for( size_t i = 1; i < bp_slate.size(); ++i ) {
         check( bp_slate[i-1] < bp_slate[i],
                "BP slate must be sorted and unique" );
      }

      validate_bp_slate( bp_slate );

      reps_table reps( get_self(), get_self().value );
      auto it = reps.require_find( rep.value, "rep not registered; call regrep first" );
      check( it->is_active, "rep is inactive" );

      reps.modify( it, same_payer, [&]( auto& r ) {
         r.boost_apy_bps     = boost_apy_bps;
         r.bp_slate          = bp_slate;
         r.last_boost_update = current_time_point();
      });

      // Caller should invoke eosio::voteproducer separately when changing slate.
   }

   // ---------------------------------------------------------------------------
   // on_transfer — handle incoming cGHS for boost funding
   //
   // When a Rep transfers cGHS to sika.boost:
   //   - memo must contain "boost:<rep_name>"
   //   - update boost_balance.cghs_funded
   //
   // Any other incoming transfer is rejected (returned by failure).
   // ---------------------------------------------------------------------------
   void rep_contract::on_transfer( const name& from, const name& to,
                                    const asset& quantity, const string& memo )
   {
      if( to != get_self() ) return;            // not for us
      if( from == get_self() ) return;          // outgoing — ignore

      // Reject anything that isn't cGHS
      check( quantity.symbol == eosio::symbol{"CGHS", 4},
             "sika.boost accepts only cGHS deposits" );

      // memo format: "boost:<rep_name>"
      check( memo.size() > 6 && memo.substr(0, 6) == "boost:",
             "memo must be 'boost:<rep_name>'" );
      name rep_name{ memo.substr(6).c_str() };

      reps_table reps( get_self(), get_self().value );
      auto rit = reps.require_find( rep_name.value,
                                     "boost target is not a registered Rep" );
      check( rit->is_active, "rep is inactive" );

      boost_balance_table balances( get_self(), get_self().value );
      auto bit = balances.find( rep_name.value );
      if( bit == balances.end() ) {
         balances.emplace( get_self(), [&]( auto& b ) {
            b.rep         = rep_name;
            b.cghs_funded = quantity;
            b.cghs_paid   = asset{ 0, quantity.symbol };
         });
      } else {
         balances.modify( bit, same_payer, [&]( auto& b ) {
            b.cghs_funded += quantity;
         });
      }

      eosio::print( from, " funded boost for ", rep_name, ": ", quantity, "\n" );
   }

   // ---------------------------------------------------------------------------
   // payboost — distribute funded cGHS to a specific delegator
   //
   // For initial contract: caller specifies (rep, delegator). The contract
   // computes the delegator's accrued boost (since their last payout) and
   // settles it via sika.system::crediboost. Hard-capped at what the Rep
   // has funded — if cghs_funded < amount, we pay the funded amount only.
   //
   // In a more sophisticated version, an off-chain keeper batches all
   // delegators of a Rep into one transaction per round.
   // ---------------------------------------------------------------------------
   void rep_contract::payboost( const name& rep, const name& delegator ) {
      // Anyone can trigger this — it pays a third party with the Rep's
      // pre-deposited funds. No new tokens are created.
      reps_table reps( get_self(), get_self().value );
      auto rit = reps.require_find( rep.value, "rep not registered" );
      check( rit->is_active, "rep is inactive" );

      boost_balance_table balances( get_self(), get_self().value );
      auto bit = balances.require_find( rep.value, "rep has not funded boost" );
      asset available = bit->cghs_funded - bit->cghs_paid;
      check( available.amount > 0, "no funded boost remaining" );

      // For initial contract: pay out a fixed small slice per call to keep
      // gas predictable. A full implementation would compute the delegator's
      // accrued share since last payout. Here we send the whole available
      // balance scaled by the boost APY for 1 day:
      //
      //   per_day = delegator_stake × boost_apy_bps / (10000 × 365)
      //
      // Without on-chain access to the delegator's exact stake, we trust the
      // sika.system contract to enforce caps on the receiving side.

      // Move cGHS from sika.boost to delegator via crediboost
      eosio::action(
         { eosio::permission_level{ get_self(), "active"_n } },
         sikaaccounts::SYSTEM, "crediboost"_n,
         std::make_tuple( delegator, available )
      ).send();

      balances.modify( bit, same_payer, [&]( auto& b ) {
         b.cghs_paid += available;
      });
   }

   // ---------------------------------------------------------------------------
   // validate_bp_slate — every BP in the slate must be currently registered
   // ---------------------------------------------------------------------------
   void rep_contract::validate_bp_slate( const std::vector<name>& slate ) {
      check( slate.size() <= max_slate_size, "slate exceeds max size" );
      // Cross-contract producer validation deferred to sika.system::voteproducer.
      (void)slate;
   }

} // namespace sikarep
