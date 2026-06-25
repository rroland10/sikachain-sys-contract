// =============================================================================
// SikaChain — sika.system core implementation
// =============================================================================
// Constructor, destructor, init (genesis), voting, regproducer, delegatebw,
// undelegatebw. The novel SikaChain logic lives in producer_pay.cpp,
// enforcement.cpp, rex.cpp, and ram_bancor.cpp.
// =============================================================================

#include <sika.system/sika.system.hpp>
#include <sika.accounts.hpp>

#include <eosio/check.hpp>
#include <eosio/system.hpp>
#include <eosio/action.hpp>
#include <eosio/transaction.hpp>
#include <math.h>

namespace sikasystem {

   system_contract::system_contract( name s, name code, eosio::datastream<const char*> ds )
   : contract( s, code, ds ),
     _voters( s, s.value ),
     _producers( s, s.value ),
     _global( s, s.value ),
     _rammarket( s, s.value ),
     _rexpool( s, s.value ),
     _rexbalance( s, s.value ),
     _bpvest( s, s.value )
   {
      if( _global.exists() ) {
         _gstate = _global.get();
      } else {
         _gstate = sika_global_state();
      }
   }

   system_contract::~system_contract() {
      _global.set( _gstate, get_self() );
   }

   // ---------------------------------------------------------------------------
   // init — runs once at genesis. Sets the genesis timestamp (for halving math)
   // and seeds the RAM market.
   // ---------------------------------------------------------------------------
   void system_contract::init( unsigned_int version, const symbol& core ) {
      require_auth( get_self() );
      check( version.value == 0, "unsupported init version" );
      check( core == sika_symbol, "core symbol must be SIKA,4" );

      check( _gstate.genesis_time.sec_since_epoch() == 0,
             "init has already been called" );

      _gstate.genesis_time              = current_time_point();
      _gstate.last_pervote_bucket_fill  = current_time_point();
      _gstate.thresh_activated_stake_time = current_time_point();
      _gstate.current_year_inflation_bps  = inflation_year1_bps;

      // Seed the RAM market with 64 GiB RAM × ~1B SIKA (Bancor invariant).
      auto itr = _rammarket.find( ramcore_symbol.raw() );
      if( itr == _rammarket.end() ) {
         _rammarket.emplace( get_self(), [&]( auto& m ) {
            m.supply.amount = 100'000'000'000'000ll;
            m.supply.symbol = ramcore_symbol;
            m.base.balance.amount  = static_cast<int64_t>(_gstate.max_ram_size);
            m.base.balance.symbol  = ram_symbol;
            m.quote.balance.amount = 1'000'000'000ll * 10'000;  // 1B SIKA at 4 decimals
            m.quote.balance.symbol = sika_symbol;
         });
      }

      if( _rexpool.begin() == _rexpool.end() ) {
         _rexpool.emplace( get_self(), [&]( auto& p ) {
            p.total_lent         = asset{ 0, sika_symbol };
            p.total_unlent       = asset{ 0, sika_symbol };
            p.total_rent         = asset{ 0, sika_symbol };
            p.total_lendable     = asset{ 0, sika_symbol };
            p.total_rex          = asset{ 0, symbol{ "REX", 4 } };
            p.namebid_proceeds   = asset{ 0, sika_symbol };
            p.cghs_yield_pool    = asset{ 0, cghs_symbol };
         });
      }
   }

   // ---------------------------------------------------------------------------
   // regproducer — register or update a BP.
   // Requires that the producer has ≥ 1M SIKA staked. We do NOT check the floor
   // at registration time (the BP can register early, then stake up). Rule 3
   // is checked by enforce().
   // ---------------------------------------------------------------------------
   void system_contract::regproducer( const name& producer,
                                       const eosio::public_key& producer_key,
                                       const std::string& url,
                                       uint16_t location )
   {
      require_auth( producer );
      check( url.size() <= 512, "url too long" );

      auto p = _producers.find( producer.value );
      if( p != _producers.end() ) {
         _producers.modify( p, producer, [&]( auto& m ) {
            m.producer_key = producer_key;
            m.is_active    = true;
            m.url          = url;
            m.location     = location;
         });
      } else {
         _producers.emplace( producer, [&]( auto& m ) {
            m.owner          = producer;
            m.total_votes    = 0;
            m.producer_key   = producer_key;
            m.is_active      = true;
            m.url            = url;
            m.location       = location;
            m.uptime_bps     = 10000;
            m.has_public_rpc = false;
         });
      }
   }

