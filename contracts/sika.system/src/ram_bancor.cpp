// =============================================================================
// SikaChain — RAM Market (Article VI)
// =============================================================================
// Wraps the Bancor exchange_state with SikaChain's 0.5% trade fee on each
// buyram and sellram. The fee splits 50/50:
//   - 50% → cghs_yield_pool (paid to REX holders)
//   - 50% → sika.burn (permanently removed from circulation)
//
// IMPORTANT: The fee is denominated in SIKA. Half is converted to cGHS via
// an inline transfer routed through the SIKA/cGHS swap pair. For initial
// contract, we simplify: we transfer the full fee to sika.boost (which can
// hold both tokens) and let the boost contract perform the swap before
// crediting cghs_yield_pool via crediboost.
//
// In production this would route through an on-chain DEX. For MVP it's fine
// for sika.boost to hold the swap responsibility because it already has
// keeper logic for periodic disbursements.
// =============================================================================

#include <sika.system/sika.system.hpp>
#include <sika.accounts.hpp>

#include <eosio/action.hpp>
#include <eosio/print.hpp>

namespace sikasystem {

   // ---------------------------------------------------------------------------
   // buyram — payer transfers SIKA, receiver gets RAM bytes.
   //   1. Take 0.5% fee off the top
   //   2. Run the Bancor curve on the remaining 99.5%
   //   3. Credit receiver with RAM bytes
   //   4. Split the fee: half to sika.boost (eventually → cGHS yield), half burn
   // ---------------------------------------------------------------------------
   void system_contract::buyram( const name& payer,
                                  const name& receiver,
                                  const asset& quant )
   {
      require_auth( payer );
      check( quant.symbol == sika_symbol, "ram purchases must be in SIKA" );
      check( quant.amount > 0, "must purchase a positive amount" );

      buy_ram( payer, receiver, quant );
   }

   void system_contract::buyrambytes( const name& payer,
                                       const name& receiver,
                                       uint32_t bytes )
   {
      require_auth( payer );
      check( bytes > 0, "must purchase positive byte count" );

      // Convert byte count to SIKA quantity via the current curve, including
      // the fee adjustment (caller specified bytes-received, we work back to
      // SIKA-required).
      auto rm_it = _rammarket.require_find( ramcore_symbol.raw(),
                                             "ram market not initialized" );

      // Probe: how much SIKA to get `bytes` of RAM?
      asset bytes_needed{ static_cast<int64_t>(bytes), ram_symbol };
      exchange_state market = *rm_it;
      asset sika_required = market.convert( bytes_needed, sika_symbol );

      // Re-undo by adding the 0.5% fee back on top
      sika_required.amount = (sika_required.amount * 10000) / (10000 - ram_fee_bps);

      buy_ram( payer, receiver, sika_required );
   }

   // Shared helper: the actual buy logic.
   void system_contract::buy_ram( const name& payer,
                                   const name& receiver,
                                   asset sika_payload )
   {
      // 1. Calculate fee
      asset fee = exchange_state::compute_fee( sika_payload, ram_fee_bps );

      // 2. Move SIKA: full quantity from payer → eosio (this contract holds RAM market)
      eosio::action(
         { eosio::permission_level{ payer, "active"_n } },
         sikaaccounts::TOKEN, "transfer"_n,
         std::make_tuple( payer, get_self(), sika_payload,
                          std::string("buy ram") )
      ).send();

      // 3. Run the curve on (quant - fee)
      asset sika_for_curve = sika_payload;
      sika_for_curve.amount -= fee.amount;

      int64_t ram_received = 0;
      auto rm_it = _rammarket.require_find( ramcore_symbol.raw(),
                                             "ram market not initialized" );
      _rammarket.modify( rm_it, same_payer, [&]( auto& m ) {
         asset ram_out = m.convert( sika_for_curve, ram_symbol );
         ram_received = ram_out.amount;
      });
      check( ram_received > 0, "would buy zero ram — increase quantity" );

      // 4. Credit RAM bytes to receiver
      user_resources_table userres( get_self(), receiver.value );
      auto ur_it = userres.find( receiver.value );
      if( ur_it == userres.end() ) {
         userres.emplace( payer, [&]( auto& r ) {
            r.owner      = receiver;
            r.net_weight = asset{ 0, sika_symbol };
            r.cpu_weight = asset{ 0, sika_symbol };
            r.ram_bytes  = ram_received;
         });
      } else {
         userres.modify( ur_it, same_payer, [&]( auto& r ) {
            r.ram_bytes += ram_received;
         });
      }

      // Apply on-chain RAM limit via intrinsic
      eosio::set_resource_limits( receiver,
         (ur_it != userres.end() ? ur_it->ram_bytes : ram_received),
         -1, -1 );

      _gstate.total_ram_bytes_reserved += static_cast<uint64_t>(ram_received);

      // 5. Route the fee — Article VI: 50% REX, 50% burn
      route_ram_fee( fee );
   }

