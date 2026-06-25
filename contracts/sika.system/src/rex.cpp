// =============================================================================
// SikaChain — REX (Article V)
// =============================================================================
// SikaChain's defining REX innovation: users stake SIKA, but yield is paid
// in cGHS (the network's GHS-pegged stablecoin). This isolates yield from
// SIKA price volatility, making it more attractive for African users who
// think in cedis, not crypto.
//
// Where the cGHS yield comes from:
//   1. Rep boost / fee revenue → reference-unit yield pool (cash leg)
//   2. 50% RAM Bancor fee → SIKA REX compound (SIKA leg — v0.2 §6.2)
//
// Boost mechanics:
//   - A voter who delegates to a Rep receives base APY + Rep boost
//   - Boost is HARD CAPPED at +5.00% (rep_boost_cap_bps = 500)
//   - Boost is paid in cGHS, sourced from sika.boost (which the Rep funds)
//   - claimrewards() pays out base + capped boost in a single transfer
//
// 7-day unstake window applies on sellrex — matches CPU/NET refund window.
// =============================================================================

#include <sika.system/sika.system.hpp>
#include <sika.accounts.hpp>
#include <sika.fx.hpp>

#include <eosio/action.hpp>
#include <eosio/print.hpp>
#include <eosio/system.hpp>

namespace sikasystem {

   // ---------------------------------------------------------------------------
   // deposit — used internally before buyrex to move SIKA into REX-controlled
   // custody. In the simplified MVP we accept that the user has already
   // approved the transfer; this is a bookkeeping action.
   // ---------------------------------------------------------------------------
   void system_contract::deposit( const name& owner, const asset& amount ) {
      require_auth( owner );
      check( amount.symbol == sika_symbol, "deposit must be in SIKA" );
      check( amount.amount > 0, "must deposit a positive amount" );

      // Pull SIKA from the user into sika.rex custody
      eosio::action(
         { eosio::permission_level{ owner, "active"_n } },
         sikaaccounts::TOKEN, "transfer"_n,
         std::make_tuple( owner, sikaaccounts::REX, amount,
                          std::string("REX deposit") )
      ).send();
   }

   void system_contract::withdraw( const name& owner, const asset& amount ) {
      require_auth( owner );
      check( amount.symbol == sika_symbol, "withdraw must be in SIKA" );
      check( amount.amount > 0, "must withdraw a positive amount" );

      // Inverse of deposit: return SIKA from REX custody to the owner.
      eosio::action(
         { eosio::permission_level{ sikaaccounts::REX, "active"_n } },
         sikaaccounts::TOKEN, "transfer"_n,
         std::make_tuple( sikaaccounts::REX, owner, amount,
                          std::string("REX withdraw") )
      ).send();
   }

   // ---------------------------------------------------------------------------
   // buyrex — convert SIKA into REX shares.
   // REX shares represent a proportional claim on the cGHS yield pool.
   // ---------------------------------------------------------------------------
   void system_contract::buyrex( const name& from, const asset& amount ) {
      require_auth( from );
      check( amount.symbol == sika_symbol, "must buy REX with SIKA" );
      check( amount.amount > 0, "amount must be positive" );

      auto pool_it = _rexpool.begin();
      check( pool_it != _rexpool.end(),
             "REX pool not initialized; call setrex first" );

      eosio::action(
         { eosio::permission_level{ from, "active"_n } },
         sikaaccounts::TOKEN, "transfer"_n,
         std::make_tuple( from, sikaaccounts::REX, amount,
                          std::string("buyrex") )
      ).send();

      apply_rex_stake( from, amount );
   }

