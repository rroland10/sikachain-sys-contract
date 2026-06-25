// =============================================================================
// SikaChain — sika.system (core system contract)
// =============================================================================
//
// This is the SikaChain core system contract, modeled on the Antelope
// system contract pattern. It must be deployed to the privileged `eosio`
// account (Spring protocol constant until a custom chain build).
//
// SikaChain-specific protocol design:
//   - Bitcoin-style halving inflation (not fixed APR)
//   - 75/25 split between Top 21 and Standby 22-50
//   - 5-rule BP compliance (Article IV) with vote-removal-only enforcement
//   - Representatives renamed from proxies; +5% APY boost cap in cGHS
//   - REX yield paid in cGHS (the network's stablecoin), not SIKA
//   - RAM Bancor with 0.5% fee split (50% REX, 50% burn)
//
// Authority assumptions:
//   - `eosio` (Spring privileged) — hosts sika.system WASM
//   - `sika.token`     — SIKA + cGHS token contract
//   - `sika.rex`       — REX pool custody
//   - `sika.rep`         — Representative registry (separate contract)
//   - `sika.guard`       — Network Guardian multisig
//   - `sika.rules`       — Rules amendments
//   - `sika.issue`       — Issuer registry
//   - `sika.boost`       — Rep boost payout pool (cGHS)
//   - `sika.bppay`       — BP rewards from inflation
//   - `sika.burn`        — SIKA burn destination
// =============================================================================

#pragma once

#include <eosio/asset.hpp>
#include <eosio/binary_extension.hpp>
#include <eosio/eosio.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/privileged.hpp>
#include <eosio/producer_schedule.hpp>
#include <eosio/singleton.hpp>
#include <eosio/system.hpp>
#include <eosio/time.hpp>

#include <sika.system/exchange_state.hpp>
#include <sika.system/native.hpp>

#include <deque>
#include <optional>
#include <string>
#include <type_traits>

namespace sikasystem {

   using eosio::asset;
   using eosio::block_signing_authority;
   using eosio::block_timestamp;
   using eosio::check;
   using eosio::const_mem_fun;
   using eosio::current_time_point;
   using eosio::ignore;
   using eosio::indexed_by;
   using eosio::microseconds;
   using eosio::name;
   using eosio::same_payer;
   using eosio::seconds;
   using eosio::singleton;
   using eosio::symbol;
   using eosio::symbol_code;
   using eosio::time_point;
   using eosio::time_point_sec;
   using eosio::unsigned_int;

   // ===========================================================================
   // CONSTANTS — protocol parameters (Articles I-V of the Rules of SikaChain)
   // ===========================================================================

   // Token symbols
   static constexpr symbol sika_symbol      = symbol{ "SIKA", 4 };
   static constexpr symbol cghs_symbol      = symbol{ "CGHS", 4 };
   static constexpr symbol cusd_symbol      = symbol{ "CUSD", 4 };
   static constexpr symbol ramcore_symbol   = symbol{ "RAMCORE", 4 };
   static constexpr symbol ram_symbol       = symbol{ "RAM", 0 };

   // Supply parameters (Article I)
   // 8B SIKA initial supply, raw units = 8B × 10^4 = 8 × 10^13
   static constexpr int64_t initial_sika_supply  = 80'000'000'000'000ll;
   // 8.64B SIKA asymptotic ceiling = initial + 8% lifetime inflation
   static constexpr int64_t max_sika_supply      = 86'400'000'000'000ll;

   // Inflation schedule (Article V) — Bitcoin-style halving
   static constexpr int64_t inflation_year1_bps  = 100;  // 1.00% = 100 bps
   static constexpr int64_t halving_epoch_years  = 4;
   // Distribution: 75% Top 21, 25% Standby 22-50
   static constexpr int64_t top21_share_bps      = 7500; // 75.00%
   static constexpr int64_t standby_share_bps    = 2500; // 25.00%
   static constexpr int64_t top21_count          = 21;
   static constexpr int64_t standby_count        = 29;   // ranks 22-50

