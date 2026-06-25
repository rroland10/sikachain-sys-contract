// =============================================================================
// SikaChain — shared FX helpers (reads sika.treas::fxrates table)
// =============================================================================
#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/time.hpp>

namespace sikafx {

   using eosio::asset;
   using eosio::check;
   using eosio::current_time_point;
   using eosio::name;
   using eosio::symbol;
   using eosio::time_point;

   static constexpr uint64_t fx_ppm_scale = 1'000'000;
   static constexpr symbol   cusd_symbol  = symbol{ "CUSD", 4 };

   struct [[eosio::table]] fx_rate_row {
      symbol   local_symbol;
      uint64_t cusd_ppm = fx_ppm_scale;
      time_point updated_at;
      time_point expires_at;

      uint64_t primary_key() const { return local_symbol.code().raw(); }
   };
   using fx_rate_table = eosio::multi_index<"fxquotes"_n, fx_rate_row>;

   inline bool fx_rate_fresh( const fx_rate_row& row ) {
      if( row.expires_at.time_since_epoch().count() == 0 ) {
         return true;
      }
      return current_time_point() < row.expires_at;
   }

   inline int64_t local_to_cusd_atoms( name treas,
                                       const symbol& local,
                                       int64_t local_atoms )
   {
      if( local_atoms <= 0 ) {
         return 0;
      }
      if( local == cusd_symbol ) {
         return local_atoms;
      }

      fx_rate_table fx( treas, treas.value );
      auto it = fx.find( local.code().raw() );
      const uint64_t ppm = it != fx.end() && fx_rate_fresh( *it )
         ? it->cusd_ppm
         : fx_ppm_scale;
      return static_cast<int64_t>(
         (static_cast<__int128>(local_atoms) * ppm) / fx_ppm_scale
      );
   }

   inline int64_t cusd_to_local_atoms( name treas,
                                         const symbol& local,
                                         int64_t cusd_atoms )
   {
      if( cusd_atoms <= 0 ) {
         return 0;
      }
      if( local == cusd_symbol ) {
         return cusd_atoms;
      }

      fx_rate_table fx( treas, treas.value );
      auto it = fx.find( local.code().raw() );
      const uint64_t ppm = it != fx.end() && fx_rate_fresh( *it )
         ? it->cusd_ppm
         : fx_ppm_scale;
      check( ppm > 0, "fx rate must be positive" );
      return static_cast<int64_t>(
         (static_cast<__int128>(cusd_atoms) * fx_ppm_scale) / ppm
      );
   }

   inline asset local_to_cusd( name treas, const asset& local ) {
      return asset{ local_to_cusd_atoms( treas, local.symbol, local.amount ),
                    cusd_symbol };
   }

   inline asset cusd_to_local( name treas, const symbol& local, const asset& cusd ) {
      check( cusd.symbol == cusd_symbol, "amount must be CUSD" );
      return asset{ cusd_to_local_atoms( treas, local, cusd.amount ), local };
   }

} // namespace sikafx
