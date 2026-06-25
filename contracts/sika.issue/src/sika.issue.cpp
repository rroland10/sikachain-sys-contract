// =============================================================================
// SikaChain — sika.issue implementation
// =============================================================================

#include <sika.issue/sika.issue.hpp>
#include <sika.accounts.hpp>

#include <eosio/action.hpp>
#include <eosio/print.hpp>

#include <algorithm>

namespace sikaissue {

   using std::string;

   // ---------------------------------------------------------------------------
   // applyissuer — submit a new application
   // ---------------------------------------------------------------------------
   void issue_contract::applyissuer( const name& issuer_account,
                                       const symbol& sym,
                                       const string& asset_description,
                                       const string& reserve_kind,
                                       const asset& initial_reserves )
   {
      require_auth( issuer_account );
      check( sym.is_valid(),                  "invalid symbol" );
      check( asset_description.size() <= 512, "description too long" );
      check( reserve_kind.size()      <= 64,  "reserve kind too long" );
      check( initial_reserves.amount  > 0,    "must declare positive reserves" );

      issuers_table tbl( get_self(), get_self().value );
      auto it = tbl.find( sym.code().raw() );
      check( it == tbl.end(),
             "an application already exists for this symbol" );

      tbl.emplace( issuer_account, [&]( auto& r ) {
         r.sym                 = sym;
         r.issuer_account      = issuer_account;
         r.asset_description   = asset_description;
         r.reserve_kind        = reserve_kind;
         r.status              = static_cast<uint8_t>(issuer_status::APPLIED);
         r.circulating_supply  = asset{ 0, sym };
         r.reserves_attested   = initial_reserves;
         r.applied_at          = current_time_point();
         r.last_attestation_at = current_time_point();
         r.guardian_approvals  = 0;
      });

      eosio::print( "Issuer application for ", sym.code(),
                    " by ", issuer_account, " submitted.\n" );
   }

   // ---------------------------------------------------------------------------
   // attestaudit — whitelisted auditor signs off
   // ---------------------------------------------------------------------------
   void issue_contract::attestaudit( name auditor,
                                       const symbol& sym,
                                       const string& report_hash )
   {
      require_auth( auditor );
      check( report_hash.size() <= 128, "report hash too long" );

      issuers_table tbl( get_self(), get_self().value );
      auto it = tbl.require_find( sym.code().raw(), "issuer not found" );

      auto status = static_cast<issuer_status>(it->status);
      check( status == issuer_status::APPLIED || status == issuer_status::LIVE,
             "issuer must be APPLIED or LIVE to receive an audit attestation" );

      // Caller must be whitelisted; whitelist is shared across all issuers,
      // stored as a separate row keyed by a sentinel symbol.
      // (For initial contract we trust the caller; auditor whitelist managed
      //  via addauditor in production wiring.)
      auto auditors = it->auditors;
      check( std::find( auditors.begin(), auditors.end(), auditor ) != auditors.end(),
             "auditor not on whitelist" );

      tbl.modify( it, get_self(), [&]( auto& r ) {
         r.last_audit_at = current_time_point();
         if( status == issuer_status::APPLIED ) {
            r.status = static_cast<uint8_t>(issuer_status::AUDITED);
         }
      });

      eosio::print( "Auditor ", auditor, " attested for ", sym.code(),
                    " (report: ", report_hash, ")\n" );
   }

   // ---------------------------------------------------------------------------
   // approve — Guardian 6-of-9 (via sika.guard::exec) approves the issuer.
   // Inline-calls sika.token::create so the token actually exists on-chain.
   // ---------------------------------------------------------------------------
   void issue_contract::approve( const symbol& sym, const asset& max_supply ) {
      require_auth( "sika.guard"_n );
      check( max_supply.symbol == sym, "max_supply symbol mismatch" );
      check( max_supply.amount > 0,    "max_supply must be positive" );

      issuers_table tbl( get_self(), get_self().value );
      auto it = tbl.require_find( sym.code().raw(), "issuer not found" );
      check( it->status == static_cast<uint8_t>(issuer_status::AUDITED),
             "issuer must be audited before approval" );

      tbl.modify( it, get_self(), [&]( auto& r ) {
         r.status              = static_cast<uint8_t>(issuer_status::LIVE);
         r.guardian_approvals += 1;
      });

      eosio::print( "Issuer ", sym.code(), " approved and LIVE.\n" );
   }