   // BP compliance — Article IV (must mirror domain.ts EXACTLY)
   // 1M SIKA staked floor
   static constexpr int64_t bp_stake_floor       = 10'000'000'000ll;  // 1M × 10^4
   // > 95% uptime required (uptime_pct stored as basis points, 0..10000)
   static constexpr int64_t uptime_floor_bps     = 9500;
   // 7-day SLA — if open_issue is older than this, BP is in breach
   static constexpr int64_t sla_seconds          = 7 * 24 * 60 * 60;

   // Representative boost (Article V) — capped at +5% APY in cGHS
   // Stored as basis points: 500 = 5.00%
   static constexpr int64_t rep_boost_cap_bps    = 500;

   // Voting (Article III)
   static constexpr size_t  max_bps_per_vote     = 30;

   // RAM Bancor (Article VI) — 0.5% trade fee, 50% to REX, 50% burned
   static constexpr int64_t ram_fee_bps          = 50;   // 0.50%
   static constexpr int64_t ram_fee_to_rex_bps   = 5000; // 50% of fee
   static constexpr int64_t ram_fee_to_burn_bps  = 5000; // 50% of fee

   // REX cool-down — 7 days from sell to claim
   static constexpr int64_t rex_unstake_seconds  = 7 * 24 * 60 * 60;

   // BP probation — once a rule fails, votes withdraw over this window
   static constexpr int64_t bp_probation_seconds = 7 * 24 * 60 * 60;

   // ===========================================================================
   // GLOBAL STATE — singletons holding chain-wide parameters
   // ===========================================================================

   // Core blockchain parameters (CPU/NET/RAM rate, schedule size, etc.)
   struct [[eosio::table("global"), eosio::contract("sika.system")]]
   sika_global_state : eosio::blockchain_parameters {
      uint64_t          max_ram_size                = 64ll * 1024 * 1024 * 1024;  // 64 GB
      uint64_t          total_ram_bytes_reserved    = 0;
      int64_t           total_ram_stake             = 0;
      block_timestamp   last_producer_schedule_update;
      time_point        last_pervote_bucket_fill;
      int64_t           pervote_bucket              = 0;     // cumulative SIKA owed to top 21
      int64_t           perblock_bucket             = 0;     // cumulative SIKA owed per-block
      uint32_t          total_unpaid_blocks         = 0;
      int64_t           total_activated_stake       = 0;
      time_point        thresh_activated_stake_time;
      uint16_t          last_producer_schedule_size = 0;
      double            total_producer_vote_weight  = 0;
      block_timestamp   last_name_close;

      // SikaChain extensions
      time_point        genesis_time;                         // chain genesis (year 0)
      int64_t           current_year_inflation_bps  = inflation_year1_bps;
      int64_t           total_burned                = 0;      // lifetime SIKA burned
      int64_t           total_minted                = 0;      // lifetime SIKA minted via inflation

      EOSLIB_SERIALIZE_DERIVED( sika_global_state, eosio::blockchain_parameters,
         (max_ram_size)(total_ram_bytes_reserved)(total_ram_stake)
         (last_producer_schedule_update)(last_pervote_bucket_fill)
         (pervote_bucket)(perblock_bucket)(total_unpaid_blocks)
         (total_activated_stake)(thresh_activated_stake_time)
         (last_producer_schedule_size)(total_producer_vote_weight)
         (last_name_close)
         (genesis_time)(current_year_inflation_bps)(total_burned)(total_minted) )
   };

   using global_state_singleton = singleton<"global"_n, sika_global_state>;

   // ===========================================================================
   // PRODUCER INFO — Block Producer registry (Article IV)
   // ===========================================================================

   struct [[eosio::table, eosio::contract("sika.system")]] producer_info {
      name              owner;
      double            total_votes        = 0;     // weighted by stake
      eosio::public_key producer_key;               // legacy convenience field
      bool              is_active          = true;
      std::string       url;
      uint32_t          unpaid_blocks      = 0;
      time_point        last_claim_time;
      uint16_t          location           = 0;     // ISO 3166 numeric

      // SikaChain extensions — 5-rule compliance (Article IV)
      // All boolean flags default to "compliant" so initial registration is clean.
      uint16_t          uptime_bps         = 10000; // 0..10000 (100.00%)
      bool              has_public_rpc     = false; // proven only after attestation
      bool              upgrade_on_time    = true;
      bool              issues_within_sla  = true;
      time_point        open_issue_at;              // null when no open issue (epoch 0)

