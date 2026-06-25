// =============================================================================
// SikaChain — Native intrinsic handlers (newaccount, setabi, …)
// =============================================================================

#include <sika.system/native.hpp>

#include <eosio/privileged.hpp>

namespace sikasystem {

   void native::newaccount( const eosio::name&        creator,
                            const eosio::name&        newact,
                            eosio::ignore<authority>  owner,
                            eosio::ignore<authority>  active )
   {
      (void)creator;
      (void)owner;
      (void)active;
      set_resource_limits( newact, 0, 0, 0 );
   }

   void native::setabi( const eosio::name& account, const std::vector<char>& abi ) {
      (void)account;
      (void)abi;
   }

} // namespace sikasystem