   // Credit REX shares after SIKA is already in sika.rex custody.
   void system_contract::apply_rex_stake( const name& owner, const asset& amount ) {
      check( amount.symbol == sika_symbol && amount.amount > 0,
             "invalid REX stake amount" );

      auto pool_it = _rexpool.begin();
      check( pool_it != _rexpool.end(), "REX pool missing" );

      asset rex_to_mint{ 0, symbol{"REX", 4} };
      if( pool_it->total_lendable.amount == 0 || pool_it->total_rex.amount == 0 ) {
         // First stake, or pool has SIKA from RAM-fee compound without REX shares yet.
         rex_to_mint = asset{ amount.amount, symbol{"REX", 4} };
      } else {
         int64_t shares = static_cast<int64_t>(
            (static_cast<__int128>(amount.amount) * pool_it->total_rex.amount)
            / pool_it->total_lendable.amount
         );
         rex_to_mint = asset{ shares, symbol{"REX", 4} };
      }

      _rexpool.modify( pool_it, same_payer, [&]( auto& p ) {
         p.total_unlent.amount    += amount.amount;
         p.total_lendable.amount  += amount.amount;
         p.total_rex.amount       += rex_to_mint.amount;
      });

      auto bal_it = _rexbalance.find( owner.value );
      if( bal_it == _rexbalance.end() ) {
         _rexbalance.emplace( owner, [&]( auto& b ) {
            b.owner                  = owner;
            b.vote_stake             = amount;
            b.rex_balance            = rex_to_mint;
            b.cghs_claimed_lifetime  = asset{ 0, cghs_symbol };
         });
      } else {
         _rexbalance.modify( bal_it, same_payer, [&]( auto& b ) {
            b.vote_stake.amount  += amount.amount;
            b.rex_balance.amount += rex_to_mint.amount;
         });
      }

      auto voter_it = _voters.find( owner.value );
      if( voter_it != _voters.end() ) {
         _voters.modify( voter_it, same_payer, [&]( auto& v ) {
            v.staked += amount.amount;
         });
         propagate_weight_change( *voter_it );
      }

      eosio::print( "rex stake: ", owner, " +", amount, " → ", rex_to_mint, "\n" );
   }

   void system_contract::compound_rex_sika( const asset& amount ) {
      check( amount.symbol == sika_symbol && amount.amount > 0,
             "invalid compound amount" );

      auto pool_it = _rexpool.begin();
      check( pool_it != _rexpool.end(), "REX pool missing" );

      eosio::action(
         { eosio::permission_level{ get_self(), "active"_n } },
         sikaaccounts::TOKEN, "transfer"_n,
         std::make_tuple( get_self(), sikaaccounts::REX, amount,
                          std::string("RAM fee → REX compound (v0.2)") )
      ).send();

      _rexpool.modify( pool_it, same_payer, [&]( auto& p ) {
         p.total_unlent.amount    += amount.amount;
         p.total_lendable.amount  += amount.amount;
      });

      eosio::print( "compound_rex_sika +", amount, "\n" );
   }

   void system_contract::credit_rex_from_bppay( const name& owner,
                                                const asset& amount )
   {
      check( amount.symbol == sika_symbol && amount.amount > 0,
             "invalid bppay REX credit" );

      eosio::action(
         { eosio::permission_level{ sikaaccounts::BPPAY, "active"_n } },
         sikaaccounts::TOKEN, "transfer"_n,
         std::make_tuple( sikaaccounts::BPPAY, sikaaccounts::REX, amount,
                          std::string("Tier-2 vest → REX") )
      ).send();

      apply_rex_stake( owner, amount );
   }

   void system_contract::credit_rex_from_escrow( const name& owner,
                                                  const asset& amount )
   {
      check( amount.symbol == sika_symbol && amount.amount > 0,
             "invalid escrow REX credit" );

      eosio::action(
         { eosio::permission_level{ get_self(), "active"_n } },
         sikaaccounts::TOKEN, "transfer"_n,
         std::make_tuple( get_self(), sikaaccounts::REX, amount,
                          std::string("Tier-2 vest release → REX") )
      ).send();

      apply_rex_stake( owner, amount );
   }