      // Status — used by enforcement to gate inflation eligibility
      uint8_t           probation_flags    = 0;     // bitmask of failing rules
      time_point        probation_started_at;       // when the 7-day clock started

      uint64_t primary_key()const { return owner.value; }
      double   by_votes()  const  { return is_active ? -total_votes : total_votes; }
      bool     active()    const  { return is_active; }
      bool     in_probation() const { return probation_flags != 0; }

      void deactivate() {
         producer_key = eosio::public_key{};
         is_active    = false;
      }

      EOSLIB_SERIALIZE( producer_info,
         (owner)(total_votes)(producer_key)(is_active)(url)(unpaid_blocks)
         (last_claim_time)(location)
         (uptime_bps)(has_public_rpc)(upgrade_on_time)(issues_within_sla)(open_issue_at)
         (probation_flags)(probation_started_at) )
   };

   using producers_table = eosio::multi_index<"producers"_n, producer_info,
      indexed_by<"prototalvote"_n, const_mem_fun<producer_info, double, &producer_info::by_votes>>
   >;

   // Compliance violation flag bits — must match domain.ts BpRuleViolation enum
   namespace probation_bits {
      static constexpr uint8_t UPTIME_BELOW_FLOOR = 1 << 0;
      static constexpr uint8_t NO_PUBLIC_RPC      = 1 << 1;
      static constexpr uint8_t BELOW_STAKE_FLOOR  = 1 << 2;
      static constexpr uint8_t MISSED_UPGRADE     = 1 << 3;
      static constexpr uint8_t ISSUE_SLA_BREACHED = 1 << 4;
   }

   // ===========================================================================
   // VOTER INFO — voters and Representative followers
   // ===========================================================================

   struct [[eosio::table, eosio::contract("sika.system")]] voter_info {
      name              owner;
      name              proxy;                 // Representative this voter follows (if any)
      std::vector<name> producers;             // direct BP votes (≤ 30) if no proxy
      int64_t           staked              = 0;
      double            last_vote_weight    = 0;
      double            proxied_vote_weight = 0; // total weight of followers (if this is a Rep)
      bool              is_proxy            = false;
      uint32_t          flags1              = 0;
      uint32_t          reserved2           = 0;
      eosio::asset      reserved3;

      uint64_t primary_key()const { return owner.value; }
      enum class flags1_fields : uint32_t {
         ram_managed   = 1,
         net_managed   = 2,
         cpu_managed   = 4,
      };

      EOSLIB_SERIALIZE( voter_info,
         (owner)(proxy)(producers)(staked)(last_vote_weight)
         (proxied_vote_weight)(is_proxy)(flags1)(reserved2)(reserved3) )
   };

   using voters_table = eosio::multi_index<"voters"_n, voter_info>;

   // ===========================================================================
   // REX — stake SIKA, earn cGHS yield (Article V)
   // ===========================================================================

   struct [[eosio::table("rexpool"), eosio::contract("sika.system")]] rex_pool {
      uint8_t   version           = 0;
      asset     total_lent;          // SIKA currently lent out of the pool
      asset     total_unlent;        // SIKA available
      asset     total_rent;          // SIKA accumulated as rent
      asset     total_lendable;      // total_lent + total_unlent
      asset     total_rex;           // total REX shares minted
      asset     namebid_proceeds;
      uint64_t  loan_num            = 0;

      // SikaChain extension — reference-unit yield (cUSD-equivalent atoms).
      // Stored as CGHS symbol for ABI compat; amounts are market-agnostic units
      // settled to the user's payout currency at claim via sika.treas::clearyield.
      asset     cghs_yield_pool;

      uint64_t primary_key() const { return 0; }
   };
   using rex_pool_table = eosio::multi_index<"rexpool"_n, rex_pool>;

   // Tier-2 BP bonus vesting (v0.2 §4.2 / §9.5).
   struct [[eosio::table("bpvest"), eosio::contract("sika.system")]] bp_vest_row {
      name       owner;
      int64_t    total_amount      = 0;  // SIKA escrowed on eosio for this schedule
      int64_t    released_amount   = 0;  // SIKA already auto-staked to REX
      time_point vest_start;
      uint32_t   vest_seconds      = 0;
      bool       forfeited         = false;

