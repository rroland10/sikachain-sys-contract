// =============================================================================
// SikaChain — sika.rep (Representative registry, Article III & V)
// =============================================================================
// Deployed to: sika.rep
//
// Manages the Representative system — the renamed-and-improved "proxy"
// pattern from EOSIO. Differences from a vanilla proxy:
//
//   1. Reps publicly declare a yield BOOST (in basis points) they will pay
//      to delegators, capped at +5.00% APY (500 bps).
//   2. The cap is HARD-ENFORCED on-chain: setboost rejects values > 500.
//   3. Reps publish a BP_SLATE — the up-to-30 BPs they will vote for on
//      behalf of their delegators.
//   4. When delegators delegate to a Rep, their vote weight automatically
//      flows to the Rep's BP slate via eosio::voteproducer.
//   5. Reps fund their boost by transferring cGHS to sika.boost; this
//      contract inline-calls eosio::crediboost to register the credit.
//
// Why 5%? Because a higher cap turns Reps into Ponzi-style yield farms.
// 5% above base REX yield is meaningful but bounded. The Rules of SikaChain
// can amend this only via the sika.rules amendment process.
// =============================================================================

#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/singleton.hpp>
#include <eosio/time.hpp>

#include <string>
#include <vector>

namespace sikarep {

   using eosio::asset;
   using eosio::check;
   using eosio::current_time_point;
   using eosio::name;
   using eosio::same_payer;
   using eosio::singleton;
   using eosio::time_point;

   // ---------------------------------------------------------------------------
   // PROTOCOL CONSTANT — must match sika.system::rep_boost_cap_bps
   // ---------------------------------------------------------------------------
   static constexpr uint16_t rep_boost_cap_bps  = 500;   // +5.00% APY max
   static constexpr size_t   max_slate_size     = 30;    // ≤ 30 BPs per slate

   // ---------------------------------------------------------------------------
   // representative table — one row per registered Rep
   // ---------------------------------------------------------------------------
   struct [[eosio::table("reps"), eosio::contract("sika.rep")]] rep_info {
      name              owner;
      bool              is_active        = true;
      uint16_t          boost_apy_bps    = 0;     // 0..500 (0..5.00%)
      std::vector<name> bp_slate;                 // up to 30 BPs
      std::string       url;                      // public-facing profile
      asset             total_followers_weight;   // sum of delegated stake
      uint32_t          follower_count   = 0;     // for analytics
      time_point        last_boost_update;
      time_point        registered_at;

      uint64_t primary_key() const { return owner.value; }

      EOSLIB_SERIALIZE( rep_info,
         (owner)(is_active)(boost_apy_bps)(bp_slate)(url)
         (total_followers_weight)(follower_count)
         (last_boost_update)(registered_at) )
   };
   using reps_table = eosio::multi_index<"reps"_n, rep_info>;

   // ---------------------------------------------------------------------------
   // boost_account — per-Rep cGHS balance held at sika.boost.
   // When a Rep transfers cGHS in, this row tracks how much is available to
   // pay their delegators. Drained as crediboost calls fire.
   // ---------------------------------------------------------------------------
   struct [[eosio::table, eosio::contract("sika.rep")]] boost_balance {
      name   rep;
      asset  cghs_funded;       // cGHS the Rep has deposited
      asset  cghs_paid;         // cGHS already distributed to delegators

      uint64_t primary_key() const { return rep.value; }
   };
   using boost_balance_table = eosio::multi_index<"boostbal"_n, boost_balance>;

   class [[eosio::contract("sika.rep")]] rep_contract : public eosio::contract {
   public:
      using contract::contract;

      // -----------------------------------------------------------------------
      // regrep — register as a Representative. The account must:
      //   - have ≥ 1M SIKA staked (encourages skin in the game)
      //   - call eosio::regproxy(true) to flip the proxy bit in voter_info
      //
      // The boost is NOT set here; setboost is a separate action so Reps can
      // adjust without re-registering.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void regrep( const name& rep, const std::string& url );

      // -----------------------------------------------------------------------
      // unregrep — voluntary withdrawal. Existing delegators are unaffected
      // (their votes still count) but new delegations cannot route here.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void unregrep( const name& rep );

      // -----------------------------------------------------------------------
      // setboost — declare the boost APY (in basis points) AND the BP slate.
      //
      // Protocol-enforced caps:
      //   - boost_apy_bps must be ≤ 500 (5.00%)
      //   - bp_slate.size() must be ≤ 30
      //   - every BP in slate must be currently registered with sika.system
      //
      // The Rep is responsible for funding the boost — see fundboost below.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void setboost( const name& rep,
                     uint16_t boost_apy_bps,
                     const std::vector<name>& bp_slate );

      // -----------------------------------------------------------------------
      // fundboost — Rep deposits cGHS to fund future boost payouts.
      //
      // Triggered automatically via the sika.token::transfer notification
      // hook when a Rep sends cGHS to sika.boost. Updates boost_balance.
      // -----------------------------------------------------------------------
      [[eosio::on_notify("sika.token::transfer")]]
      void on_transfer( const name& from, const name& to,
                        const asset& quantity, const std::string& memo );

      // -----------------------------------------------------------------------
      // payboost — distribute funded cGHS to a specific delegator.
      //
      // Called by a keeper (or any account, gas-paid by the keeper) to settle
      // accrued boost. Caps the payout at what the Rep has funded — if the
      // Rep underfunds, delegators get less; the contract NEVER pays out cGHS
      // the Rep hasn't deposited.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void payboost( const name& rep, const name& delegator );

   private:
      // Helper: confirm a candidate BP slate matches active sika.system BPs.
      void validate_bp_slate( const std::vector<name>& slate );
   };

} // namespace sikarep