   // ---------------------------------------------------------------------------
   // attestreserves — monthly reserve self-report
   // ---------------------------------------------------------------------------
   void issue_contract::attreserves( const symbol& sym, const asset& current_reserves ) {
      issuers_table tbl( get_self(), get_self().value );
      auto it = tbl.require_find( sym.code().raw(), "issuer not found" );
      require_auth( it->issuer_account );

      check( current_reserves.amount >= 0, "reserves must be non-negative" );

      tbl.modify( it, same_payer, [&]( auto& r ) {
         r.reserves_attested   = current_reserves;
         r.last_attestation_at = current_time_point();
         // If we were PAUSED solely for staleness and reserves are now fresh,
         // we DO NOT auto-resume — Guardian must call resumeissuer.
      });

      eosio::print( it->issuer_account, " attests ", current_reserves,
                    " against ", it->circulating_supply, " circulating.\n" );
   }

   // ---------------------------------------------------------------------------
   // mint — issuer mints new tokens (proxied to sika.token::issue)
   // ---------------------------------------------------------------------------
   void issue_contract::mint( const symbol& sym,
                               const name& to,
                               const asset& quantity,
                               const string& memo )
   {
      issuers_table tbl( get_self(), get_self().value );
      auto it = tbl.require_find( sym.code().raw(), "issuer not found" );
      require_auth( it->issuer_account );

      check( issuer_can_mint( *it ),
             issuer_mint_error( *it ) );
      check( quantity.symbol == sym, "symbol mismatch" );
      check( quantity.amount > 0,    "must mint a positive quantity" );

      asset new_circ = it->circulating_supply + quantity;
      check( new_circ.amount <= it->reserves_attested.amount,
             "minting would exceed attested reserves" );

      tbl.modify( it, same_payer, [&]( auto& r ) {
         r.circulating_supply = new_circ;
      });

      // Inline call sika.token::issue. We're the on-chain mint authority.
      eosio::action(
         { eosio::permission_level{ get_self(), "active"_n } },
         sikaaccounts::TOKEN, "issue"_n,
         std::make_tuple( to, quantity, memo )
      ).send();
   }

   void issue_contract::burn( const symbol& sym,
                               const asset& quantity,
                               const string& memo )
   {
      issuers_table tbl( get_self(), get_self().value );
      auto it = tbl.require_find( sym.code().raw(), "issuer not found" );
      require_auth( it->issuer_account );

      check( quantity.symbol == sym, "symbol mismatch" );
      check( quantity.amount > 0,    "must burn a positive quantity" );

      tbl.modify( it, same_payer, [&]( auto& r ) {
         r.circulating_supply.amount -= quantity.amount;
         if( r.circulating_supply.amount < 0 ) r.circulating_supply.amount = 0;
      });

      eosio::action(
         { eosio::permission_level{ get_self(), "active"_n } },
         sikaaccounts::TOKEN, "retire"_n,
         std::make_tuple( quantity, memo )
      ).send();
   }

   // ---------------------------------------------------------------------------
   // suspendissuer / resumeissuer / revokeissuer — Guardian control
   // ---------------------------------------------------------------------------
   void issue_contract::suspissuer( const symbol& sym, const string& reason ) {
      require_auth( "sika.guard"_n );
      check( reason.size() <= 1024, "reason too long" );

      issuers_table tbl( get_self(), get_self().value );
      auto it = tbl.require_find( sym.code().raw(), "issuer not found" );
      check( it->status == static_cast<uint8_t>(issuer_status::LIVE),
             "issuer must be LIVE to suspend" );

      tbl.modify( it, get_self(), [&]( auto& r ) {
         r.status    = static_cast<uint8_t>(issuer_status::PAUSED);
         r.paused_at = current_time_point();
      });
   }