      uint64_t primary_key() const { return owner.value; }
   };
   using bp_vest_table = eosio::multi_index<"bpvest"_n, bp_vest_row>;

   struct [[eosio::table("vestglb"), eosio::contract("sika.system")]] bp_vest_global {
      uint8_t  tier2_vesting_enabled = 0;  // 0 = liquid Tier-2 (legacy dev)
      uint32_t bonus_vesting_seconds  = 365 * 24 * 60 * 60;
      uint16_t inflation_gain_bps     = 10000;  // Tier-2 cap = k × epoch fees
      int64_t  epoch_fee_revenue      = 0;      // reference-unit atoms (CUSD-equiv)
   };
   using bp_vest_global_singleton = singleton<"vestglb"_n, bp_vest_global>;

   /** REX governance config (v0.2 — dev can shorten unstake window). */
   struct [[eosio::table("rexcfg"), eosio::contract("sika.system")]] rex_config_row {
      uint32_t unstake_seconds = 7 * 24 * 60 * 60;
   };
   using rex_config_singleton = singleton<"rexcfg"_n, rex_config_row>;

   struct [[eosio::table("rexbal"), eosio::contract("sika.system")]] rex_balance {
      uint8_t  version             = 0;
      name     owner;
      asset    vote_stake;          // SIKA — counted toward voting weight
      asset    rex_balance;         // REX shares held
      int64_t  matured_rex         = 0;
      std::deque<std::pair<time_point_sec, int64_t>> rex_maturities;

      // SikaChain extension — last claimed cGHS yield
      asset    cghs_claimed_lifetime;

      uint64_t primary_key() const { return owner.value; }
   };
   using rex_balance_table = eosio::multi_index<"rexbal"_n, rex_balance>;

   // ===========================================================================
   // USER RESOURCES — CPU/NET stake totals + RAM bytes
   // ===========================================================================

   struct [[eosio::table, eosio::contract("sika.system")]] user_resources {
      name      owner;
      asset     net_weight;
      asset     cpu_weight;
      int64_t   ram_bytes        = 0;

      uint64_t primary_key() const { return owner.value; }
      bool     is_empty()    const { return net_weight.amount == 0
                                          && cpu_weight.amount == 0
                                          && ram_bytes == 0; }
   };
   using user_resources_table = eosio::multi_index<"userres"_n, user_resources>;

   // Delegations (scope = from) — chain-native rows; ABI entry enables get_table_rows on Phase 3.
   struct [[eosio::table("delband"), eosio::contract("sika.system")]] delegated_bandwidth {
      name   from;
      name   to;
      asset  net_weight;
      asset  cpu_weight;

      uint64_t primary_key() const { return to.value; }
   };

   // Pending refund — SIKA in 7-day unstake window
   struct [[eosio::table, eosio::contract("sika.system")]] refund_request {
      name           owner;
      time_point_sec request_time;
      asset          net_amount;
      asset          cpu_amount;

      uint64_t primary_key() const { return owner.value; }
   };
   using refunds_table = eosio::multi_index<"refunds"_n, refund_request>;

   // ===========================================================================
   // RAMMARKET — Bancor curve with 0.5% trade fee
   // ===========================================================================
   // The exchange_state in include/exchange_state.hpp implements the constant-
   // product market maker. We extend it with our 0.5% fee that splits 50/50
   // between REX and burn.

   // ===========================================================================
   // THE CONTRACT
   // ===========================================================================

   class [[eosio::contract("sika.system")]] system_contract : public eosio::contract {
   private:
      voters_table             _voters;
      producers_table          _producers;
      global_state_singleton   _global;
      sika_global_state        _gstate;
      rammarket                _rammarket;
      rex_pool_table           _rexpool;
      rex_balance_table        _rexbalance;
      bp_vest_table            _bpvest;

   public:
      using contract::contract;

      system_contract( name s, name code, eosio::datastream<const char*> ds );
      ~system_contract();

      // ------------------------------------------------------------------------
      // INIT — set up the chain at genesis
      // ------------------------------------------------------------------------

      // Called once at genesis to mint the initial 8B SIKA and 1B cGHS supply.
      [[eosio::action]]
      void init( unsigned_int version, const symbol& core );

      // ------------------------------------------------------------------------
      // BLOCK PRODUCER REGISTRATION (Article IV)
      // ------------------------------------------------------------------------

