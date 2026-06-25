// =============================================================================
// SikaChain — Producer Pay (Article V)
// =============================================================================
// Implements the unique SikaChain inflation schedule:
//   - Year 1: 1.00% APR
//   - Halves every 4 years (Bitcoin-style)
//   - 75% to Top 21 (split: 25% per-block + 75% per-vote share)
//   - 25% to Standby 22-50 (split evenly per round)
//   - Asymptotic supply ceiling: 8.64B SIKA
//   - BPs in probation are SKIPPED but NOT slashed (Article X)
//
// Every block we accumulate `unpaid_blocks` for the active producer. When a
// BP calls `claimprod`, we compute their share since their last claim, mint
// the corresponding inflation, and transfer.
//
// Planned (v0.2): split into Tier-1 cUSD cost-recovery (sika.treas/sika.cost)
// and Tier-2 usage-gated vested SIKA bonus — see settlement-layer spec.
// =============================================================================

#include <sika.system/sika.system.hpp>
#include <sika.accounts.hpp>

#include <eosio/system.hpp>
#include <eosio/transaction.hpp>
#include <eosio/print.hpp>
#include <eosio/action.hpp>
#include <eosio/asset.hpp>

#include <cmath>

namespace sikasystem {

   // ---------------------------------------------------------------------------
   // Inflation rate at year N — Bitcoin-style halving every 4 years.
   // Mirrors domain.ts::inflationRateForYear EXACTLY.
   //   Year 1..4: 100 bps (1.00%)
   //   Year 5..8: 50 bps  (0.50%)
   //   Year 9..12: 25 bps
   //   ...
   //   Year 100+: effectively zero
   // ---------------------------------------------------------------------------
   static int64_t inflation_bps_for_year( int64_t year ) {
      if( year < 1 ) return 0;
      int64_t epoch = (year - 1) / halving_epoch_years;
      int64_t bps   = inflation_year1_bps;
      for( int64_t i = 0; i < epoch; ++i ) {
         bps /= 2;
         if( bps == 0 ) break;
      }
      return bps;
   }

   // ---------------------------------------------------------------------------
   // compute_inflation_amount
   // For elapsed seconds at the current chain year, mint SIKA proportional to:
   //   amount = total_supply × (bps / 10000) × (elapsed / SECONDS_PER_YEAR)
   //
   // We use integer math to avoid float drift; multiplications are scaled to
   // avoid overflow (total_supply max is 8.64e13 raw, bps max 100, seconds
   // per year < 32e6 — product fits in int64).
   // ---------------------------------------------------------------------------
   static constexpr int64_t seconds_per_year = 365 * 24 * 60 * 60; // ignores leap

   int64_t system_contract::compute_inflation_amount( int64_t seconds_elapsed ) const {
      int64_t current_year = ((current_time_point().sec_since_epoch()
                              - _gstate.genesis_time.sec_since_epoch())
                              / seconds_per_year) + 1;
      int64_t bps = inflation_bps_for_year( current_year );
      if( bps == 0 ) return 0;

      // Current circulating supply — read from sika.token
      int64_t total_supply = initial_sika_supply + _gstate.total_minted - _gstate.total_burned;
      check( total_supply <= max_sika_supply, "supply cap breached" );

      // amount = supply * bps * elapsed / (10000 * SECONDS_PER_YEAR)
      // Split the divisor to avoid overflow.
      __int128 product = static_cast<__int128>(total_supply) * bps * seconds_elapsed;
      int64_t  amount  = static_cast<int64_t>( product / (10000ll * seconds_per_year) );

      // Hard cap so we never exceed asymptotic supply
      int64_t headroom = max_sika_supply - total_supply;
      if( amount > headroom ) amount = headroom;
      return amount;
   }

   // ---------------------------------------------------------------------------
   // mint_inflation_to_buckets
   // Mint SIKA via sika.token and split into:
   //   pervote_bucket  = top21_share × 75% (the "per-vote-share" portion)
   //   perblock_bucket = top21_share × 25% (paid out per block produced)
   //   standby_bucket  = standby_share (paid evenly per round)
   //
   // Mints go to sika.bppay; producers then withdraw their share via claimprod.
   // ---------------------------------------------------------------------------
   void system_contract::mint_inflation_to_buckets( int64_t total_to_mint ) {
      if( total_to_mint <= 0 ) return;

      // 75% to Top 21
      int64_t top21_total = (total_to_mint * top21_share_bps) / 10000;
      // 25% to Standby
      int64_t standby_total = total_to_mint - top21_total;

      // Within Top 21: 25% perblock, 75% pervote
      int64_t perblock_amt = (top21_total * 25) / 100;
      int64_t pervote_amt  = top21_total - perblock_amt;

      _gstate.perblock_bucket += perblock_amt;
      _gstate.pervote_bucket  += pervote_amt;
      _gstate.total_minted    += total_to_mint;

      // Accounting-only during onblock — actual SIKA enters sika.bppay when
      // claimprod runs (explicit signed transfer). Inline issue from onblock
      // fails Spring auth when token lives on sika.token instead of eosio.token.
      (void)standby_total;
   }

