// =============================================================================
// SikaChain — on-chain account names (single source of truth)
// =============================================================================
// Protocol account: `eosio` (stock Spring) or `sikaio` (SIKACHAIN build).
// System contract host: `eosio` (stock) or `sika` (SIKACHAIN — sika.system WASM).
// Rebuild contracts with SIKACHAIN=1 ./build.sh when using -DSIKACHAIN=ON Spring.
// =============================================================================

#pragma once

#include <eosio/name.hpp>

namespace sikaaccounts {

#ifdef SIKACHAIN
   static constexpr eosio::name PROTOCOL{ "sikaio"_n };
   static constexpr eosio::name SYSTEM{ "sika"_n };
#else
   static constexpr eosio::name PROTOCOL{ "eosio"_n };
   static constexpr eosio::name SYSTEM{ "eosio"_n };
#endif

   static constexpr eosio::name TOKEN{ "sika.token"_n };
   static constexpr eosio::name REX{ "sika.rex"_n };

   static constexpr eosio::name REP{ "sika.rep"_n };
   static constexpr eosio::name GUARD{ "sika.guard"_n };
   static constexpr eosio::name RULES{ "sika.rules"_n };
   static constexpr eosio::name ISSUE{ "sika.issue"_n };
   static constexpr eosio::name MSIG{ "sika.msig"_n };

   static constexpr eosio::name BOOST{ "sika.boost"_n };
   static constexpr eosio::name BPPAY{ "sika.bppay"_n };
   static constexpr eosio::name BURN{ "sika.burn"_n };

   // Settlement layer (BP compensation v0.2 — accounts reserved; sika.treas WASM TBD)
   static constexpr eosio::name TREAS{ "sika.treas"_n };
   static constexpr eosio::name COST{ "sika.cost"_n };
   static constexpr eosio::name USD{ "sika.usd"_n };
   static constexpr eosio::name ORACLE{ "sika.oracle"_n };

} // namespace sikaaccounts
