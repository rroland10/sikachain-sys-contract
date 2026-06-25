// =============================================================================
// SikaChain — sika.token implementation
// =============================================================================
// Implements the multi-token contract for SIKA and cGHS. Mirrors the standard
// eosio.token reference implementation (well-understood, audited, identical
// public interface) so existing wallets and explorers work unchanged.
// =============================================================================

#include <sika.token/sika.token.hpp>

#include <eosio/transaction.hpp>

namespace sikatoken {

   void token::check_account_auth( const name& account ) {
      if( has_auth( account ) ) return;
      check( eosio::get_sender() == account, "missing authority of account" );
   }

   // ---------------------------------------------------------------------------
   // create — register a new currency
   // ---------------------------------------------------------------------------
   void token::create( const name& issuer, const asset& maximum_supply ) {
      require_auth( get_self() );

      auto sym = maximum_supply.symbol;
      check( sym.is_valid(), "invalid symbol" );
      check( maximum_supply.is_valid(), "invalid supply" );
      check( maximum_supply.amount > 0, "max supply must be positive" );

      stats statstable( get_self(), sym.code().raw() );
      auto existing = statstable.find( sym.code().raw() );
      check( existing == statstable.end(), "token with symbol already exists" );

      statstable.emplace( get_self(), [&]( auto& s ) {
         s.supply.symbol = maximum_supply.symbol;
         s.max_supply    = maximum_supply;
         s.issuer        = issuer;
      });
   }

   // ---------------------------------------------------------------------------
   // issue — mint new tokens to a destination
   // ---------------------------------------------------------------------------
   void token::issue( const name& to, const asset& quantity, const std::string& memo ) {
      auto sym = quantity.symbol;
      check( sym.is_valid(), "invalid symbol" );
      check( memo.size() <= 256, "memo too long (max 256)" );

      stats statstable( get_self(), sym.code().raw() );
      auto existing = statstable.require_find( sym.code().raw(), "symbol does not exist" );
      const auto& st = *existing;

      check_account_auth( st.issuer );
      check( quantity.is_valid(), "invalid quantity" );
      check( quantity.amount > 0, "must issue a positive quantity" );
      check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
      check( quantity.amount <= st.max_supply.amount - st.supply.amount,
             "quantity exceeds available supply" );

      statstable.modify( st, same_payer, [&]( auto& s ) {
         s.supply += quantity;
      });

      add_balance( st.issuer, quantity, st.issuer );

      if( to != st.issuer ) {
         sub_balance( st.issuer, quantity );
         add_balance( to, quantity, st.issuer );
      }
   }

   // ---------------------------------------------------------------------------
   // retire — destroy tokens from the issuer's own balance
   // ---------------------------------------------------------------------------
   void token::retire( const asset& quantity, const std::string& memo ) {
      auto sym = quantity.symbol;
      check( sym.is_valid(), "invalid symbol" );
      check( memo.size() <= 256, "memo too long" );

      stats statstable( get_self(), sym.code().raw() );
      auto existing = statstable.require_find( sym.code().raw(), "symbol does not exist" );
      const auto& st = *existing;

      check_account_auth( st.issuer );
      check( quantity.is_valid(), "invalid quantity" );
      check( quantity.amount > 0, "must retire a positive quantity" );
      check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

      statstable.modify( st, same_payer, [&]( auto& s ) {
         s.supply -= quantity;
      });

      sub_balance( st.issuer, quantity );
   }

   // ---------------------------------------------------------------------------
   // transfer — move tokens between accounts
   // ---------------------------------------------------------------------------
   void token::transfer( const name& from, const name& to,
                          const asset& quantity, const std::string& memo )
   {
      check( from != to, "cannot transfer to self" );
      check_account_auth( from );
      check( eosio::is_account( to ), "to account does not exist" );

      auto sym = quantity.symbol.code();
      stats statstable( get_self(), sym.raw() );
      const auto& st = statstable.get( sym.raw(), "symbol does not exist" );

      eosio::require_recipient( from );
      eosio::require_recipient( to );

      check( quantity.is_valid(), "invalid quantity" );
      check( quantity.amount > 0, "must transfer a positive quantity" );
      check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
      check( memo.size() <= 256, "memo too long" );

      auto payer = eosio::has_auth( to ) ? to : from;

      sub_balance( from, quantity );
      add_balance( to, quantity, payer );
   }

   // ---------------------------------------------------------------------------
   // open — initialize a zero-balance row for an account
   // ---------------------------------------------------------------------------
   void token::open( const name& owner, const symbol& symbol, const name& ram_payer ) {
      require_auth( ram_payer );
      check( eosio::is_account( owner ), "owner does not exist" );

      auto sym_code_raw = symbol.code().raw();
      stats statstable( get_self(), sym_code_raw );
      const auto& st = statstable.get( sym_code_raw, "symbol does not exist" );
      check( st.supply.symbol == symbol, "symbol precision mismatch" );

      accounts acnts( get_self(), owner.value );
      auto it = acnts.find( sym_code_raw );
      if( it == acnts.end() ) {
         acnts.emplace( ram_payer, [&]( auto& a ) {
            a.balance = asset{ 0, symbol };
         });
      }
   }

   // ---------------------------------------------------------------------------
   // close — remove a zero-balance row, reclaim RAM
   // ---------------------------------------------------------------------------
   void token::close( const name& owner, const symbol& symbol ) {
      require_auth( owner );
      accounts acnts( get_self(), owner.value );
      auto it = acnts.require_find( symbol.code().raw(), "balance row missing" );
      check( it->balance.amount == 0,
             "cannot close: row still holds a non-zero balance" );
      acnts.erase( it );
   }

   // ---------------------------------------------------------------------------
   // Internal helpers
   // ---------------------------------------------------------------------------

   void token::sub_balance( const name& owner, const asset& value ) {
      accounts from_acnts( get_self(), owner.value );
      const auto& from = from_acnts.get( value.symbol.code().raw(),
                                          "no balance for owner/symbol" );
      check( from.balance.amount >= value.amount, "overdrawn balance" );
      from_acnts.modify( from, owner, [&]( auto& a ) {
         a.balance -= value;
      });
   }

   void token::add_balance( const name& owner, const asset& value, const name& ram_payer ) {
      accounts to_acnts( get_self(), owner.value );
      auto to = to_acnts.find( value.symbol.code().raw() );
      if( to == to_acnts.end() ) {
         to_acnts.emplace( ram_payer, [&]( auto& a ) {
            a.balance = value;
         });
      } else {
         to_acnts.modify( to, same_payer, [&]( auto& a ) {
            a.balance += value;
         });
      }
   }

} // namespace sikatoken
