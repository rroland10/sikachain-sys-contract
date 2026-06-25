// =============================================================================
// SikaChain — Native action wrappers
// =============================================================================
// These are the privileged intrinsics that nodeos forwards to the system
// contract: newaccount, updateauth, setcode, etc. We bind them so the
// contract can validate or react. Each is a stub that mirrors Vaulta.
// =============================================================================

#pragma once

#include <eosio/action.hpp>
#include <eosio/crypto.hpp>
#include <eosio/eosio.hpp>
#include <eosio/ignore.hpp>
#include <eosio/print.hpp>
#include <eosio/privileged.hpp>
#include <eosio/producer_schedule.hpp>

#include <optional>

namespace sikasystem {

   struct permission_level_weight {
      eosio::permission_level permission;
      uint16_t                weight;
      EOSLIB_SERIALIZE( permission_level_weight, (permission)(weight) )
   };

   struct key_weight {
      eosio::public_key key;
      uint16_t          weight;
      EOSLIB_SERIALIZE( key_weight, (key)(weight) )
   };

   struct wait_weight {
      uint32_t wait_sec;
      uint16_t weight;
      EOSLIB_SERIALIZE( wait_weight, (wait_sec)(weight) )
   };

   struct authority {
      uint32_t                              threshold = 0;
      std::vector<key_weight>               keys;
      std::vector<permission_level_weight>  accounts;
      std::vector<wait_weight>              waits;
      EOSLIB_SERIALIZE( authority, (threshold)(keys)(accounts)(waits) )
   };

   struct block_header {
      uint32_t                                timestamp;
      eosio::name                             producer;
      uint16_t                                confirmed = 0;
      eosio::checksum256                      previous;
      eosio::checksum256                      transaction_mroot;
      eosio::checksum256                      action_mroot;
      uint32_t                                schedule_version = 0;
      std::optional<eosio::producer_schedule> new_producers;

      EOSLIB_SERIALIZE( block_header, (timestamp)(producer)(confirmed)(previous)(transaction_mroot)(action_mroot)
            (schedule_version)(new_producers) )
   };

   // Native action signatures — match nodeos intrinsics exactly.
   class [[eosio::contract("sika.system")]] native : public eosio::contract {
   public:
      using contract::contract;

      [[eosio::action]]
      void newaccount( const eosio::name&        creator,
                       const eosio::name&        name,
                       eosio::ignore<authority>  owner,
                       eosio::ignore<authority>  active );

      [[eosio::action]]
      void updateauth( eosio::ignore<eosio::name>      account,
                       eosio::ignore<eosio::name>      permission,
                       eosio::ignore<eosio::name>      parent,
                       eosio::ignore<authority>        auth ) {}

      [[eosio::action]]
      void deleteauth( eosio::ignore<eosio::name>      account,
                       eosio::ignore<eosio::name>      permission ) {}

      [[eosio::action]]
      void linkauth( eosio::ignore<eosio::name>        account,
                     eosio::ignore<eosio::name>        code,
                     eosio::ignore<eosio::name>        type,
                     eosio::ignore<eosio::name>        requirement ) {}

      [[eosio::action]]
      void unlinkauth( eosio::ignore<eosio::name>      account,
                       eosio::ignore<eosio::name>      code,
                       eosio::ignore<eosio::name>      type ) {}

      [[eosio::action]]
      void canceldelay( eosio::ignore<eosio::permission_level>  canceling_auth,
                        eosio::ignore<eosio::checksum256>       trx_id ) {}

      [[eosio::action]]
      void onerror( eosio::ignore<uint128_t>          sender_id,
                    eosio::ignore<std::vector<char>>  sent_trx ) {}

      [[eosio::action]]
      void setabi( const eosio::name& account, const std::vector<char>& abi );

      [[eosio::action]]
      void setcode( const eosio::name& account, uint8_t vmtype,
                    uint8_t vmversion, const std::vector<char>& code ) {}
   };

} // namespace sikasystem
