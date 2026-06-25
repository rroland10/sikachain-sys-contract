// =============================================================================
// SikaChain — sika.token (fungible token contract)
// =============================================================================
// Deployed to: sika.token
//
// Implements both the SIKA network token (8B initial, 8.64B asymptotic) and
// the cGHS stablecoin (issued by Network Guardians against attested reserves).
//
// Multi-token: a single contract instance hosts many symbols. Each symbol has
// its own currency_stats row keyed by symbol_code, controlled by `issuer`.
//
// Authority model:
//   - SIKA issuer = eosio (system contract mints inflation; sika.burn destroys)
//   - cGHS issuer = sika.issue (issuer registry mints/burns against reserves)
//
// Standard EOSIO token interface (create / issue / transfer / retire / open /
// close), with SikaChain-specific extension: the issuer can call retire to
// permanently destroy tokens, which is how `sika.burn` becomes a real burn
// rather than a black hole.
// =============================================================================

#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>

#include <string>

namespace sikatoken {

   using eosio::asset;
   using eosio::check;
   using eosio::name;
   using eosio::same_payer;
   using eosio::symbol;
   using eosio::symbol_code;

   class [[eosio::contract("sika.token")]] token : public eosio::contract {
   public:
      using contract::contract;

      // -----------------------------------------------------------------------
      // create — issuer creates a new currency on this contract.
      //
      // Called once per symbol at genesis:
      //   create( eosio,      "8640000000.0000 SIKA" )   ← max supply ceiling
      //   create( sika.issue, "10000000000.0000 CGHS" )  ← max for cGHS
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void create( const name& issuer, const asset& maximum_supply );

      // -----------------------------------------------------------------------
      // issue — mint new tokens. Only the `issuer` may call.
      //
      // For SIKA: called by `eosio` (the system contract) during inflation
      //           and by `eosio` at genesis to seed the 8B initial supply.
      // For cGHS: called by `sika.issue` after a Guardian-approved reserve
      //           attestation lands on-chain.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void issue( const name& to, const asset& quantity, const std::string& memo );

      // -----------------------------------------------------------------------
      // retire — permanently destroy tokens from the issuer's own balance.
      //
      // The `sika.burn` account holds SIKA that arrived via RAM fee burns;
      // periodically `eosio` permission calls retire to actually destroy them
      // and shrink the supply.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void retire( const asset& quantity, const std::string& memo );

      // -----------------------------------------------------------------------
      // transfer — move tokens between accounts. Standard.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void transfer( const name&        from,
                     const name&        to,
                     const asset&       quantity,
                     const std::string& memo );

      // -----------------------------------------------------------------------
      // open / close — manage RAM-billed token balance rows.
      //
      // Anyone can `open` a row for an account (paying RAM); only the owner
      // can `close` (and reclaim that RAM). Required because EOSIO charges
      // RAM to store balance rows.
      // -----------------------------------------------------------------------
      [[eosio::action]]
      void open( const name& owner, const symbol& symbol, const name& ram_payer );

      [[eosio::action]]
      void close( const name& owner, const symbol& symbol );

      // -----------------------------------------------------------------------
      // get_supply / get_balance — view helpers for off-chain code.
      // -----------------------------------------------------------------------
      static asset get_supply( const name& token_contract_account, const symbol_code& sym_code ) {
         stats statstable( token_contract_account, sym_code.raw() );
         const auto& st = statstable.get( sym_code.raw(), "symbol not found" );
         return st.supply;
      }

      static asset get_balance( const name& token_contract_account,
                                const name& owner,
                                const symbol_code& sym_code )
      {
         accounts acnts( token_contract_account, owner.value );
         const auto& ac = acnts.get( sym_code.raw(), "no balance for symbol" );
         return ac.balance;
      }

   private:
      // ------------------------------------------------------------------------
      // Tables
      // ------------------------------------------------------------------------

      struct [[eosio::table]] account {
         asset    balance;
         uint64_t primary_key() const { return balance.symbol.code().raw(); }
      };
      using accounts = eosio::multi_index<"accounts"_n, account>;

      struct [[eosio::table]] currency_stats {
         asset    supply;
         asset    max_supply;
         name     issuer;
         uint64_t primary_key() const { return supply.symbol.code().raw(); }
      };
      using stats = eosio::multi_index<"stat"_n, currency_stats>;

      // ------------------------------------------------------------------------
      // Internal helpers
      // ------------------------------------------------------------------------

      void check_account_auth( const name& account );

      void sub_balance( const name& owner, const asset& value );
      void add_balance( const name& owner, const asset& value, const name& ram_payer );
   };

} // namespace sikatoken