   // ---------------------------------------------------------------------------
   // sellrex — convert REX shares back to SIKA. Enters 7-day cool-down via
   // refunds table, not paid out immediately. Mirrors CPU/NET refund flow.
   // ---------------------------------------------------------------------------
   void system_contract::sellrex( const name& from, const asset& rex ) {
      require_auth( from );
      check( rex.symbol == symbol{"REX", 4}, "must sell REX shares" );
      check( rex.amount > 0, "amount must be positive" );

      auto bal_it = _rexbalance.require_find( from.value, "no REX balance" );
      check( bal_it->rex_balance.amount >= rex.amount,
             "insufficient REX shares" );

      auto pool_it = _rexpool.begin();
      check( pool_it != _rexpool.end(), "REX pool missing" );

      // SIKA owed = rex × total_lendable / total_rex
      int64_t sika_amt = static_cast<int64_t>(
         (static_cast<__int128>(rex.amount) * pool_it->total_lendable.amount)
         / pool_it->total_rex.amount
      );
      asset sika_owed{ sika_amt, sika_symbol };

      // Update pool
      _rexpool.modify( pool_it, same_payer, [&]( auto& p ) {
         p.total_unlent.amount   -= sika_amt;
         p.total_lendable.amount -= sika_amt;
         p.total_rex.amount      -= rex.amount;
      });

      // Reduce user's balances
      _rexbalance.modify( bal_it, same_payer, [&]( auto& b ) {
         b.rex_balance.amount -= rex.amount;
         b.vote_stake.amount  -= sika_amt;
      });

      // Voter weight drops accordingly
      auto voter_it = _voters.find( from.value );
      if( voter_it != _voters.end() ) {
         _voters.modify( voter_it, same_payer, [&]( auto& v ) {
            v.staked -= sika_amt;
         });
         propagate_weight_change( *voter_it );
      }

      // Park SIKA in refund queue with 7-day cool-down
      refunds_table refunds( get_self(), from.value );
      auto rit = refunds.find( from.value );
      if( rit == refunds.end() ) {
         refunds.emplace( from, [&]( auto& r ) {
            r.owner        = from;
            r.request_time = current_time_point();
            r.net_amount   = asset{ 0, sika_symbol };
            r.cpu_amount   = sika_owed;
         });
      } else {
         refunds.modify( rit, same_payer, [&]( auto& r ) {
            r.cpu_amount.amount += sika_amt;
            r.request_time = current_time_point();  // reset clock
         });
      }
   }

   // ---------------------------------------------------------------------------
   // crediboost — sika.rep credits Rep boost yield into the reference pool.
   // Local stable (4 decimals) is converted via sika.treas FX rates.
   // ---------------------------------------------------------------------------
   void system_contract::crediboost( const name& voter, const asset& local_amount ) {
      require_auth( sikaaccounts::REP );
      check( local_amount.symbol.precision() == 4, "boost must use 4-decimal stable" );
      check( local_amount.amount > 0, "boost must be positive" );

      const int64_t ref_atoms = sikafx::local_to_cusd_atoms(
         sikaaccounts::TREAS, local_amount.symbol, local_amount.amount );
      check( ref_atoms > 0, "boost converts to zero reference units" );

      auto pool_it = _rexpool.begin();
      check( pool_it != _rexpool.end(), "REX pool missing" );

      _rexpool.modify( pool_it, same_payer, [&]( auto& p ) {
         p.cghs_yield_pool.amount += ref_atoms;
      });

      eosio::print( "boost credited: ", voter, " ", local_amount,
                    " → ref ", ref_atoms, "\n" );
   }

   void system_contract::credyield( const asset& ref_amount ) {
      require_auth( sikaaccounts::TREAS );
      check( ref_amount.amount > 0, "yield credit must be positive" );
      check( ref_amount.symbol == cusd_symbol || ref_amount.symbol == cghs_symbol,
             "yield credit must be reference unit (CUSD/CGHS dev peg)" );

      auto pool_it = _rexpool.begin();
      check( pool_it != _rexpool.end(), "REX pool missing" );

      _rexpool.modify( pool_it, same_payer, [&]( auto& p ) {
         p.cghs_yield_pool.amount += ref_amount.amount;
      });

      eosio::print( "credyield +", ref_amount, " (pool ",
                    _rexpool.begin()->cghs_yield_pool, ")\n" );
   }

   void system_contract::accruepoch( const asset& quantity ) {
      check( has_auth( get_self() ) || has_auth( sikaaccounts::TREAS ),
             "accruepoch requires system or sika.treas" );
      if( has_auth( get_self() ) ) {
         require_auth( get_self() );
      } else {
         require_auth( sikaaccounts::TREAS );
      }

      check( quantity.amount > 0, "fee must be positive" );
      check( quantity.symbol == cusd_symbol || quantity.symbol == cghs_symbol,
             "epoch fee must be reference unit (CUSD/CGHS dev peg)" );

      bp_vest_global_singleton vg( get_self(), get_self().value );
      bp_vest_global g = vg.exists() ? vg.get() : bp_vest_global{};
      g.epoch_fee_revenue += quantity.amount;
      vg.set( g, get_self() );

      eosio::print( "accruepoch +", quantity, " (total ",
                    g.epoch_fee_revenue, ")\n" );
   }