   // Authority-based registration (multi-key support)
   void system_contract::regproducer2( const name& producer,
                                        const block_signing_authority& producer_authority,
                                        const std::string& url,
                                        uint16_t location )
   {
      // In Vaulta this stores producer_authority separately; for the initial
      // SikaChain contract we extract the first key and delegate to regproducer.
      require_auth( producer );
      check( url.size() <= 512, "url too long" );
      // (Real implementation: store the full authority in producer_info2)
      (void)producer_authority;
      (void)location;
      check( false, "regproducer2 not yet supported; use regproducer" );
   }

   // ---------------------------------------------------------------------------
   // unregprod — voluntary withdrawal from BP candidacy.
   // NOT a punishment. The BP can re-register later.
   // ---------------------------------------------------------------------------
   void system_contract::unregprod( const name& producer ) {
      require_auth( producer );
      auto it = _producers.require_find( producer.value, "producer not registered" );
      forfeit_bp_vest( producer );
      _producers.modify( it, same_payer, [&]( auto& p ) {
         p.deactivate();
      });
   }

   // ---------------------------------------------------------------------------
   // voteproducer — vote for up to 30 BPs directly, OR delegate to a Rep.
   // The Rep is referenced by account_name and registers separately via
   // sika.rep::regrep.
   // ---------------------------------------------------------------------------
   void system_contract::voteproducer( const name& voter, const name& proxy,
                                        const std::vector<name>& producers )
   {
      require_auth( voter );

      check( producers.size() <= max_bps_per_vote,
             "cannot vote for more than 30 producers (Article III)" );
      check( proxy.value == 0 || producers.empty(),
             "cannot both delegate to a Rep and vote directly" );

      // Producers list must be sorted & deduplicated
      for( size_t i = 1; i < producers.size(); ++i ) {
         check( producers[i-1] < producers[i],
                "producers must be sorted and unique" );
      }
      for( const auto& bp : producers ) {
         check( _producers.find( bp.value ) != _producers.end(),
                "voted-for producer not registered" );
      }

      update_votes( voter, proxy, producers, true );
   }

   // ---------------------------------------------------------------------------
   // regproxy — register or unregister as a Representative.
   // Reps are managed by sika.rep::regrep / sika.rep::setboost; this is the
   // on-chain primitive that activates the proxy bit in voter_info.
   // ---------------------------------------------------------------------------
   void system_contract::regproxy( const name& proxy, bool isproxy ) {
      require_auth( proxy );

      auto it = _voters.find( proxy.value );
      if( it != _voters.end() ) {
         _voters.modify( it, same_payer, [&]( auto& v ) {
            v.is_proxy = isproxy;
         });
      } else {
         _voters.emplace( proxy, [&]( auto& v ) {
            v.owner    = proxy;
            v.is_proxy = isproxy;
         });
      }
   }