      // Register as a BP. Must hold ≥ 1M SIKA staked.
      [[eosio::action]]
      void regproducer( const name& producer,
                        const eosio::public_key& producer_key,
                        const std::string& url,
                        uint16_t location );

      // Newer authority-based registration (multi-key support)
      [[eosio::action]]
      void regproducer2( const name& producer,
                         const block_signing_authority& producer_authority,
                         const std::string& url,
                         uint16_t location );

      // Voluntarily withdraw from BP candidacy
      [[eosio::action]]
      void unregprod( const name& producer );

      // ------------------------------------------------------------------------
      // ENFORCEMENT (Article IV) — vote removal only, no slashing
      // ------------------------------------------------------------------------

      // Submit a compliance attestation from this BP — uptime, RPC, etc.
      // Called by the BP itself or by a Guardian-authorized witness.
      [[eosio::action]]
      void attestcompl( const name& producer,
                        uint16_t  uptime_bps,
                        bool      has_public_rpc,
                        bool      upgrade_on_time );

      // Open an issue against a BP. Starts the 7-day SLA clock.
      // Caller must have sika.guard authority.
      [[eosio::action]]
      void openissue( const name& producer, const std::string& reason );

      // Close an issue. Called by sika.guard once resolved.
      [[eosio::action]]
      void closeissue( const name& producer );

      // Run enforcement check — if BP fails any rule, drop them from Top 21
      // and start the 7-day vote-withdrawal clock. Idempotent; runs hourly via cron.
      [[eosio::action]]
      void enforce();

      // ------------------------------------------------------------------------
      // VOTING (Article III)
      // ------------------------------------------------------------------------

      [[eosio::action]]
      void voteproducer( const name& voter, const name& proxy,
                         const std::vector<name>& producers );

      // Register as a Representative (formerly "proxy"). The boost APY is set
      // via sika.rep::setboost, not here.
      [[eosio::action]]
      void regproxy( const name& proxy, bool isproxy );

      // ------------------------------------------------------------------------
      // STAKING (CPU/NET) — same model as Vaulta
      // ------------------------------------------------------------------------

      [[eosio::action]]
      void delegatebw( const name& from, const name& receiver,
                       const asset& stake_net_quantity,
                       const asset& stake_cpu_quantity,
                       bool transfer );

      [[eosio::action]]
      void undelegatebw( const name& from, const name& receiver,
                         const asset& unstake_net_quantity,
                         const asset& unstake_cpu_quantity );

      [[eosio::action]]
      void refund( const name& owner );

      // ------------------------------------------------------------------------
      // REX (Article V) — stake SIKA, earn cGHS yield
      // ------------------------------------------------------------------------

      // Deposit SIKA into the REX pool and receive REX shares
      [[eosio::action]]
      void deposit( const name& owner, const asset& amount );

      [[eosio::action]]
      void withdraw( const name& owner, const asset& amount );

      // Convert SIKA to REX shares
      [[eosio::action]]
      void buyrex( const name& from, const asset& amount );

      // Sell REX shares back to SIKA — enters 7-day unstake window
      [[eosio::action]]
      void sellrex( const name& from, const asset& rex );

      // Claim accumulated reference-unit yield as cGHS (legacy direct transfer).
      [[eosio::action]]
      void claimrewards( const name& owner );

      // Claim reference-unit yield settled in payout_currency via sika.treas.
      [[eosio::action]]
      void claimrexyld( const name& owner,
                        const symbol& payout_currency,
                        const name& market );

      // Internal — called by sika.boost contract when boost payouts are due
      [[eosio::action]]
      void crediboost( const name& voter, const asset& local_amount );

      /** Credit reference-unit yield pool (v0.2 §6 — sika.treas sweep leg). */
      [[eosio::action]]
      void credyield( const asset& ref_amount );

      // ------------------------------------------------------------------------
      // RAM MARKET (Article VI) — Bancor with 0.5% fee
      // ------------------------------------------------------------------------

      [[eosio::action]]
      void buyram( const name& payer, const name& receiver, const asset& quant );

      [[eosio::action]]
      void buyrambytes( const name& payer, const name& receiver, uint32_t bytes );

      [[eosio::action]]
      void sellram( const name& account, int64_t bytes );

