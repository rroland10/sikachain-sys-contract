// =============================================================================
// SikaChain — RAM Bancor exchange state
// =============================================================================
// Constant-product market maker for the RAM market. The base is a Bancor
// reserve seeded with 64 GiB of RAM and ~ 1B SIKA. SikaChain adds a 0.5%
// trade fee on top of the base curve (Article VI): 50% goes to the REX pool
// as cGHS yield, 50% is burned forever.
// =============================================================================

#pragma once

#include <eosio/asset.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/symbol.hpp>

namespace sikasystem {

   using eosio::asset;
   using eosio::symbol;

   struct [[eosio::table, eosio::contract("sika.system")]] exchange_state {
      asset       supply;

      struct connector {
         asset    balance;
         double   weight = 0.5;   // 50/50 connector — standard Bancor

         EOSLIB_SERIALIZE( connector, (balance)(weight) )
      };

      connector   base;            // RAM connector
      connector   quote;           // SIKA connector

      uint64_t primary_key() const { return supply.symbol.raw(); }

      asset convert_to_exchange( connector& reserve, const asset& payment );
      asset convert_from_exchange( connector& reserve, const asset& tokens );
      asset convert( const asset& from, const symbol& to );
      bool  direct_convert( const asset& from, const symbol& to, asset& out );

      // Returns the base reserve fee that should be applied on a trade of
      // `quantity`. Caller is responsible for splitting between REX and burn.
      static asset compute_fee( const asset& quantity, int64_t fee_bps );

      EOSLIB_SERIALIZE( exchange_state, (supply)(base)(quote) )
   };

   using rammarket = eosio::multi_index<"rammarket"_n, exchange_state>;

} // namespace sikasystem
