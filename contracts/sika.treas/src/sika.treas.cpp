// =============================================================================
// SikaChain — sika.treas implementation (v0.2 scaffold)
// =============================================================================

#include <sika.treas/sika.treas.hpp>
#include <sika.accounts.hpp>
#include <sika.fx.hpp>

#include <eosio/action.hpp>
#include <eosio/crypto.hpp>
#include <eosio/print.hpp>
#include <eosio/system.hpp>

#include <cstring>

namespace sikatreas {

   using eosio::checksum256;
   using eosio::public_key;
   using eosio::signature;

   using sikaaccounts::ORACLE;
   using sikaaccounts::RULES;
   using sikaaccounts::SYSTEM;
   using sikaaccounts::TOKEN;
   using sikafx::cusd_symbol;
   using sikafx::cusd_to_local;
   using sikafx::local_to_cusd;
   using sikafx::local_to_cusd_atoms;

   static constexpr symbol cghs_symbol  = symbol{ "CGHS", 4 };
   static constexpr symbol ggold_symbol = symbol{ "GGOLD", 4 };

   void treas_contract::require_governance( name authority ) const {
      check( authority == SYSTEM || authority == RULES,
             "governance authority must be system or sika.rules" );
      require_auth( authority );
   }

   void treas_contract::require_initialized() const {
      params_singleton params( get_self(), get_self().value );
      check( params.exists() && params.get().initialized,
             "sika.treas not initialized" );
   }

   void treas_contract::upsert_fx( const symbol& local_symbol,
                                   uint64_t cusd_ppm,
                                   uint32_t ttl_seconds,
                                   const char* label )
   {
      check( local_symbol.precision() == 4, "local symbol must have 4 decimals" );
      check( local_symbol != cusd_symbol, "use CUSD directly" );
      check( cusd_ppm > 0, "fx rate must be positive" );

      fx_rate_table fx( get_self(), get_self().value );
      const time_point now = current_time_point();
      const time_point expires = ttl_seconds > 0
         ? time_point{ now.time_since_epoch()
            + eosio::microseconds{ static_cast<int64_t>(ttl_seconds) * 1'000'000 } }
         : time_point{};

      auto it = fx.find( local_symbol.code().raw() );
      if( it == fx.end() ) {
         fx.emplace( get_self(), [&]( auto& row ) {
            row.local_symbol = local_symbol;
            row.cusd_ppm     = cusd_ppm;
            row.updated_at   = now;
            row.expires_at   = expires;
         });
      } else {
         fx.modify( it, same_payer, [&]( auto& row ) {
            row.cusd_ppm   = cusd_ppm;
            row.updated_at = now;
            row.expires_at = expires;
         });
      }

      eosio::print( label, " ", local_symbol, " → CUSD ppm ", cusd_ppm, "\n" );
   }

   void treas_contract::verify_fx_attestation( const symbol& local_symbol,
                                               uint64_t cusd_ppm,
                                               uint32_t ttl_seconds,
                                               uint64_t published_at,
                                               const signature& sig ) const
   {
      check( local_symbol.precision() == 4, "local symbol must have 4 decimals" );
      check( local_symbol != cusd_symbol, "use CUSD directly" );
      check( cusd_ppm > 0, "fx rate must be positive" );
      check( published_at > 0, "published_at required" );

      oracle_singleton oracle( get_self(), get_self().value );
      check( oracle.exists(), "oracle key not configured" );
      const oracle_row cfg = oracle.get();
      check( cfg.attest_key != public_key{}, "oracle key not configured" );

      const time_point pub = time_point{
         eosio::microseconds{ static_cast<int64_t>(published_at) }
      };
      const time_point now = current_time_point();
      check( pub <= now + eosio::seconds(300), "published_at too far in future" );
      check( now <= pub + eosio::seconds(3600), "attestation stale" );

      uint64_t parts[4];
      parts[0] = local_symbol.code().raw();
      parts[1] = cusd_ppm;
      parts[2] = ttl_seconds;
      parts[3] = published_at;

      const checksum256 digest = eosio::sha256(
         reinterpret_cast<const char*>( parts ), sizeof( parts ) );
      eosio::assert_recover_key( digest, sig, cfg.attest_key );
   }