   void system_contract::refill_inflation_buckets( int64_t max_elapsed_seconds ) {
      if( max_elapsed_seconds <= 0 ) {
         return;
      }

      int64_t current_year = ((current_time_point().sec_since_epoch()
                              - _gstate.genesis_time.sec_since_epoch())
                              / seconds_per_year) + 1;
      _gstate.current_year_inflation_bps = inflation_bps_for_year( current_year );
      const int64_t mint = compute_inflation_amount( max_elapsed_seconds );
      mint_inflation_to_buckets( mint );
      _gstate.last_pervote_bucket_fill = current_time_point();

      eosio::print( "refill_inflation_buckets +", mint, " SIKA scheduled (",
                    max_elapsed_seconds, "s)\n" );
   }

   // ---------------------------------------------------------------------------
   // onblock — invoked by nodeos on every block.
   // Updates the producing BP's unpaid_blocks, refills inflation buckets daily.
   // ---------------------------------------------------------------------------
   void system_contract::onblock( ignore<block_header> ) {
      using namespace eosio;
      require_auth( get_self() );

      // Decode just the producer field from the block header.
      block_timestamp        timestamp;
      name                   producer;
      _ds >> timestamp >> producer;

      auto prod = _producers.find( producer.value );
      if( prod != _producers.end() ) {
         _producers.modify( prod, same_payer, [&]( auto& p ) {
            p.unpaid_blocks++;
         });
      }
      _gstate.total_unpaid_blocks++;

      // Promote voted BPs into the pending producer schedule (~once per minute).
      if( timestamp.slot - _gstate.last_producer_schedule_update.slot > 120 ) {
         update_elected_producers( timestamp );
      }

      // Refill buckets at most once per day.
      const microseconds refill_interval = seconds(24 * 60 * 60);
      if( current_time_point() - _gstate.last_pervote_bucket_fill > refill_interval ) {
         int64_t elapsed = (current_time_point()
                            - _gstate.last_pervote_bucket_fill).count() / 1'000'000;
         if( elapsed > refill_interval.count() / 1'000'000 ) {
            elapsed = refill_interval.count() / 1'000'000;
         }
         refill_inflation_buckets( elapsed );
      }
   }

