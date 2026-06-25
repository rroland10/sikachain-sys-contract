// =============================================================================
// SikaChain — sika.rules (protocol amendments, Article VIII)
// =============================================================================
// Deployed to: sika.rules
//
// The Rules of the SikaChain are not immutable — but changing them is hard
// by design. An amendment must:
//
//   1. Be proposed by any account (RAM cost paid by proposer)
//   2. Collect approvals from ≥ 17 of 21 active Top-21 BPs (81% supermajority)
//   3. Enter a 7-day VETO WINDOW where sika.guard (6-of-9) may veto
//   4. If unvetoed, execute the embedded transaction with sika.rules authority
//
// Why this design:
//   - The 17-of-21 supermajority means a small BP minority cannot push amendments
//   - The Guardian veto serves as a circuit breaker for clearly harmful changes
//   - The 7-day delay gives the network time to react and Guardians time to coordinate
//
// What can be amended:
//   - Inflation rate / halving schedule
//   - BP compliance rules (Article IV thresholds)
//   - Rep boost cap (currently 500 bps)
//   - Guardian seat count
//   - RAM Bancor fee
//   - Anything else the system contracts permit sika.rules to call
// =============================================================================

#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/time.hpp>
#include <eosio/transaction.hpp>

#include <string>
#include <vector>

namespace sikarules {

   using eosio::asset;
   using eosio::check;
   using eosio::current_time_point;
   using eosio::name;
   using eosio::same_payer;
   using eosio::time_point;

   // ---------------------------------------------------------------------------
   // PROTOCOL CONSTANTS (Article VIII)
   // ---------------------------------------------------------------------------
   static constexpr size_t  bp_approval_threshold  = 17;   // of 21
   static constexpr size_t  bp_active_count        = 21;
   static constexpr int64_t veto_window_seconds    = 7 * 24 * 60 * 60;  // 7 days
   static constexpr int64_t max_proposal_lifetime  = 30 * 24 * 60 * 60; // 30 days

   // ---------------------------------------------------------------------------
   // AMENDMENT — lifecycle: PROPOSED → APPROVED → EXECUTABLE → EXECUTED|VETOED
   // ---------------------------------------------------------------------------
   enum class amendment_status : uint8_t {
      PROPOSED   = 0,
      APPROVED   = 1,    // ≥17 BP approvals reached; 7-day veto window armed
      EXECUTED   = 2,    // veto window expired; transaction dispatched
      VETOED     = 3,    // Guardian 6-of-9 vetoed during the window
      EXPIRED    = 4,    // proposal lived past max_proposal_lifetime without approval
   };

   struct [[eosio::table("amendments"), eosio::contract("sika.rules")]] amendment {
      uint64_t           id;
      name               proposer;
      std::string        article;             // e.g. "Article V — Inflation"
      std::string        rationale;
      std::vector<char>  packed_transaction;  // the action(s) to apply
      std::vector<name>  bp_approvals;        // up to 21
      uint8_t            status         = static_cast<uint8_t>(amendment_status::PROPOSED);
      time_point         created_at;
      time_point         approved_at;         // when 17-of-21 was reached
      time_point         executable_at;       // approved_at + 7 days
      time_point         expires_at;          // created_at + 30 days

      uint64_t primary_key() const { return id; }
   };
   using amendments_table = eosio::multi_index<"amendments"_n, amendment>;

   class [[eosio::contract("sika.rules")]] rules_contract : public eosio::contract {
   public:
      using contract::contract;

      // -----------------------------------------------------------------------
      // propose — anyone may propose. Cost: RAM for the row.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void propose( const name& proposer,
                    const std::string& article,
                    const std::string& rationale,
                    const std::vector<char>& packed_transaction );

      // -----------------------------------------------------------------------
      // approve — current Top-21 BP endorses an amendment.
      //
      // We re-check Top-21 status at approval time. If the BP rotates out
      // before the proposal hits 17, their approval is dropped on the next
      // approve() call (lazy cleanup).
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void approve( name bp, uint64_t amendment_id );

      [[eosio::action]]
      void unapprove( name bp, uint64_t amendment_id );

      // -----------------------------------------------------------------------
      // veto — sika.guard (6-of-9 via guard exec) calls this during the
      // 7-day window. Marks the amendment VETOED; transaction never executes.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void veto( uint64_t amendment_id, const std::string& guardian_rationale );

      // -----------------------------------------------------------------------
      // execute — after the 7-day veto window expires unvetoed, anyone can
      // trigger execution. Dispatches the packed transaction with this
      // contract's authority.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void execute( name executor, uint64_t amendment_id );

      // -----------------------------------------------------------------------
      // gc — garbage-collect expired or fully-handled amendments. Anyone can call.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void gc( uint64_t amendment_id );

   private:
      // Count BP approvals that are CURRENTLY in the Top 21
      size_t count_valid_approvals( const std::vector<name>& approvals );

      // Read current Top 21 from sika.system
      std::vector<name> current_top21();
   };

} // namespace sikarules