      // ------------------------------------------------------------------------
      // INFLATION & BP PAY (Article V) — halving schedule
      // ------------------------------------------------------------------------

      // Producer claims their share of inflation rewards.
      // - Top 21 get 75% of yearly inflation (split per-block + per-vote)
      // - Standby 22-50 get 25% split evenly
      // BPs in probation are SKIPPED but not slashed.
      [[eosio::action]]
      void claimprod( const name& producer );

      // Release vested Tier-2 SIKA into REX (v0.2 §4.2).
      [[eosio::action]]
      void claimvest( const name& producer );

      // Record epoch fee revenue for usage-gated Tier-2 (v0.2 §8).
      [[eosio::action]]
      void accruepoch( const asset& quantity );

      // Governance: Tier-2 vesting / usage-gate parameters.
      [[eosio::action]]
      void setvesting( name authority,
                          uint8_t tier2_vesting_enabled,
                          uint32_t bonus_vesting_seconds,
                          uint16_t inflation_gain_bps );

      /** Governance: credit one daily inflation tranche to pay buckets (dev / ops). */
      [[eosio::action]]
      void refillpay( name authority );

      /** Governance: REX unstake cool-down (seconds until refund claim). */
      [[eosio::action]]
      void setrexcfg( name authority, uint32_t unstake_seconds );

      // ------------------------------------------------------------------------
      // SYSTEM PARAMETERS — only sika.rules (via amendment) may call these
      // ------------------------------------------------------------------------

      [[eosio::action]]
      void setparams( const eosio::blockchain_parameters& params );

      [[eosio::action]]
      void setram( uint64_t max_ram_size );

      [[eosio::action]]
      void setpriv( const name& account, uint8_t is_priv );

      // ------------------------------------------------------------------------
      // BLOCK CALLBACK — eosio::onblock; updates BP unpaid blocks
      // ------------------------------------------------------------------------

      [[eosio::action]]
      void onblock( ignore<block_header> header );

   private:
      // -------- Internal helpers (implemented across the .cpp files) --------

      // Halving inflation: returns SIKA amount to mint for `seconds_elapsed`
      // at the chain's current year.
      int64_t compute_inflation_amount( int64_t seconds_elapsed ) const;

      // Mint SIKA into the pay buckets (per-vote + per-block).
      void mint_inflation_to_buckets( int64_t total_to_mint );

      // Credit inflation buckets for up to `max_elapsed_seconds` of schedule.
      void refill_inflation_buckets( int64_t max_elapsed_seconds );

      void recompute_producer_vote_weight();

      // Mark a BP as failing rule `rule_bit`. Returns true if state changed.
      bool flag_violation( producer_info& p, uint8_t rule_bit );
      bool clear_violation( producer_info& p, uint8_t rule_bit );

      // Remove all voter weight pointing at a probationary BP after 7 days.
      void apply_vote_removal( const name& bp );

      // Standard helpers
      void update_votes( const name& voter_name, const name& proxy,
                         const std::vector<name>& producers, bool voting );
      void propagate_weight_change( const voter_info& voter );

      void update_elected_producers( block_timestamp block_time );

      void change_resource_limits( const name& from, const name& receiver,
                                   const asset& stake_net_delta,
                                   const asset& stake_cpu_delta,
                                   bool transfer );

      void buy_ram( const name& payer, const name& receiver, asset sika_payload );
      void sell_ram( const name& account, int64_t bytes );

      // RAM Bancor fee split — 50% REX compound, 50% burn (Article VI / v0.2 §6.2)
      void route_ram_fee( const asset& fee );

      // Compound SIKA fees into REX share value (no new shares minted).
      void compound_rex_sika( const asset& amount );

      // Stake SIKA from sika.bppay into a user's REX position (Tier-2 vest release).
      void credit_rex_from_bppay( const name& owner, const asset& amount );

      void apply_rex_stake( const name& owner, const asset& amount );
      void credit_rex_from_escrow( const name& owner, const asset& amount );

      int64_t usage_gated_tier2( int64_t scheduled_pay ) const;
      void credit_bp_vest( const name& producer, int64_t amount );
      void forfeit_bp_vest( const name& producer );

      uint32_t rex_unstake_window() const;

      symbol core_symbol() const { return sika_symbol; }
   };

} // namespace sikasystem