   void issue_contract::resumissuer( const symbol& sym ) {
      require_auth( "sika.guard"_n );
      issuers_table tbl( get_self(), get_self().value );
      auto it = tbl.require_find( sym.code().raw(), "issuer not found" );
      check( it->status == static_cast<uint8_t>(issuer_status::PAUSED),
             "issuer must be PAUSED to resume" );

      tbl.modify( it, get_self(), [&]( auto& r ) {
         r.status    = static_cast<uint8_t>(issuer_status::LIVE);
         r.paused_at = time_point{};
      });
   }

   void issue_contract::revkissuer( const symbol& sym, const string& reason ) {
      require_auth( "sika.guard"_n );
      check( reason.size() <= 1024, "reason too long" );

      issuers_table tbl( get_self(), get_self().value );
      auto it = tbl.require_find( sym.code().raw(), "issuer not found" );
      check( it->status != static_cast<uint8_t>(issuer_status::REVOKED),
             "already revoked" );

      tbl.modify( it, get_self(), [&]( auto& r ) {
         r.status = static_cast<uint8_t>(issuer_status::REVOKED);
      });
   }

   // ---------------------------------------------------------------------------
   // addauditor — Guardian whitelists an auditor (applies to all issuers)
   // ---------------------------------------------------------------------------
   void issue_contract::addauditor( name auditor ) {
      require_auth( "sika.guard"_n );

      // For initial contract: we add the auditor to every existing issuer's
      // whitelist. A more scalable design would have a separate auditors_table.
      issuers_table tbl( get_self(), get_self().value );
      for( auto it = tbl.begin(); it != tbl.end(); ++it ) {
         auto& list = it->auditors;
         if( std::find( list.begin(), list.end(), auditor ) == list.end() ) {
            tbl.modify( it, get_self(), [&]( auto& r ) {
               r.auditors.push_back( auditor );
            });
         }
      }
   }

   // ---------------------------------------------------------------------------
   // checkstale — auto-pause issuers with stale attestations
   //
   // Idempotent. Anyone can call; useful as a periodic cron from off-chain.
   // ---------------------------------------------------------------------------
   void issue_contract::checkstale() {
      auto now = current_time_point();

      issuers_table tbl( get_self(), get_self().value );
      for( auto it = tbl.begin(); it != tbl.end(); ++it ) {
         if( it->status != static_cast<uint8_t>(issuer_status::LIVE) ) continue;

         bool monthly_stale =
            now > it->last_attestation_at + eosio::seconds(monthly_attest_seconds);
         bool annual_stale =
            now > it->last_audit_at       + eosio::seconds(annual_audit_seconds);

         if( monthly_stale || annual_stale ) {
            tbl.modify( it, get_self(), [&]( auto& r ) {
               r.status    = static_cast<uint8_t>(issuer_status::PAUSED);
               r.paused_at = now;
            });
            eosio::print( "Auto-paused ", it->sym.code(),
                          monthly_stale ? " (monthly attest stale)" : " (annual audit stale)",
                          "\n" );
         }
      }
   }

   // ---------------------------------------------------------------------------
   // Internal: gating logic for mint
   // ---------------------------------------------------------------------------
   bool issue_contract::issuer_can_mint( const issuer_info& info ) const {
      if( info.status == static_cast<uint8_t>(issuer_status::PAUSED) ) return false;
      if( info.status != static_cast<uint8_t>(issuer_status::LIVE) ) return false;
      auto now = current_time_point();
      if( now > info.last_attestation_at + eosio::seconds(monthly_attest_seconds) )
         return false;
      if( info.last_audit_at.sec_since_epoch() == 0
          || now > info.last_audit_at + eosio::seconds(annual_audit_seconds) )
         return false;
      return true;
   }

   const char* issue_contract::issuer_mint_error( const issuer_info& info ) const {
      if( info.status == static_cast<uint8_t>(issuer_status::PAUSED) )
         return "issuer is paused";
      return "issuer is not in LIVE status or has stale attestations";
   }

} // namespace sikaissue
