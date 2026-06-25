// =============================================================================
// SikaChain — Enforcement (Article IV + Article X)
// =============================================================================
// Implements the 5-rule BP compliance check with VOTE REMOVAL ONLY.
// No SIKA is ever destroyed by enforcement. Mirrors domain.ts::checkBpCompliance.
//
// The 5 rules from Article IV:
//   1. Uptime > 95.00%
//   2. Public RPC endpoint exposed
//   3. Self-staked SIKA ≥ 1,000,000
//   4. Network upgrades applied on time
//   5. Open issues resolved within 7 days
//
// When a rule is violated, the BP enters PROBATION:
//   - probation_flags bitmask records which rules are failing
//   - probation_started_at marks when the 7-day clock began
//   - The BP is dropped from the active schedule
//   - All voter weight pointing at the BP is REMOVED (not slashed — voters keep
//     their SIKA, but their direct vote on that BP is cleared)
//
// If the BP fixes the issue within 7 days, probation clears.
// If not, the BP is permanently withdrawn from candidacy (status only — no
// SIKA destruction, ever).
// =============================================================================

#include <sika.system/sika.system.hpp>

#include <eosio/system.hpp>
#include <eosio/print.hpp>
#include <eosio/action.hpp>

namespace sikasystem {

   // ---------------------------------------------------------------------------
   // Set or clear a single rule violation flag on a producer.
   // Returns true if state changed.
   // ---------------------------------------------------------------------------
   bool system_contract::flag_violation( producer_info& p, uint8_t rule_bit ) {
      if( (p.probation_flags & rule_bit) != 0 ) return false;
      p.probation_flags |= rule_bit;
      if( p.probation_started_at.sec_since_epoch() == 0 ) {
         p.probation_started_at = current_time_point();
      }
      return true;
   }

   bool system_contract::clear_violation( producer_info& p, uint8_t rule_bit ) {
      if( (p.probation_flags & rule_bit) == 0 ) return false;
      p.probation_flags &= ~rule_bit;
      if( p.probation_flags == 0 ) {
         p.probation_started_at = time_point{};
      }
      return true;
   }

   // ---------------------------------------------------------------------------
   // attestcompl
   // Submit a compliance attestation. The caller authority must be:
   //   - the producer themselves (for self-attestation), OR
   //   - sika.guard (when a Guardian-witnessed check is required)
   //
   // We re-evaluate rules 1-2-4 from the attested numbers. Rules 3 (stake floor)
   // and 5 (issue SLA) are re-checked separately because they have on-chain
   // truth sources.
   // ---------------------------------------------------------------------------
   void system_contract::attestcompl( const name& producer,
                                       uint16_t uptime_bps,
                                       bool has_public_rpc,
                                       bool upgrade_on_time )
   {
      // Allow either the BP or a Guardian to submit.
      check( has_auth(producer) || has_auth("sika.guard"_n),
             "attestation requires producer or sika.guard auth" );

      auto it = _producers.require_find( producer.value, "producer not registered" );

      _producers.modify( it, same_payer, [&]( auto& p ) {
         p.uptime_bps        = uptime_bps;
         p.has_public_rpc    = has_public_rpc;
         p.upgrade_on_time   = upgrade_on_time;

         // Rule 1: uptime > 95.00%
         if( uptime_bps <= uptime_floor_bps ) {
            flag_violation( p, probation_bits::UPTIME_BELOW_FLOOR );
         } else {
            clear_violation( p, probation_bits::UPTIME_BELOW_FLOOR );
         }

         // Rule 2: public RPC
         if( !has_public_rpc ) {
            flag_violation( p, probation_bits::NO_PUBLIC_RPC );
         } else {
            clear_violation( p, probation_bits::NO_PUBLIC_RPC );
         }

         // Rule 4: upgrades on time
         if( !upgrade_on_time ) {
            flag_violation( p, probation_bits::MISSED_UPGRADE );
         } else {
            clear_violation( p, probation_bits::MISSED_UPGRADE );
         }
      });
   }

   // ---------------------------------------------------------------------------
   // openissue
   // Starts the 7-day SLA clock against a BP. Only sika.guard may open.
   // ---------------------------------------------------------------------------
   void system_contract::openissue( const name& producer, const std::string& reason ) {
      require_auth( "sika.guard"_n );
      check( reason.size() <= 256, "reason too long" );

      auto it = _producers.require_find( producer.value, "producer not registered" );
      check( it->open_issue_at.sec_since_epoch() == 0,
             "an issue is already open against this producer" );

      _producers.modify( it, same_payer, [&]( auto& p ) {
         p.open_issue_at     = current_time_point();
         p.issues_within_sla = true;  // still within SLA at the moment of opening
      });
      eosio::print( "Issue opened against ", producer, ": ", reason, "\n" );
   }

