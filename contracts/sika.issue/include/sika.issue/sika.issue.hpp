// =============================================================================
// SikaChain — sika.issue (issuer registry, Article IX)
// =============================================================================
// Deployed to: sika.issue
//
// The "open platform" half of SikaChain: anyone may apply to issue a
// stablecoin (cNGN, cKES, gGOLD…) or tokenize a real-world asset (cCOCOA,
// cGOLD, cLAND). The registry mediates between issuers, Guardians, and
// auditors.
//
// Approval flow:
//   1. ISSUER APPLIES   — submits KYC docs, reserve attestation, audit report
//   2. AUDITOR REVIEWS  — at least one whitelisted auditor signs off
//   3. GUARDIANS VOTE   — 6-of-9 Guardians approve via sika.guard::exec
//   4. TOKEN LIVE       — sika.token::create is called; issuer can mint
//
// Ongoing obligations:
//   - Monthly reserve attestation (else token enters PAUSED status)
//   - Annual audit (else token enters PAUSED status)
//   - Guardian can suspend at any time via suspendissuer (Article IX)
//
// Anti-rug protection:
//   - sika.token::issue requires the issuer's mint authority
//   - sika.token::retire (burn) requires the issuer's authority too
//   - If the issuer is suspended, transfers continue but mint is blocked
//     (holders can still trade/redeem their existing tokens)
// =============================================================================

#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/system.hpp>
#include <eosio/time.hpp>

#include <string>
#include <vector>

namespace sikaissue {

   using eosio::asset;
   using eosio::check;
   using eosio::current_time_point;
   using eosio::name;
   using eosio::same_payer;
   using eosio::symbol;
   using eosio::time_point;

   // ---------------------------------------------------------------------------
   // ISSUER LIFECYCLE
   // ---------------------------------------------------------------------------
   enum class issuer_status : uint8_t {
      APPLIED   = 0,    // Submitted; under auditor review
      AUDITED   = 1,    // Auditor signed off; pending Guardian vote
      LIVE      = 2,    // Approved; can mint via sika.token
      PAUSED    = 3,    // Missed an obligation; mint blocked, transfers allowed
      REVOKED   = 4,    // Permanently revoked (rug, fraud, etc.)
   };

   static constexpr int64_t monthly_attest_seconds = 31 * 24 * 60 * 60;   // 31 days
   static constexpr int64_t annual_audit_seconds   = 366 * 24 * 60 * 60;  // 1 year + buffer
   static constexpr int64_t pause_grace_seconds    = 7 * 24 * 60 * 60;    // 7-day grace before auto-pause

   // ---------------------------------------------------------------------------
   // ISSUERS TABLE — one row per (account, symbol)
   //
   // Primary key = symbol_code raw. An account can issue multiple tokens but
   // each symbol has its own row.
   // ---------------------------------------------------------------------------
   struct [[eosio::table("issuers"), eosio::contract("sika.issue")]] issuer_info {
      symbol         sym;                 // e.g. CGHS,4 or CGOLD,8
      name           issuer_account;      // who controls the mint key
      std::string    asset_description;   // human-readable
      std::string    reserve_kind;        // "FIAT-GHS", "PHYSICAL-GOLD", "T-BILL", etc.
      uint8_t        status      = static_cast<uint8_t>(issuer_status::APPLIED);
      asset          circulating_supply;
      asset          reserves_attested;   // self-reported by issuer at last attest
      time_point     applied_at;
      time_point     last_attestation_at;
      time_point     last_audit_at;
      time_point     paused_at;
      std::vector<name> auditors;         // accounts permitted to attest audits
      uint32_t       guardian_approvals;  // count at exec time

      uint64_t primary_key() const { return sym.code().raw(); }
   };
   using issuers_table = eosio::multi_index<"issuers"_n, issuer_info>;

   class [[eosio::contract("sika.issue")]] issue_contract : public eosio::contract {
   public:
      using contract::contract;

      // -----------------------------------------------------------------------
      // applyissuer — submit a new issuer application
      //
      // The applicant must have already done off-chain KYC; this action
      // records the on-chain commitment. Cost: RAM for the row.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void applyissuer( const name&        issuer_account,
                        const symbol&      sym,
                        const std::string& asset_description,
                        const std::string& reserve_kind,
                        const asset&       initial_reserves );

      // -----------------------------------------------------------------------
      // attestaudit — whitelisted auditor signs off
      //
      // Multiple auditors may attest; the FIRST flip status APPLIED → AUDITED.
      // Subsequent attestations refresh last_audit_at.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void attestaudit( name auditor, const symbol& sym, const std::string& report_hash );

      // -----------------------------------------------------------------------
      // approve — Guardian 6-of-9 approval (called via sika.guard::exec)
      //
      // Authority: sika.guard. Status moves AUDITED → LIVE; sika.token::create
      // is invoked inline so the token actually exists on-chain.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void approve( const symbol& sym, const asset& max_supply );

      // -----------------------------------------------------------------------
      // attestreserves — issuer self-reports reserves (monthly cadence)
      //
      // We don't verify reserves on-chain (they're off-chain assets). We do
      // record the attestation and reset the auto-pause clock. If reserves
      // fall below circulating_supply, Guardians can pause via suspendissuer.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void attreserves( const symbol& sym, const asset& current_reserves );

      // -----------------------------------------------------------------------
      // mint — issuer mints new tokens (proxied to sika.token::issue)
      //
      // Only LIVE issuers may mint. Enforces circulating_supply ≤
      // reserves_attested. If reserves are stale (> 1 month), reject.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void mint( const symbol& sym, const name& to, const asset& quantity, const std::string& memo );

      // -----------------------------------------------------------------------
      // burn — destroy tokens held by the issuer (redeemed off-chain)
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void burn( const symbol& sym, const asset& quantity, const std::string& memo );

      // -----------------------------------------------------------------------
      // suspendissuer — Guardian (6-of-9) suspends an issuer
      //
      // Authority: sika.guard. Status moves LIVE → PAUSED. Transfers continue;
      // mint is blocked. Holders can still trade and redeem their tokens.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void suspissuer( const symbol& sym, const std::string& reason );

      // -----------------------------------------------------------------------
      // resumeissuer — Guardian (6-of-9) resumes a paused issuer
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void resumissuer( const symbol& sym );

      // -----------------------------------------------------------------------
      // revokeissuer — Guardian (6-of-9) permanently revokes
      //
      // Status moves PAUSED → REVOKED. Token freezes entirely — no new mints
      // and existing balances cannot transfer. Reserved for rugs / fraud.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void revkissuer( const symbol& sym, const std::string& reason );

      // -----------------------------------------------------------------------
      // addauditor — Guardian (6-of-9) whitelists an auditor
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void addauditor( name auditor );

      // -----------------------------------------------------------------------
      // checkstale — auto-pause issuers with stale attestations
      // Anyone can call; idempotent.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void checkstale();

   private:
      bool issuer_can_mint( const issuer_info& info ) const;
      const char* issuer_mint_error( const issuer_info& info ) const;
   };

} // namespace sikaissue