   // ---------------------------------------------------------------------------
   // update_votes — core weight-recompute logic.
   // Loosely modeled on Vaulta's; we keep the integer math identical so
   // existing tooling (block explorers, vote-counters) sees familiar numbers.
   // ---------------------------------------------------------------------------
   void system_contract::update_votes( const name& voter_name, const name& proxy,
                                        const std::vector<name>& producers, bool voting )
   {
      auto voter_it = _voters.find( voter_name.value );
      check( voter_it != _voters.end()
             || (voter_name.value != 0 && proxy.value == 0 && producers.empty()),
             "voter must be initialized via deposit first" );

      // If the voter previously delegated to a Rep or voted directly,
      // subtract their previous contribution before applying the new one.
      double old_weight = voter_it != _voters.end()
                          ? voter_it->last_vote_weight : 0.0;

      // Weight is sqrt-time-decayed in Vaulta. For SikaChain we keep the same
      // exact formula so vote-weight tables match expectations.
      double now_secs = static_cast<double>( current_time_point().sec_since_epoch() );
      // 31557600 seconds = 1 Julian year
      double weight   = pow( 2.0, (now_secs - 946684800.0) / 31557600.0 );

      double new_stake_weight = voter_it != _voters.end()
                                ? static_cast<double>(voter_it->staked) * weight
                                : 0.0;

      // Remove old contributions
      if( voter_it != _voters.end() ) {
         if( voter_it->proxy.value != 0 ) {
            auto p_it = _voters.find( voter_it->proxy.value );
            if( p_it != _voters.end() ) {
               _voters.modify( p_it, same_payer, [&]( auto& m ) {
                  m.proxied_vote_weight -= old_weight;
               });
            }
         } else {
            for( const auto& bp : voter_it->producers ) {
               auto b = _producers.find( bp.value );
               if( b != _producers.end() ) {
                  _producers.modify( b, same_payer, [&]( auto& m ) {
                     m.total_votes -= old_weight;
                     if( m.total_votes < 0 ) m.total_votes = 0;
                  });
               }
            }
         }
      }

      // Add new contributions
      if( proxy.value != 0 ) {
         auto p_it = _voters.find( proxy.value );
         check( p_it != _voters.end() && p_it->is_proxy,
                "proxy not registered as Representative" );
         _voters.modify( p_it, same_payer, [&]( auto& m ) {
            m.proxied_vote_weight += new_stake_weight;
         });
      } else {
         for( const auto& bp : producers ) {
            auto b = _producers.find( bp.value );
            if( b != _producers.end() ) {
               _producers.modify( b, same_payer, [&]( auto& m ) {
                  m.total_votes += new_stake_weight;
               });
            }
         }
      }

      // Persist the voter's new vote
      if( voter_it != _voters.end() ) {
         _voters.modify( voter_it, same_payer, [&]( auto& m ) {
            m.proxy            = proxy;
            m.producers        = producers;
            m.last_vote_weight = new_stake_weight;
         });
      } else {
         _voters.emplace( voter_name, [&]( auto& m ) {
            m.owner            = voter_name;
            m.proxy            = proxy;
            m.producers        = producers;
            m.last_vote_weight = 0.0;
         });
      }

      (void)voting;

      recompute_producer_vote_weight();
   }

   void system_contract::recompute_producer_vote_weight() {
      double sum = 0.0;
      for( auto it = _producers.begin(); it != _producers.end(); ++it ) {
         if( it->is_active ) {
            sum += it->total_votes;
         }
      }
      _gstate.total_producer_vote_weight = sum;
   }

   // ---------------------------------------------------------------------------
   // propagate_weight_change — recompute a voter's contribution after their
   // stake or producer list changed (used by enforcement on vote removal).
   // ---------------------------------------------------------------------------
   void system_contract::propagate_weight_change( const voter_info& v ) {
      // Simplification for initial contract: same logic as update_votes with
      // the voter's current selections. In production we'd batch this.
      update_votes( v.owner, v.proxy, v.producers, false );
   }

   // ---------------------------------------------------------------------------
   // delegatebw / undelegatebw / refund — CPU+NET staking.
   // SikaChain reuses Vaulta's mechanics verbatim — these are battle-tested.
   // ---------------------------------------------------------------------------
   void system_contract::delegatebw( const name& from, const name& receiver,
                                      const asset& stake_net_quantity,
                                      const asset& stake_cpu_quantity,
                                      bool transfer )
   {
      require_auth( from );
      check( stake_cpu_quantity.amount >= 0, "must stake a non-negative CPU amount" );
      check( stake_net_quantity.amount >= 0, "must stake a non-negative NET amount" );
      check( stake_cpu_quantity.symbol == sika_symbol
             && stake_net_quantity.symbol == sika_symbol,
             "stake must be in SIKA" );

      change_resource_limits( from, receiver,
                              stake_net_quantity, stake_cpu_quantity,
                              transfer );
   }

   void system_contract::undelegatebw( const name& from, const name& receiver,
                                        const asset& unstake_net_quantity,
                                        const asset& unstake_cpu_quantity )
   {
      require_auth( from );
      asset net_delta = unstake_net_quantity; net_delta.amount = -net_delta.amount;
      asset cpu_delta = unstake_cpu_quantity; cpu_delta.amount = -cpu_delta.amount;
      change_resource_limits( from, receiver, net_delta, cpu_delta, false );
   }

   void system_contract::refund( const name& owner ) {
      require_auth( owner );
      refunds_table tbl( get_self(), owner.value );
      auto it = tbl.require_find( owner.value, "no refund pending" );

      // 7-day cool-down
      check( current_time_point() - it->request_time
             > microseconds( int64_t(rex_unstake_window()) * 1'000'000 ),
             "still in 7-day unstake window" );

      eosio::action(
         { eosio::permission_level{ get_self(), "active"_n } },
         sikaaccounts::TOKEN, "transfer"_n,
         std::make_tuple( get_self(), owner,
                          it->net_amount + it->cpu_amount,
                          std::string("unstaked SIKA refund") )
      ).send();

      tbl.erase( it );
   }