   // ---------------------------------------------------------------------------
   // closeissue
   // Resolved an issue — clears the SLA clock and the SLA violation flag.
   // ---------------------------------------------------------------------------
   void system_contract::closeissue( const name& producer ) {
      require_auth( "sika.guard"_n );

      auto it = _producers.require_find( producer.value, "producer not registered" );
      check( it->open_issue_at.sec_since_epoch() > 0, "no open issue to close" );

      _producers.modify( it, same_payer, [&]( auto& p ) {
         p.open_issue_at     = time_point{};
         p.issues_within_sla = true;
         clear_violation( p, probation_bits::ISSUE_SLA_BREACHED );
      });
   }

   // ---------------------------------------------------------------------------
   // enforce
   // Idempotent enforcement sweep — recomputes rule 3 (stake floor) and rule 5
   // (SLA breach), updates probation flags, applies vote removal for BPs in
   // probation > 7 days.
   //
   // Should be called regularly (hourly) by a cron account. Any caller is
   // permitted because the action is idempotent and read-only-equivalent to
   // anyone who isn't a BP.
   // ---------------------------------------------------------------------------
   void system_contract::enforce() {
      const auto now = current_time_point();

      for( auto itr = _producers.begin(); itr != _producers.end(); /**/ ) {
         const auto& p = *itr;

         _producers.modify( itr, same_payer, [&]( auto& m ) {
            // Rule 3 — self-stake floor. Read from the BP's own voter row.
            auto vit = _voters.find( p.owner.value );
            int64_t self_stake = (vit != _voters.end()) ? vit->staked : 0;
            if( self_stake < bp_stake_floor ) {
               flag_violation( m, probation_bits::BELOW_STAKE_FLOOR );
            } else {
               clear_violation( m, probation_bits::BELOW_STAKE_FLOOR );
            }

            // Rule 5 — open issue older than 7 days = SLA breach
            if( m.open_issue_at.sec_since_epoch() > 0 ) {
               int64_t age = (now - m.open_issue_at).count() / 1'000'000;
               if( age >= sla_seconds ) {
                  // Probation clock for rule 5 starts when the issue was opened,
                  // not when enforce() first notices the breach.
                  if( (m.probation_flags & probation_bits::ISSUE_SLA_BREACHED) == 0 ) {
                     m.probation_started_at = m.open_issue_at;
                  }
                  flag_violation( m, probation_bits::ISSUE_SLA_BREACHED );
                  m.issues_within_sla = false;
               }
            }

            // Apply vote removal if BP has been in probation > 7 days
            if( m.probation_flags != 0
                && m.probation_started_at.sec_since_epoch() > 0 )
            {
               int64_t prob_age = (now - m.probation_started_at).count() / 1'000'000;
               if( prob_age >= bp_probation_seconds && m.is_active ) {
                  m.is_active = false;
                  // Vote weight is removed in apply_vote_removal below.
               }
            }
         });

         // Vote removal happens OUTSIDE the modify to avoid iterator invalidation
         if( !itr->is_active && itr->probation_flags != 0 ) {
            forfeit_bp_vest( itr->owner );
            apply_vote_removal( itr->owner );
         }
         ++itr;
      }
   }

   // ---------------------------------------------------------------------------
   // apply_vote_removal
   // Scrub the BP's account_name from every voter's `producers` list.
   // NO SIKA IS DESTROYED. Voters keep their stake and their voting weight;
   // they simply no longer vote for this BP.
   //
   // This is O(N) over the voters table. In production we'd index voters by
   // selected_bp and iterate the secondary index instead. For the initial
   // contract we keep it simple and rely on the fact that enforce() runs
   // hourly with bounded work per call.
   // ---------------------------------------------------------------------------
   void system_contract::apply_vote_removal( const name& bp ) {
      for( auto it = _voters.begin(); it != _voters.end(); ++it ) {
         const auto& v = *it;
         auto pos = std::find( v.producers.begin(), v.producers.end(), bp );
         if( pos == v.producers.end() ) continue;

         _voters.modify( it, same_payer, [&]( auto& m ) {
            m.producers.erase( std::remove( m.producers.begin(),
                                            m.producers.end(),
                                            bp ),
                              m.producers.end() );
         });

         // Recompute vote weight for this voter
         propagate_weight_change( v );
      }

      // Reduce the BP's total_votes to zero
      auto bp_it = _producers.find( bp.value );
      if( bp_it != _producers.end() ) {
         _producers.modify( bp_it, same_payer, [&]( auto& p ) {
            p.total_votes = 0;
         });
      }
   }

} // namespace sikasystem