   // ---------------------------------------------------------------------------
   // sellram — receiver gets SIKA in exchange for RAM bytes, minus 0.5% fee
   // ---------------------------------------------------------------------------
   void system_contract::sellram( const name& account, int64_t bytes ) {
      require_auth( account );
      check( bytes > 0, "must sell a positive byte count" );

      user_resources_table userres( get_self(), account.value );
      auto ur_it = userres.require_find( account.value, "no RAM to sell" );
      check( ur_it->ram_bytes >= bytes, "insufficient RAM" );

      // Run the curve in reverse
      int64_t sika_returned = 0;
      auto rm_it = _rammarket.require_find( ramcore_symbol.raw(), "ram market missing" );
      _rammarket.modify( rm_it, same_payer, [&]( auto& m ) {
         asset bytes_in{ bytes, ram_symbol };
         asset sika_out = m.convert( bytes_in, sika_symbol );
         sika_returned = sika_out.amount;
      });
      check( sika_returned > 0, "would receive zero SIKA — bytes too small" );

      // Apply 0.5% fee on the SIKA proceeds
      asset proceeds{ sika_returned, sika_symbol };
      asset fee = exchange_state::compute_fee( proceeds, ram_fee_bps );
      asset net_proceeds = proceeds;
      net_proceeds.amount -= fee.amount;

      // Update user's RAM accounting
      userres.modify( ur_it, same_payer, [&]( auto& r ) {
         r.ram_bytes -= bytes;
      });
      eosio::set_resource_limits( account, ur_it->ram_bytes, -1, -1 );

      _gstate.total_ram_bytes_reserved -= static_cast<uint64_t>(bytes);

      // Pay user the net SIKA
      eosio::action(
         { eosio::permission_level{ get_self(), "active"_n } },
         sikaaccounts::TOKEN, "transfer"_n,
         std::make_tuple( get_self(), account, net_proceeds,
                          std::string("sell ram") )
      ).send();

      // Route the fee
      route_ram_fee( fee );
   }

   // ---------------------------------------------------------------------------
   // route_ram_fee — Article VI fee split (v0.2 §6.2).
   //   - 50% → REX pool SIKA compound (raises share value; never sold)
   //   - 50% → sika.burn (permanently destroyed)
   // ---------------------------------------------------------------------------
   void system_contract::route_ram_fee( const asset& fee ) {
      if( fee.amount == 0 ) return;

      int64_t to_rex  = (fee.amount * ram_fee_to_rex_bps) / 10000;
      int64_t to_burn = fee.amount - to_rex;

      if( to_rex > 0 ) {
         compound_rex_sika( asset{ to_rex, sika_symbol } );
      }
      if( to_burn > 0 ) {
         eosio::action(
            { eosio::permission_level{ get_self(), "active"_n } },
            sikaaccounts::TOKEN, "transfer"_n,
            std::make_tuple( get_self(), sikaaccounts::BURN,
                             asset{ to_burn, sika_symbol },
                             std::string("RAM fee → burn (Article VI)") )
         ).send();
         _gstate.total_burned += to_burn;
      }
   }

} // namespace sikasystem