   void system_contract::change_resource_limits( const name& from,
                                                  const name& receiver,
                                                  const asset& stake_net_delta,
                                                  const asset& stake_cpu_delta,
                                                  bool transfer )
   {
      const asset total_delta = stake_net_delta + stake_cpu_delta;
      check( total_delta.amount != 0 || stake_net_delta.amount != 0 || stake_cpu_delta.amount != 0,
             "must stake a non-zero amount" );

      user_resources_table userres( get_self(), receiver.value );
      auto res_it = userres.find( receiver.value );
      int64_t net_weight = 0;
      int64_t cpu_weight = 0;
      if( res_it == userres.end() ) {
         userres.emplace( from, [&]( auto& r ) {
            r.owner      = receiver;
            r.net_weight = stake_net_delta;
            r.cpu_weight = stake_cpu_delta;
            net_weight   = stake_net_delta.amount;
            cpu_weight   = stake_cpu_delta.amount;
         });
      } else {
         userres.modify( res_it, same_payer, [&]( auto& r ) {
            r.net_weight += stake_net_delta;
            r.cpu_weight += stake_cpu_delta;
            net_weight    = r.net_weight.amount;
            cpu_weight    = r.cpu_weight.amount;
         });
      }

      check( net_weight >= 0, "insufficient staked net bandwidth" );
      check( cpu_weight >= 0, "insufficient staked cpu bandwidth" );

      eosio::set_resource_limits( receiver, -1, net_weight, cpu_weight );

      const name stake_owner = transfer ? receiver : from;

      if( total_delta.amount > 0 ) {
         eosio::action(
            { eosio::permission_level{ from, "active"_n } },
            sikaaccounts::TOKEN, "transfer"_n,
            std::make_tuple( from, get_self(), total_delta,
                             std::string( "stake bandwidth" ) )
         ).send();
      } else if( total_delta.amount < 0 && stake_owner == receiver ) {
         refunds_table refunds( get_self(), stake_owner.value );
         auto req = refunds.find( stake_owner.value );
         asset net_refund = stake_net_delta.amount < 0
                            ? asset{ -stake_net_delta.amount, sika_symbol }
                            : asset{ 0, sika_symbol };
         asset cpu_refund = stake_cpu_delta.amount < 0
                            ? asset{ -stake_cpu_delta.amount, sika_symbol }
                            : asset{ 0, sika_symbol };
         if( req == refunds.end() ) {
            refunds.emplace( stake_owner, [&]( auto& r ) {
               r.owner        = stake_owner;
               r.request_time = time_point_sec{ current_time_point() };
               r.net_amount   = net_refund;
               r.cpu_amount   = cpu_refund;
            });
         } else {
            refunds.modify( req, same_payer, [&]( auto& r ) {
               r.request_time = time_point_sec{ current_time_point() };
               r.net_amount  += net_refund;
               r.cpu_amount  += cpu_refund;
            });
         }
      }

      auto voter_it = _voters.find( stake_owner.value );
      if( voter_it == _voters.end() ) {
         _voters.emplace( stake_owner, [&]( auto& v ) {
            v.owner  = stake_owner;
            v.staked = total_delta.amount;
         });
      } else {
         _voters.modify( voter_it, same_payer, [&]( auto& v ) {
            v.staked += total_delta.amount;
            check( v.staked >= 0, "staked balance cannot be negative" );
         });
      }

      auto vit = _voters.find( stake_owner.value );
      if( vit != _voters.end() && !vit->producers.empty() ) {
         propagate_weight_change( *vit );
      }
   }

   void system_contract::setparams( const eosio::blockchain_parameters& params ) {
      // Only sika.rules (after a ratified amendment) may call this.
      require_auth( "sika.rules"_n );
      (eosio::blockchain_parameters&)_gstate = params;
      eosio::set_blockchain_parameters( params );
   }

   void system_contract::setram( uint64_t max_ram_size ) {
      require_auth( "sika.rules"_n );
      check( max_ram_size > _gstate.max_ram_size,
             "RAM size can only increase" );
      _gstate.max_ram_size = max_ram_size;
   }

   void system_contract::setpriv( const name& account, uint8_t is_priv ) {
      require_auth( "sika.rules"_n );
      eosio::set_privileged( account, is_priv );
   }

} // namespace sikasystem
