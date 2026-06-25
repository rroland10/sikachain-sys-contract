// =============================================================================
// SikaChain — RAM Bancor exchange state implementation
// =============================================================================
// Constant-product market maker. The math here is verbatim from the EOSIO
// reference (it's a well-understood Bancor curve). The SikaChain-unique bit
// — the 0.5% trade fee split — lives in ram_bancor.cpp where buyram/sellram
// extract `compute_fee(quantity, ram_fee_bps)` and route it.
// =============================================================================

#include <sika.system/exchange_state.hpp>

#include <eosio/check.hpp>

#include <cmath>

namespace sikasystem {

   // ---------------------------------------------------------------------------
   // convert_to_exchange
   // Buyer pays `payment` of the reserve currency; we mint exchange tokens.
   //   T = S × ((1 + P/R)^F - 1)
   // where F = weight (0.5), R = reserve balance, S = supply, P = payment.
   // ---------------------------------------------------------------------------
   asset exchange_state::convert_to_exchange( connector& reserve, const asset& payment ) {
      const double S0 = supply.amount;
      const double R0 = reserve.balance.amount;
      const double dR = payment.amount;
      const double F  = reserve.weight;

      double dS = S0 * (std::pow(1.0 + dR/R0, F) - 1.0);
      if( dS < 0 ) dS = 0;
      const int64_t Idelta = static_cast<int64_t>(dS);

      reserve.balance.amount += payment.amount;
      supply.amount          += Idelta;
      return asset{ Idelta, supply.symbol };
   }

   // ---------------------------------------------------------------------------
   // convert_from_exchange
   // Seller burns `tokens` of exchange supply; we return reserve currency.
   //   P = R × (1 - (1 - T/S)^(1/F))
   // ---------------------------------------------------------------------------
   asset exchange_state::convert_from_exchange( connector& reserve, const asset& tokens ) {
      const double R0 = reserve.balance.amount;
      const double S0 = supply.amount;
      const double dT = tokens.amount;
      const double F  = reserve.weight;

      double dR = R0 * (1.0 - std::pow(1.0 - dT/S0, 1.0/F));
      if( dR < 0 ) dR = 0;
      const int64_t Pdelta = static_cast<int64_t>(dR);

      reserve.balance.amount -= Pdelta;
      supply.amount          -= tokens.amount;
      return asset{ Pdelta, reserve.balance.symbol };
   }

   // ---------------------------------------------------------------------------
   // convert — route through the appropriate reserve based on which symbol
   // is being paid in and which is being received.
   // ---------------------------------------------------------------------------
   asset exchange_state::convert( const asset& from, const symbol& to ) {
      const auto& sym_from = from.symbol;
      const auto& sym_to   = to;

      // Direct convert is preferred when both ends match a reserve.
      asset out;
      if( direct_convert( from, sym_to, out ) ) return out;

      // Two-step: from → exchange supply → to
      asset tokens{ 0, supply.symbol };
      if( sym_from == base.balance.symbol ) {
         tokens = convert_to_exchange( base, from );
      } else if( sym_from == quote.balance.symbol ) {
         tokens = convert_to_exchange( quote, from );
      } else {
         eosio::check( false, "convert: unsupported source symbol" );
      }

      if( sym_to == base.balance.symbol ) {
         return convert_from_exchange( base, tokens );
      } else if( sym_to == quote.balance.symbol ) {
         return convert_from_exchange( quote, tokens );
      }
      eosio::check( false, "convert: unsupported target symbol" );
      return asset{};
   }

   bool exchange_state::direct_convert( const asset& from, const symbol& to, asset& out ) {
      // Direct conversion not implemented for the SikaChain Bancor variant;
      // we always go through the intermediate supply token.
      (void)from; (void)to; (void)out;
      return false;
   }

   // ---------------------------------------------------------------------------
   // compute_fee — extract the trade fee in basis points from a quantity.
   //   fee = quantity × bps / 10000
   // Returned asset has the same symbol as the input.
   // ---------------------------------------------------------------------------
   asset exchange_state::compute_fee( const asset& quantity, int64_t fee_bps ) {
      int64_t fee_amt = (quantity.amount * fee_bps) / 10000;
      // Round up so we don't accidentally undercharge on small trades
      if( (quantity.amount * fee_bps) % 10000 != 0 ) fee_amt += 1;
      return asset{ fee_amt, quantity.symbol };
   }

} // namespace sikasystem