   void treas_contract::setoraclekey( name authority,
                                      const public_key& attest_key,
                                      bool require_signed_push )
   {
      require_governance( authority );
      require_initialized();
      check( attest_key != public_key{}, "attest_key required" );

      oracle_singleton oracle( get_self(), get_self().value );
      oracle_row row;
      row.attest_key           = attest_key;
      row.require_signed_push  = require_signed_push;
      oracle.set( row, get_self() );

      eosio::print( "setoraclekey require_signed=", require_signed_push, "\n" );
   }

   void treas_contract::setfx( name authority,
                               const symbol& local_symbol,
                               uint64_t cusd_ppm,
                               uint32_t ttl_seconds )
   {
      require_governance( authority );
      require_initialized();
      upsert_fx( local_symbol, cusd_ppm, ttl_seconds, "setfx" );
   }

   void treas_contract::pushfx( const symbol& local_symbol,
                                uint64_t cusd_ppm,
                                uint32_t ttl_seconds )
   {
      require_auth( ORACLE );
      require_initialized();

      oracle_singleton oracle( get_self(), get_self().value );
      if( oracle.exists() && oracle.get().require_signed_push ) {
         check( false, "signed push required — use pushfxsig" );
      }

      upsert_fx( local_symbol, cusd_ppm, ttl_seconds, "pushfx" );
   }

   void treas_contract::pushfxsig( const symbol& local_symbol,
                                   uint64_t cusd_ppm,
                                   uint32_t ttl_seconds,
                                   uint64_t published_at,
                                   const signature& sig )
   {
      require_initialized();
      verify_fx_attestation( local_symbol, cusd_ppm, ttl_seconds, published_at, sig );
      upsert_fx( local_symbol, cusd_ppm, ttl_seconds, "pushfxsig" );
   }

   void treas_contract::init() {
      require_auth( SYSTEM );

      params_singleton params( get_self(), get_self().value );
      check( !params.exists() || !params.get().initialized,
             "already initialized" );

      params_row p;
      p.cost_recovery_cusd = asset{ 0, cusd_symbol };
      p.initialized        = true;
      params.set( p, get_self() );

      reserve_singleton reserve( get_self(), get_self().value );
      if( !reserve.exists() ) {
         reserve_row r;
         r.cusd_balance     = asset{ 0, cusd_symbol };
         r.ggold_balance    = asset{ 0, ggold_symbol };
         r.reserve_gold_bps = 3000;
         reserve.set( r, get_self() );
      }

      eosio::print( "sika.treas initialized (scaffold v0.2)\n" );
   }

   void treas_contract::creditreserve( const asset& quantity ) {
      require_auth( SYSTEM );
      require_initialized();
      check( quantity.symbol == cusd_symbol, "must credit CUSD" );
      check( quantity.amount > 0, "must be positive" );

      reserve_singleton reserve( get_self(), get_self().value );
      reserve_row r = reserve.get();
      r.cusd_balance += quantity;
      reserve.set( r, get_self() );

      eosio::print( "creditreserve +", quantity, " (balance ", r.cusd_balance, ")\n" );
   }