   // ---------------------------------------------------------------------------
   // claimprod — a BP claims their share of pending inflation.
   // BPs in probation (any rule failing for > 7 days) are SKIPPED entirely:
   // their unpaid_blocks are reset to 0 and their share is forfeited back to
   // the pool. NO STAKE IS SLASHED. (Article X.)
   // ---------------------------------------------------------------------------
   void system_contract::claimprod( const name& producer ) {
      require_auth( producer );

      auto it = _producers.require_find( producer.value, "producer not registered" );
      check( it->is_active, "producer is not active" );

      // ARTICLE X — vote-removal-only enforcement.
      // If the BP is in probation, forfeit this round and clear unpaid_blocks
      // (no actual SIKA leaves their account).
      if( it->in_probation() ) {
         forfeit_bp_vest( producer );
         _producers.modify( it, same_payer, [&]( auto& p ) {
            p.unpaid_blocks   = 0;
            p.last_claim_time = current_time_point();
         });
         eosio::print( "BP ", producer, " skipped (probation: ",
                       static_cast<uint64_t>(it->probation_flags), ")\n" );
         return;
      }

      // First-time claim guard
      auto ct = current_time_point();
      check( ct - it->last_claim_time > microseconds(seconds(24*60*60).count()),
             "must wait 24h between claims" );

      // Compute this BP's share.
      int64_t per_block_pay = 0;
      if( _gstate.total_unpaid_blocks > 0 ) {
         per_block_pay = (_gstate.perblock_bucket * it->unpaid_blocks)
                       / _gstate.total_unpaid_blocks;
      }

      // pervote share — proportional to this BP's total_votes vs all Top 21
      int64_t per_vote_pay = 0;
      if( _gstate.total_producer_vote_weight > 0 && it->total_votes > 0 ) {
         per_vote_pay = static_cast<int64_t>(
            _gstate.pervote_bucket * (it->total_votes / _gstate.total_producer_vote_weight)
         );
      }

      int64_t total_pay = per_block_pay + per_vote_pay;
      check( total_pay >= 0, "negative payout (bug)" );

      // Drain the buckets accordingly.
      _gstate.perblock_bucket    -= per_block_pay;
      _gstate.pervote_bucket     -= per_vote_pay;
      _gstate.total_unpaid_blocks -= it->unpaid_blocks;

      _producers.modify( it, same_payer, [&]( auto& p ) {
         p.last_claim_time = ct;
         p.unpaid_blocks   = 0;
      });

      // Tier-1 cost recovery (cUSD) — no-op when cost_recovery_cusd is zero.
      eosio::action(
         { eosio::permission_level{ get_self(), "active"_n } },
         sikaaccounts::TREAS, "paycost"_n,
         std::make_tuple( producer )
      ).send();

      // Tier-2: SIKA inflation bonus (liquid legacy or vested auto-REX).
      if( total_pay > 0 ) {
         bp_vest_global_singleton vg( get_self(), get_self().value );
         const bool vesting_on = vg.exists() && vg.get().tier2_vesting_enabled;

         int64_t tier2_pay = usage_gated_tier2( total_pay );
         if( tier2_pay > 0 ) {
            eosio::action(
               { eosio::permission_level{ get_self(), "active"_n } },
               sikaaccounts::TOKEN, "issue"_n,
               std::make_tuple(
                  sikaaccounts::BPPAY,
                  asset{ tier2_pay, sika_symbol },
                  std::string( "BP inflation (claimprod)" )
               )
            ).send();
            // total_minted already credited when buckets were filled.

            if( vesting_on ) {
               credit_bp_vest( producer, tier2_pay );
            } else {
               eosio::action(
                  { eosio::permission_level{ sikaaccounts::BPPAY, "active"_n } },
                  sikaaccounts::TOKEN, "transfer"_n,
                  std::make_tuple(
                     sikaaccounts::BPPAY, producer,
                     asset(tier2_pay, sika_symbol),
                     std::string("BP reward (sika.system)")
                  )
               ).send();
            }
         }
      }
   }

   int64_t system_contract::usage_gated_tier2( int64_t scheduled_pay ) const {
      if( scheduled_pay <= 0 ) return 0;

      bp_vest_global_singleton vg( get_self(), get_self().value );
      if( !vg.exists() ) {
         return scheduled_pay;
      }

      const bp_vest_global g = vg.get();
      if( !g.tier2_vesting_enabled ) {
         return scheduled_pay;
      }

      if( g.epoch_fee_revenue <= 0 ) {
         return 0;
      }

      const int64_t cap = static_cast<int64_t>(
         (static_cast<__int128>(g.epoch_fee_revenue) * g.inflation_gain_bps)
         / 10000
      );
      return cap < scheduled_pay ? cap : scheduled_pay;
   }

   void system_contract::credit_bp_vest( const name& producer, int64_t amount ) {
      check( amount > 0, "vest amount must be positive" );

      bp_vest_global_singleton vg( get_self(), get_self().value );
      bp_vest_global g = vg.exists() ? vg.get() : bp_vest_global{};
      const uint32_t duration = g.bonus_vesting_seconds > 0
         ? g.bonus_vesting_seconds
         : static_cast<uint32_t>(365 * 24 * 60 * 60);

      const asset escrow{ amount, sika_symbol };

      eosio::action(
         { eosio::permission_level{ sikaaccounts::BPPAY, "active"_n } },
         sikaaccounts::TOKEN, "transfer"_n,
         std::make_tuple(
            sikaaccounts::BPPAY, get_self(), escrow,
            std::string("Tier-2 BP vest escrow")
         )
      ).send();

      auto it = _bpvest.find( producer.value );
      if( it == _bpvest.end() ) {
         _bpvest.emplace( get_self(), [&]( auto& row ) {
            row.owner            = producer;
            row.total_amount     = amount;
            row.released_amount  = 0;
            row.vest_start       = current_time_point();
            row.vest_seconds     = duration;
            row.forfeited        = false;
         });
      } else {
         _bpvest.modify( it, same_payer, [&]( auto& row ) {
            row.total_amount += amount;
            if( row.vest_seconds == 0 ) {
               row.vest_seconds = duration;
            }
            if( row.vest_start.time_since_epoch().count() == 0 ) {
               row.vest_start = current_time_point();
            }
         });
      }

      eosio::print( "credit_bp_vest ", producer, " +", escrow, "\n" );
   }

