// =============================================================================
// SikaChain — sika.treas (Settlement / Treasury, BP compensation v0.2)
// =============================================================================
// Status: scaffold — schema + accruefee ledger; FX/sweep/payout TBD.
// Spec: docs/bp-compensation-settlement-v0.2.md
// =============================================================================

#pragma once

#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/eosio.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/singleton.hpp>
#include <eosio/time.hpp>

#include <string>

namespace sikatreas {

   using eosio::asset;
   using eosio::check;
   using eosio::current_time_point;
   using eosio::name;
   using eosio::same_payer;
   using eosio::singleton;
   using eosio::symbol;
   using eosio::time_point;

   // ---------------------------------------------------------------------------
   // Reserve composition (cUSD + gGOLD blend — payout unit is cUSD)
   // ---------------------------------------------------------------------------
   struct [[eosio::table("reserve"), eosio::contract("sika.treas")]]
   reserve_row {
      asset    cusd_balance;
      asset    ggold_balance;
      uint16_t reserve_gold_bps = 3000; // 30% gold target (illustrative)
   };
   using reserve_singleton = singleton<"reserve"_n, reserve_row>;

   // ---------------------------------------------------------------------------
   // Governance parameters
   // ---------------------------------------------------------------------------
   struct [[eosio::table("params"), eosio::contract("sika.treas")]]
   params_row {
      uint16_t sweep_slice_bps           = 0;
      asset    cost_recovery_cusd;
      uint16_t max_subsidy_per_market_bps = 0;
      uint16_t fee_to_yield_bps          = 0;
      bool     initialized               = false;
   };
   using params_singleton = singleton<"params"_n, params_row>;

   // ---------------------------------------------------------------------------
   // Per-market P&L (scope = sika.treas, primary key = market id)
   // market: short Antelope name for country/market (e.g. gh, tz, ng)
   // ---------------------------------------------------------------------------
   struct [[eosio::table, eosio::contract("sika.treas")]]
   market_pnl_row {
      name     market;
      asset    fees_collected_local;
      asset    cost_allocated_local;
      asset    net_contribution_local;
      asset    fees_collected_cusd;
      asset    cost_allocated_cusd;
      asset    net_contribution_cusd;
      asset    subsidy_in;
      asset    subsidy_out;
      time_point last_accrual_at;

      uint64_t primary_key() const { return market.value; }
   };
   using market_pnl_table = eosio::multi_index<"marketpnl"_n, market_pnl_row>;

   // ---------------------------------------------------------------------------
   // Per-market payout menu + compliance gate (§6.4)
   // ---------------------------------------------------------------------------
   struct [[eosio::table, eosio::contract("sika.treas")]]
   marketpref_row {
      name     market;
      symbol   local_symbol;
      bool     allow_cusd          = false;
      bool     allow_ggold         = false;
      bool     compliance_ready    = false;

      uint64_t primary_key() const { return market.value; }
   };
   using marketpref_table = eosio::multi_index<"marketpref"_n, marketpref_row>;

   /** Per-user REX yield payout preference (§6.4). */
   struct [[eosio::table, eosio::contract("sika.treas")]]
   user_payout_row {
      name   owner;
      name   market;
      symbol payout_currency;

      uint64_t primary_key() const { return owner.value; }
   };
   using user_payout_table = eosio::multi_index<"userpayout"_n, user_payout_row>;

   // ---------------------------------------------------------------------------
   // FX oracle stub (local stable → CUSD reference, parts-per-million)
   // 1_000_000 ppm = 1:1 peg at equal precision (dev default when row missing).
   // ---------------------------------------------------------------------------
   struct [[eosio::table, eosio::contract("sika.treas")]]
   fx_rate_row {
      symbol   local_symbol;
      uint64_t cusd_ppm = 1'000'000;
      time_point updated_at;
      time_point expires_at; // epoch 0 = no expiry (appended v0.2)

      uint64_t primary_key() const { return local_symbol.code().raw(); }
   };
   using fx_rate_table = eosio::multi_index<"fxquotes"_n, fx_rate_row>;

   // Licensed-rail oracle signing key (optional; enables pushfxsig).
   struct [[eosio::table("oraclecfg"), eosio::contract("sika.treas")]]
   oracle_row {
      eosio::public_key attest_key;
      bool            require_signed_push = false;
   };
   using oracle_singleton = singleton<"oraclecfg"_n, oracle_row>;

   class [[eosio::contract("sika.treas")]] treas_contract : public eosio::contract {
   public:
      using contract::contract;

      [[eosio::action]]
      void init();

      /** Credit reserve accounting after CUSD is issued/transferred to sika.treas. */
      [[eosio::action]]
      void creditreserve( const asset& quantity );

      [[eosio::action]]
      void setparams( name authority,
                      uint16_t sweep_slice_bps,
                      const asset& cost_recovery_cusd,
                      uint16_t max_subsidy_per_market_bps,
                      uint16_t fee_to_yield_bps,
                      uint16_t reserve_gold_bps );

      [[eosio::action]]
      void accruefee( name market, const asset& local_quantity );

      [[eosio::action]]
      void sweep( name market );

      [[eosio::action]]
      void paycost( name producer );

      [[eosio::action]]
      void clearyield( name owner,
                       const asset& cusd_amount,
                       const symbol& payout_currency,
                       name market );

      /** Configure allowed REX payout currencies for a market (§6.4). */
      [[eosio::action]]
      void setmarketpref( name authority,
                          name market,
                          const symbol& local_symbol,
                          bool allow_cusd,
                          bool allow_ggold,
                          bool compliance_ready );

      /** User opts into a licensed payout currency for REX yield (§6.4). */
      [[eosio::action]]
      void setpayoutpref( name owner,
                          name market,
                          const symbol& payout_currency );

      /** Cross-market subsidy (CUSD reference); capped by max_subsidy_per_market_bps. */
      [[eosio::action]]
      void subsidize( name from_market, name to_market, const asset& amount );

      /** Set FX rate (local → CUSD ppm). ttl_seconds=0 means no expiry. */
      [[eosio::action]]
      void setfx( name authority,
                  const symbol& local_symbol,
                  uint64_t cusd_ppm,
                  uint32_t ttl_seconds );

      /** Licensed-rail oracle feed (sika.oracle). Disabled when require_signed_push. */
      [[eosio::action]]
      void pushfx( const symbol& local_symbol,
                   uint64_t cusd_ppm,
                   uint32_t ttl_seconds );

      /** Signed oracle feed — verifies attest_key signature over FX payload. */
      [[eosio::action]]
      void pushfxsig( const symbol& local_symbol,
                      uint64_t cusd_ppm,
                      uint32_t ttl_seconds,
                      uint64_t published_at,
                      const eosio::signature& sig );

      /** Configure oracle attestation key (governance). */
      [[eosio::action]]
      void setoraclekey( name authority,
                         const eosio::public_key& attest_key,
                         bool require_signed_push );

      [[eosio::action]]
      void rebalance();

   private:
      void require_governance( name authority ) const;
      void require_initialized() const;
      void verify_fx_attestation( const symbol& local_symbol,
                                  uint64_t cusd_ppm,
                                  uint32_t ttl_seconds,
                                  uint64_t published_at,
                                  const eosio::signature& sig ) const;
      void upsert_fx( const symbol& local_symbol,
                      uint64_t cusd_ppm,
                      uint32_t ttl_seconds,
                      const char* label );
      void assert_payout_allowed( name market,
                                  const symbol& payout_currency ) const;
   };

} // namespace sikatreas