   void treas_contract::setparams( name authority,
                                     uint16_t sweep_slice_bps,
                                     const asset& cost_recovery_cusd,
                                     uint16_t max_subsidy_per_market_bps,
                                     uint16_t fee_to_yield_bps,
                                     uint16_t reserve_gold_bps )
   {
      require_governance( authority );
      check( sweep_slice_bps <= 10'000, "sweep_slice_bps out of range" );
      check( max_subsidy_per_market_bps <= 10'000,
             "max_subsidy_per_market_bps out of range" );
      check( fee_to_yield_bps <= 10'000, "fee_to_yield_bps out of range" );
      check( reserve_gold_bps <= 10'000, "reserve_gold_bps out of range" );
      check( cost_recovery_cusd.symbol == cusd_symbol,
             "cost_recovery must be CUSD" );

      params_singleton params( get_self(), get_self().value );
      params_row p = params.exists() ? params.get() : params_row{};
      p.sweep_slice_bps            = sweep_slice_bps;
      p.cost_recovery_cusd         = cost_recovery_cusd;
      p.max_subsidy_per_market_bps = max_subsidy_per_market_bps;
      p.fee_to_yield_bps           = fee_to_yield_bps;
      p.initialized                = true;
      params.set( p, get_self() );

      reserve_singleton reserve( get_self(), get_self().value );
      reserve_row r = reserve.exists() ? reserve.get() : reserve_row{};
      r.reserve_gold_bps = reserve_gold_bps;
      reserve.set( r, get_self() );
   }

   void treas_contract::accruefee( name market, const asset& local_quantity ) {
      require_auth( SYSTEM );
      require_initialized();
      check( market.value != 0, "invalid market" );
      check( local_quantity.amount > 0, "fee must be positive" );

      market_pnl_table pnl( get_self(), get_self().value );
      auto it = pnl.find( market.value );
      if( it == pnl.end() ) {
         pnl.emplace( get_self(), [&]( auto& row ) {
            row.market                  = market;
            row.fees_collected_local    = local_quantity;
            row.cost_allocated_local    = asset{ 0, local_quantity.symbol };
            row.net_contribution_local  = local_quantity;
            row.fees_collected_cusd     = asset{ 0, cusd_symbol };
            row.cost_allocated_cusd     = asset{ 0, cusd_symbol };
            row.net_contribution_cusd   = asset{ 0, cusd_symbol };
            row.subsidy_in              = asset{ 0, local_quantity.symbol };
            row.subsidy_out             = asset{ 0, local_quantity.symbol };
            row.last_accrual_at         = current_time_point();
         });
      } else {
         pnl.modify( it, same_payer, [&]( auto& row ) {
            row.fees_collected_local.amount += local_quantity.amount;
            row.net_contribution_local.amount += local_quantity.amount;
            row.last_accrual_at = current_time_point();
         });
      }

      const asset ref_fee = local_to_cusd( get_self(), local_quantity );
      eosio::action(
         { eosio::permission_level{ get_self(), "active"_n } },
         SYSTEM, "accruepoch"_n,
         std::make_tuple( ref_fee )
      ).send();

      eosio::print( "accruefee ", market, " +", local_quantity, "\n" );
   }

   void treas_contract::paycost( name producer ) {
      require_auth( SYSTEM );

      params_singleton params( get_self(), get_self().value );
      if( !params.exists() || !params.get().initialized ) {
         return;
      }

      auto prod = producer;
      check( prod.value != 0, "invalid producer" );

      const asset payout = params.get().cost_recovery_cusd;
      if( payout.amount <= 0 ) {
         return;
      }

      reserve_singleton reserve( get_self(), get_self().value );
      if( !reserve.exists() ) {
         eosio::print( "paycost skipped for ", prod, ": reserve not initialized\n" );
         return;
      }
      reserve_row r = reserve.get();
      if( r.cusd_balance.amount < payout.amount ) {
         eosio::print( "paycost skipped for ", prod, ": reserve ",
                       r.cusd_balance, " < ", payout, "\n" );
         return;
      }

      r.cusd_balance -= payout;
      reserve.set( r, get_self() );

      eosio::action(
         { eosio::permission_level{ get_self(), "active"_n } },
         TOKEN, "transfer"_n,
         std::make_tuple(
            get_self(), prod, payout,
            std::string( "Tier-1 BP cost recovery (sika.treas)" )
         )
      ).send();

      eosio::print( "paycost ", prod, " ", payout, "\n" );
   }

   void treas_contract::sweep( name market ) {
      require_initialized();
      require_auth( SYSTEM );

      check( market.value != 0, "invalid market" );

      params_singleton params( get_self(), get_self().value );
      const params_row p = params.get();
      if( p.sweep_slice_bps == 0 ) {
         return;
      }

      market_pnl_table pnl( get_self(), get_self().value );
      auto it = pnl.find( market.value );
      check( it != pnl.end(), "market ledger row missing" );

      const int64_t target_swept = static_cast<int64_t>(
         (static_cast<__int128>(
            local_to_cusd_atoms( get_self(), it->fees_collected_local.symbol,
                                 it->fees_collected_local.amount ))
          * p.sweep_slice_bps)
         / 10'000
      );
      const int64_t already_swept = it->fees_collected_cusd.amount;
      const int64_t to_sweep = target_swept - already_swept;
      if( to_sweep <= 0 ) {
         eosio::print( "sweep ", market, ": nothing to sweep\n" );
         return;
      }

      const asset cusd_slice{ to_sweep, cusd_symbol };
      const int64_t yield_atoms = p.fee_to_yield_bps > 0
         ? static_cast<int64_t>(
            (static_cast<__int128>(to_sweep) * p.fee_to_yield_bps) / 10'000 )
         : 0;
      const int64_t reserve_atoms = to_sweep - yield_atoms;
      const asset reserve_slice{ reserve_atoms, cusd_symbol };
      const asset yield_slice{ yield_atoms, cusd_symbol };

      pnl.modify( it, same_payer, [&]( auto& row ) {
         row.fees_collected_cusd.amount   += to_sweep;
         row.net_contribution_cusd.amount += to_sweep;
      });

      reserve_singleton reserve( get_self(), get_self().value );
      if( reserve.exists() && reserve_atoms > 0 ) {
         reserve_row r = reserve.get();
         r.cusd_balance += reserve_slice;
         reserve.set( r, get_self() );
      }

      if( yield_atoms > 0 ) {
         eosio::action(
            { eosio::permission_level{ get_self(), "active"_n } },
            SYSTEM, "credyield"_n,
            std::make_tuple( yield_slice )
         ).send();
      }

      eosio::action(
         { eosio::permission_level{ get_self(), "active"_n } },
         SYSTEM, "accruepoch"_n,
         std::make_tuple( cusd_slice )
      ).send();

      eosio::print( "sweep ", market, " +", cusd_slice,
                    " reserve ", reserve_slice, " yield ", yield_slice,
                    " (target ", target_swept, ")\n" );
   }

   void treas_contract::assert_payout_allowed( name market,
                                               const symbol& payout_currency ) const
   {
      check( market.value != 0, "market required for payout compliance" );

      marketpref_table prefs( get_self(), get_self().value );
      auto it = prefs.find( market.value );
      check( it != prefs.end(), "market payout prefs not configured" );

      const marketpref_row& pref = *it;

      if( payout_currency == cusd_symbol ) {
         check( pref.compliance_ready && pref.allow_cusd,
                "CUSD yield payout not licensed for this market" );
         return;
      }
      if( payout_currency == ggold_symbol ) {
         check( pref.compliance_ready && pref.allow_ggold,
                "gGOLD yield payout not licensed for this market" );
         return;
      }
      check( payout_currency == pref.local_symbol,
             "payout must be market-local stable" );
   }

   void treas_contract::setmarketpref( name authority,
                                       name market,
                                       const symbol& local_symbol,
                                       bool allow_cusd,
                                       bool allow_ggold,
                                       bool compliance_ready )
   {
      require_governance( authority );
      require_initialized();
      check( market.value != 0, "invalid market" );
      check( local_symbol.precision() == 4, "local symbol must have 4 decimals" );
      check( local_symbol != cusd_symbol && local_symbol != ggold_symbol,
             "local symbol must not be CUSD or gGOLD" );

      marketpref_table prefs( get_self(), get_self().value );
      auto it = prefs.find( market.value );
      if( it == prefs.end() ) {
         prefs.emplace( authority, [&]( auto& row ) {
            row.market            = market;
            row.local_symbol      = local_symbol;
            row.allow_cusd        = allow_cusd;
            row.allow_ggold       = allow_ggold;
            row.compliance_ready  = compliance_ready;
         });
      } else {
         prefs.modify( it, same_payer, [&]( auto& row ) {
            row.local_symbol     = local_symbol;
            row.allow_cusd       = allow_cusd;
            row.allow_ggold      = allow_ggold;
            row.compliance_ready = compliance_ready;
         });
      }

      eosio::print( "setmarketpref ", market, " local=", local_symbol,
                    " cusd=", allow_cusd, " ggold=", allow_ggold,
                    " ready=", compliance_ready, "\n" );
   }

   void treas_contract::setpayoutpref( name owner,
                                       name market,
                                       const symbol& payout_currency )
   {
      require_auth( owner );
      require_initialized();
      check( market.value != 0, "market required" );
      check( payout_currency.precision() == 4,
             "payout currency must have 4 decimals" );

      assert_payout_allowed( market, payout_currency );

      user_payout_table prefs( get_self(), get_self().value );
      auto it = prefs.find( owner.value );
      if( it == prefs.end() ) {
         prefs.emplace( owner, [&]( auto& row ) {
            row.owner            = owner;
            row.market           = market;
            row.payout_currency  = payout_currency;
         });
      } else {
         prefs.modify( it, owner, [&]( auto& row ) {
            row.market          = market;
            row.payout_currency = payout_currency;
         });
      }

      eosio::print( "setpayoutpref ", owner, " ", market, " → ", payout_currency, "\n" );
   }

   void treas_contract::clearyield( name owner,
                                    const asset& cusd_amount,
                                    const symbol& payout_currency,
                                    name market )
   {
      require_auth( SYSTEM );

      params_singleton params( get_self(), get_self().value );
      if( !params.exists() || !params.get().initialized ) {
         check( false, "sika.treas not initialized" );
      }

      check( owner.value != 0, "invalid owner" );
      check( cusd_amount.symbol == cusd_symbol, "amount must be CUSD reference" );
      check( cusd_amount.amount > 0, "must be positive" );
      check( payout_currency.precision() == 4,
             "payout currency must have 4 decimals" );

      assert_payout_allowed( market, payout_currency );

      asset payout;
      if( payout_currency == cusd_symbol ) {
         payout = cusd_amount;
         reserve_singleton reserve( get_self(), get_self().value );
         if( reserve.exists() ) {
            reserve_row r = reserve.get();
            if( r.cusd_balance.amount >= payout.amount ) {
               r.cusd_balance -= payout;
               reserve.set( r, get_self() );
            }
         }
      } else if( payout_currency == cghs_symbol ) {
         payout = cusd_to_local( get_self(), cghs_symbol, cusd_amount );
         reserve_singleton reserve( get_self(), get_self().value );
         if( reserve.exists() ) {
            reserve_row r = reserve.get();
            if( r.cusd_balance.amount >= cusd_amount.amount ) {
               r.cusd_balance -= cusd_amount;
               reserve.set( r, get_self() );
            }
         }
      } else {
         payout = cusd_to_local( get_self(), payout_currency, cusd_amount );
         reserve_singleton reserve( get_self(), get_self().value );
         if( reserve.exists() ) {
            reserve_row r = reserve.get();
            if( r.cusd_balance.amount >= cusd_amount.amount ) {
               r.cusd_balance -= cusd_amount;
               reserve.set( r, get_self() );
            }
         }
      }

      eosio::action(
         { eosio::permission_level{ get_self(), "active"_n } },
         TOKEN, "transfer"_n,
         std::make_tuple(
            get_self(), owner, payout,
            std::string( "REX yield settlement (sika.treas)" )
         )
      ).send();

      eosio::print( "clearyield ", owner, " ", cusd_amount, " → ", payout, "\n" );
   }

   void treas_contract::subsidize( name from_market,
                                   name to_market,
                                   const asset& amount )
   {
      require_auth( SYSTEM );
      require_initialized();
      check( from_market.value != 0 && to_market.value != 0, "invalid market" );
      check( from_market != to_market, "markets must differ" );
      check( amount.symbol == cusd_symbol, "subsidy must be CUSD reference" );
      check( amount.amount > 0, "subsidy must be positive" );

      params_singleton params( get_self(), get_self().value );
      const params_row p = params.get();
      check( p.max_subsidy_per_market_bps > 0,
             "subsidies disabled (max_subsidy_per_market_bps is zero)" );

      market_pnl_table pnl( get_self(), get_self().value );
      auto from_it = pnl.find( from_market.value );
      auto to_it   = pnl.find( to_market.value );
      check( from_it != pnl.end(), "from_market ledger row missing" );
      check( to_it != pnl.end(), "to_market ledger row missing" );

      const int64_t fee_base_cusd = local_to_cusd_atoms(
         get_self(),
         from_it->fees_collected_local.symbol,
         from_it->fees_collected_local.amount );
      const int64_t cap = static_cast<int64_t>(
         (static_cast<__int128>(fee_base_cusd) * p.max_subsidy_per_market_bps)
         / 10'000
      );
      const int64_t already_out = local_to_cusd_atoms(
         get_self(), from_it->subsidy_out.symbol, from_it->subsidy_out.amount );
      check( already_out + amount.amount <= cap,
             "subsidy cap exceeded for donor market" );
      const int64_t donor_net_cusd = local_to_cusd_atoms(
         get_self(),
         from_it->net_contribution_local.symbol,
         from_it->net_contribution_local.amount );
      check( donor_net_cusd >= amount.amount,
             "donor market net contribution insufficient" );

      const asset local_out = cusd_to_local(
         get_self(), from_it->fees_collected_local.symbol, amount );
      const asset local_in = cusd_to_local(
         get_self(), to_it->fees_collected_local.symbol, amount );

      pnl.modify( from_it, same_payer, [&]( auto& row ) {
         row.subsidy_out.amount += local_out.amount;
         row.net_contribution_local.amount -= local_out.amount;
         row.net_contribution_cusd -= amount;
      });
      pnl.modify( to_it, same_payer, [&]( auto& row ) {
         row.subsidy_in.amount += local_in.amount;
         row.net_contribution_local.amount += local_in.amount;
         row.net_contribution_cusd += amount;
      });

      eosio::print( "subsidize ", from_market, " → ", to_market, " ", amount, "\n" );
   }

   void treas_contract::rebalance() {
      require_auth( SYSTEM );
      require_initialized();

      reserve_singleton reserve( get_self(), get_self().value );
      if( !reserve.exists() ) {
         eosio::print( "rebalance: reserve not initialized\n" );
         return;
      }

      reserve_row r = reserve.get();
      const int64_t total = r.cusd_balance.amount + r.ggold_balance.amount;
      if( total <= 0 ) {
         eosio::print( "rebalance: nothing to rebalance\n" );
         return;
      }

      const int64_t target_ggold = static_cast<int64_t>(
         (static_cast<__int128>(total) * r.reserve_gold_bps) / 10'000
      );
      const int64_t target_cusd = total - target_ggold;
      const int64_t drift       = r.ggold_balance.amount - target_ggold;

      const int64_t band = total / 100;
      if( drift <= band && drift >= -band ) {
         eosio::print( "rebalance: within band (drift ", drift, ")\n" );
         return;
      }

      r.cusd_balance  = asset{ target_cusd, cusd_symbol };
      r.ggold_balance = asset{ target_ggold, ggold_symbol };
      reserve.set( r, get_self() );

      eosio::print( "rebalance: cUSD ", target_cusd, " / gGOLD ", target_ggold,
                    " (target ", r.reserve_gold_bps, " bps gold)\n" );
   }

} // namespace sikatreas