   // ---------------------------------------------------------------------------
   // claimrexyld — reference-unit yield settled in the user's payout currency.
   //
   // Drains cghs_yield_pool (reference atoms) and calls sika.treas::clearyield
   // for FX / local-stable payout. Dev peg: 1 reference unit = 1 CGHS = 1 CUSD.
   // ---------------------------------------------------------------------------
   void system_contract::claimrexyld( const name& owner,
                                      const symbol& payout_currency,
                                      const name& market )
   {
      require_auth( owner );
      check( market.value != 0, "market required for payout compliance" );
      check( payout_currency != sika_symbol && payout_currency != symbol{"REX", 4},
             "invalid payout currency" );
      check( payout_currency.precision() == 4,
             "payout currency must have 4 decimals" );

      if( payout_currency == cghs_symbol ) {
         claimrewards( owner );
         return;
      }

      auto bal_it = _rexbalance.require_find( owner.value, "no REX balance" );
      check( bal_it->rex_balance.amount > 0, "no REX shares to claim against" );

      auto pool_it = _rexpool.begin();
      check( pool_it != _rexpool.end(), "REX pool missing" );
      check( pool_it->total_rex.amount > 0, "no REX outstanding" );
      check( pool_it->cghs_yield_pool.amount > 0, "no yield to claim" );

      int64_t payout = static_cast<int64_t>(
         (static_cast<__int128>(bal_it->rex_balance.amount)
          * pool_it->cghs_yield_pool.amount)
         / pool_it->total_rex.amount
      );

      check( payout > 0,
             "your share rounds to zero — wait for the pool to grow" );

      asset ref_payout{ payout, cghs_symbol };
      asset cusd_ref{ payout, cusd_symbol };

      _rexpool.modify( pool_it, same_payer, [&]( auto& p ) {
         p.cghs_yield_pool.amount -= payout;
      });

      _rexbalance.modify( bal_it, same_payer, [&]( auto& b ) {
         b.cghs_claimed_lifetime.amount += payout;
      });

      eosio::action(
         { eosio::permission_level{ get_self(), "active"_n } },
         sikaaccounts::TREAS, "clearyield"_n,
         std::make_tuple( owner, cusd_ref, payout_currency, market )
      ).send();

      eosio::print( "claimrexyld: ", owner, " ", ref_payout,
                    " as ", payout_currency, "\n" );
   }

   // ---------------------------------------------------------------------------
   // claimrewards — pay out a user's share of accumulated cGHS yield.
   //
   // Share = user_rex / total_rex × cghs_yield_pool
   //
   // If user delegates to a Rep, the Rep's boost is included (capped at +5%).
   // The cap is enforced at the source: sika.rep::setboost rejects values > 500.
   // So by the time cGHS arrives in cghs_yield_pool via crediboost, it's
   // already legal.
   //
   // No double-counting: we drain the pool by the amount paid, so subsequent
   // claimers get a smaller share until the next crediboost / RAM fee tops it
   // up.
   // ---------------------------------------------------------------------------
   void system_contract::claimrewards( const name& owner ) {
      require_auth( owner );

      auto bal_it = _rexbalance.require_find( owner.value, "no REX balance" );
      check( bal_it->rex_balance.amount > 0, "no REX shares to claim against" );

      auto pool_it = _rexpool.begin();
      check( pool_it != _rexpool.end(), "REX pool missing" );
      check( pool_it->total_rex.amount > 0, "no REX outstanding" );
      check( pool_it->cghs_yield_pool.amount > 0, "no yield to claim" );

      // user_share = (user_rex / total_rex) × yield_pool
      int64_t payout = static_cast<int64_t>(
         (static_cast<__int128>(bal_it->rex_balance.amount)
          * pool_it->cghs_yield_pool.amount)
         / pool_it->total_rex.amount
      );

      check( payout > 0,
             "your share rounds to zero — wait for the pool to grow" );

      asset cghs_payout{ payout, cghs_symbol };

      // Drain the pool
      _rexpool.modify( pool_it, same_payer, [&]( auto& p ) {
         p.cghs_yield_pool.amount -= payout;
      });

      // Track lifetime claimed (for analytics)
      _rexbalance.modify( bal_it, same_payer, [&]( auto& b ) {
         b.cghs_claimed_lifetime.amount += payout;
      });

      // Transfer cGHS from eosio custody (fund via tests with issue_cghs to eosio).
      eosio::action(
         { eosio::permission_level{ get_self(), "active"_n } },
         sikaaccounts::TOKEN, "transfer"_n,
         std::make_tuple( get_self(), owner, cghs_payout,
                          std::string("REX yield (sika.system)") )
      ).send();
   }

} // namespace sikasystem