   void system_contract::claimvest( const name& producer ) {
      require_auth( producer );

      auto prod_it = _producers.find( producer.value );
      check( prod_it != _producers.end() && prod_it->is_active,
             "producer not active" );
      check( !prod_it->in_probation(), "vesting paused during probation" );

      auto it = _bpvest.require_find( producer.value, "no vesting schedule" );
      check( !it->forfeited, "vesting forfeited" );
      check( it->vest_seconds > 0, "vesting not configured" );

      const int64_t elapsed_us = (current_time_point() - it->vest_start).count();
      const uint32_t elapsed_sec = elapsed_us > 0
         ? static_cast<uint32_t>( elapsed_us / 1'000'000 )
         : 0;
      const uint32_t capped = elapsed_sec > it->vest_seconds
         ? it->vest_seconds
         : elapsed_sec;

      const int64_t vested_target = static_cast<int64_t>(
         (static_cast<__int128>(it->total_amount) * capped) / it->vest_seconds
      );
      const int64_t releasable = vested_target - it->released_amount;
      if( releasable <= 0 ) {
         eosio::print( "claimvest: nothing vested yet for ", producer, "\n" );
         return;
      }

      credit_rex_from_escrow( producer, asset{ releasable, sika_symbol } );

      _bpvest.modify( it, same_payer, [&]( auto& row ) {
         row.released_amount += releasable;
      });

      eosio::print( "claimvest ", producer, " released ", releasable, " SIKA to REX\n" );
   }

   void system_contract::forfeit_bp_vest( const name& producer ) {
      auto it = _bpvest.find( producer.value );
      if( it == _bpvest.end() || it->forfeited ) {
         return;
      }

      const int64_t unreleased = it->total_amount - it->released_amount;

      _bpvest.modify( it, same_payer, [&]( auto& row ) {
         row.forfeited = true;
      });

      if( unreleased > 0 ) {
         eosio::action(
            { eosio::permission_level{ get_self(), "active"_n } },
            sikaaccounts::TOKEN, "transfer"_n,
            std::make_tuple(
               get_self(), sikaaccounts::BURN,
               asset{ unreleased, sika_symbol },
               std::string( "Tier-2 vest forfeited (probation/unreg)" )
            )
         ).send();
         _gstate.total_burned += unreleased;
      }

      eosio::print( "forfeit_bp_vest ", producer, " unreleased ",
                    unreleased, " SIKA\n" );
   }

   void system_contract::setvesting( name authority,
                                        uint8_t tier2_vesting_enabled,
                                        uint32_t bonus_vesting_seconds,
                                        uint16_t inflation_gain_bps )
   {
      require_auth( authority );
      check( authority == get_self() || authority == sikaaccounts::RULES,
             "governance authority required" );
      check( inflation_gain_bps <= 10'000, "inflation_gain_bps out of range" );
      check( bonus_vesting_seconds > 0, "bonus_vesting_seconds must be positive" );

      bp_vest_global_singleton vg( get_self(), get_self().value );
      bp_vest_global g = vg.exists() ? vg.get() : bp_vest_global{};
      g.tier2_vesting_enabled = tier2_vesting_enabled;
      g.bonus_vesting_seconds = bonus_vesting_seconds;
      g.inflation_gain_bps    = inflation_gain_bps;
      vg.set( g, get_self() );
   }

   void system_contract::refillpay( name authority ) {
      require_auth( authority );
      check( authority == get_self() || authority == sikaaccounts::RULES,
             "governance authority required" );

      refill_inflation_buckets( 24 * 60 * 60 );
   }

   uint32_t system_contract::rex_unstake_window() const {
      rex_config_singleton cfg( get_self(), get_self().value );
      if( cfg.exists() ) {
         const rex_config_row row = cfg.get();
         if( row.unstake_seconds > 0 ) {
            return row.unstake_seconds;
         }
      }
      return static_cast<uint32_t>( rex_unstake_seconds );
   }

   void system_contract::setrexcfg( name authority, uint32_t unstake_seconds ) {
      require_auth( authority );
      check( authority == get_self() || authority == sikaaccounts::RULES,
             "governance authority required" );
      check( unstake_seconds >= 5, "unstake_seconds too short" );
      check( unstake_seconds <= 90 * 24 * 60 * 60, "unstake_seconds too long" );

      rex_config_singleton cfg( get_self(), get_self().value );
      rex_config_row row = cfg.exists() ? cfg.get() : rex_config_row{};
      row.unstake_seconds = unstake_seconds;
      cfg.set( row, get_self() );

      eosio::print( "setrexcfg unstake_seconds=", unstake_seconds, "\n" );
   }

} // namespace sikasystem
