// =============================================================================
// SikaChain — sika.guard (Network Guardians, Article VII)
// =============================================================================
// Deployed to: sika.guard
//
// The Network Guardians are 9 elected accounts that collectively hold 720M
// SIKA in reserve and serve as the on-chain "supreme court" of the protocol.
// Their powers:
//
//   1. Open / close issues against BPs (sika.system::openissue/closeissue)
//   2. Approve stablecoin and RWA issuers (sika.issue::approve)
//   3. VETO any Rules amendment (sika.rules)
//   4. Multi-sig over the 720M reserve fund (this contract)
//
// All Guardian actions require 6-of-9 approval — propose / approve / execute
// pattern matching eosio.msig. No single Guardian can act unilaterally.
//
// Elections:
//   - Held quarterly (every 91 days)
//   - "Political playoff" knockout — 27 candidates → 18 → 9
//   - Each round runs for 7 days; voters cast 1 vote per round
//   - Top 9 by total weighted SIKA stake become Guardians
//   - Outgoing Guardians remain in office until new keys are installed
// =============================================================================

#pragma once

#include <eosio/asset.hpp>
#include <eosio/binary_extension.hpp>
#include <eosio/eosio.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/singleton.hpp>
#include <eosio/time.hpp>

#include <string>
#include <vector>

namespace sikaguard {

   using eosio::asset;
   using eosio::binary_extension;
   using eosio::check;
   using eosio::current_time_point;
   using eosio::name;
   using eosio::same_payer;
   using eosio::singleton;
   using eosio::time_point;

   // ---------------------------------------------------------------------------
   // PROTOCOL CONSTANTS (Article VII)
   // ---------------------------------------------------------------------------
   static constexpr size_t   guardian_seats          = 9;
   static constexpr size_t   approval_threshold      = 6;   // 6-of-9
   static constexpr int64_t  reserve_fund_amount     = 7'200'000'000'000ll;  // 720M × 10^4
   static constexpr int64_t  election_round_seconds  = 7 * 24 * 60 * 60;     // 7 days
   static constexpr int64_t  election_cycle_seconds  = 91 * 24 * 60 * 60;    // ~quarterly
   static constexpr size_t   round1_candidates       = 27;
   static constexpr size_t   round2_candidates       = 18;
   static constexpr size_t   round3_candidates       = 9;

   // ---------------------------------------------------------------------------
   // CURRENT GUARDIANS — singleton tracking the 9 active seats
   // ---------------------------------------------------------------------------
   struct [[eosio::table("guardians"), eosio::contract("sika.guard")]]
   guardian_council {
      std::vector<name>  seats;                  // exactly 9 names when sworn in
      time_point         sworn_in_at;            // start of current term
      time_point         next_election_starts;   // quarterly cadence
      uint32_t           term_number    = 0;
   };
   using guardian_council_singleton =
      singleton<"guardians"_n, guardian_council>;

   // ---------------------------------------------------------------------------
   // PROPOSAL — anyone can propose; only Guardians can approve
   // ---------------------------------------------------------------------------
   struct [[eosio::table, eosio::contract("sika.guard")]] proposal {
      uint64_t           id;
      name               proposer;
      std::string        title;
      std::string        rationale;
      std::vector<char>  packed_transaction;     // serialized eosio::transaction
      std::vector<name>  approvals;              // current Guardian approvals
      bool               executed       = false;
      time_point         created_at;
      time_point         expires_at;

      uint64_t primary_key() const { return id; }
   };
   using proposals_table = eosio::multi_index<"proposals"_n, proposal>;

   // ---------------------------------------------------------------------------
   // ELECTION CANDIDATE & VOTE tables (used during the playoff window)
   // ---------------------------------------------------------------------------
   struct [[eosio::table, eosio::contract("sika.guard")]] election_round {
      uint64_t      id;
      uint32_t      round_number;     // 1, 2, or 3
      time_point    starts_at;
      time_point    ends_at;
      std::vector<name>    candidates;
      bool          finalized    = false;

      uint64_t primary_key() const { return id; }
   };
   using election_rounds_table = eosio::multi_index<"elecround"_n, election_round>;

   struct [[eosio::table, eosio::contract("sika.guard")]] candidate_votes {
      name      candidate;
      asset     total_weight;        // sum of voter stakes
      uint32_t  voter_count;
      uint64_t  round_id;

      uint64_t primary_key() const { return candidate.value; }
      uint64_t by_weight() const   { return total_weight.amount * -1; }  // desc
      uint64_t by_round()  const   { return round_id; }
   };
   using candidate_votes_table = eosio::multi_index<"candvotes"_n, candidate_votes,
      eosio::indexed_by<"byweight"_n,
         eosio::const_mem_fun<candidate_votes, uint64_t, &candidate_votes::by_weight>>,
      eosio::indexed_by<"byround"_n,
         eosio::const_mem_fun<candidate_votes, uint64_t, &candidate_votes::by_round>>
   >;

   class [[eosio::contract("sika.guard")]] guard_contract : public eosio::contract {
   public:
      using contract::contract;

      // -----------------------------------------------------------------------
      // MULTISIG — propose / approve / execute pattern
      // -----------------------------------------------------------------------

      // Anyone can propose. Cost: RAM for the proposal row (paid by proposer).
      [[eosio::action]]
      void propose( const name&              proposer,
                    const std::string&       title,
                    const std::string&       rationale,
                    const std::vector<char>& packed_transaction,
                    time_point               expires_at );

      // Only current Guardians may approve. 6th approval triggers eligibility
      // for execute (doesn't auto-execute — keeps gas predictable).
      [[eosio::action]]
      void approve( name guardian, uint64_t proposal_id );

      // Withdraw approval before execution
      [[eosio::action]]
      void unapprove( name guardian, uint64_t proposal_id );

      // Execute a proposal that has reached 6-of-9. Authority of sika.guard
      // is used to dispatch the embedded transaction.
      [[eosio::action]]
      void exec( name executor, uint64_t proposal_id );

      // Cancel an expired or stale proposal — reclaims RAM.
      [[eosio::action]]
      void cancel( name proposer, uint64_t proposal_id );

      // -----------------------------------------------------------------------
      // ELECTIONS — playoff knockout
      // -----------------------------------------------------------------------

      // Start a new election cycle (round 1). Anyone can trigger if the
      // cadence (91 days) has elapsed since the last cycle started.
      [[eosio::action]]
      void startelec();

      // Self-nominate as a candidate. Must hold ≥ 1M SIKA staked.
      [[eosio::action]]
      void nominate( name candidate, const std::string& platform );

      // Cast a vote in the current round. Vote weight = voter's staked SIKA.
      [[eosio::action]]
      void elecvote( name voter, name candidate );

      // Close the current round, advance to the next, or — if round 3 — swear
      // in the new 9 Guardians.
      [[eosio::action]]
      void closeround();

      // -----------------------------------------------------------------------
      // RULES VETO — Guardian power over amendments
      // -----------------------------------------------------------------------

      // Guardian collectively (6-of-9) vetoes a pending Rules amendment.
      // Called BY this contract via exec(), targeting sika.rules::veto.
      // No direct action needed — handled by the multisig flow.

   private:
      void require_guardian( name account );
      void seat_guardians_for_next_term( const std::vector<name>& winners );
   };

} // namespace sikaguard
