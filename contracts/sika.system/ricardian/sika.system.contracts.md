<h1 class="contract">accruepoch</h1>

---
spec_version: "0.2.0"
title: Accrue epoch fee revenue
summary: 'Records {{quantity}} into usage-gated Tier-2 accounting'
---

**{{quantity}}** is credited to epoch fee revenue for usage-gated Tier-2 BP payouts (v0.2 section 8). Typically invoked by **sika.treas** during fee sweep/accrual.

<h1 class="contract">buyrex</h1>

---
spec_version: "0.2.0"
title: Stake SIKA in REX
summary: '{{nowrap from}} stakes {{nowrap amount}} of SIKA into the REX pool and receives REX shares'
---

{{from}} authorizes a transfer of **{{amount}}** from their wallet to the REX custody account (`sika.rex`). The network mints REX shares proportional to the pool and adds the staked SIKA to {{from}}'s vote weight.

REX shares represent a pro-rata claim on the network yield pool (paid in local stablecoin, cUSD, or gGOLD depending on your payout preference and market compliance).

<h1 class="contract">sellrex</h1>

---
spec_version: "0.2.0"
title: Unstake REX (start cooldown)
summary: '{{nowrap from}} sells {{nowrap rex}} REX shares; SIKA unlocks after the unstake cooldown'
---

{{from}} converts **{{rex}}** REX shares back to SIKA at the current pool ratio. The corresponding SIKA is **not** returned to the liquid balance immediately — it enters the unstake cooldown queue (governed by `rexcfg.unstake_seconds`, default 7 days on mainnet).

After the cooldown elapses, {{from}} must call **refund** to receive the SIKA in their wallet. Vote weight is reduced when this action executes.

<h1 class="contract">refund</h1>

---
spec_version: "0.2.0"
title: Claim unstaked SIKA
summary: '{{nowrap owner}} claims SIKA that finished the REX unstake cooldown'
---

Return previously unstaked SIKA to **{{owner}}** after the configured unstake period has elapsed. This action only succeeds when a pending refund exists and the cooldown timer has passed.

<h1 class="contract">claimrewards</h1>

---
spec_version: "0.2.0"
title: Claim REX yield (local stable)
summary: '{{nowrap owner}} claims their share of the REX yield pool in local stablecoin'
---

{{owner}} withdraws their pro-rata share of the network REX yield pool, paid in the **local market stablecoin** (e.g. CGHS). The pool balance is reduced by the amount paid; any Rep voting boost already included in the pool is reflected in the payout.

<h1 class="contract">claimrexyld</h1>

---
spec_version: "0.2.0"
title: Claim REX yield (chosen payout currency)
summary: '{{nowrap owner}} claims REX yield paid as {{payout_currency}} for market {{market}}'
---

{{owner}} withdraws their pro-rata share of the REX yield pool. Settlement runs through `sika.treas` and pays **{{payout_currency}}** for market **{{market}}**, subject to on-chain compliance gates (§6.4 payout menu).

If {{payout_currency}} is the local stable, this action behaves like **claimrewards**. Otherwise the treasury contract converts reference units using licensed FX rates.

<h1 class="contract">voteproducer</h1>

---
spec_version: "0.2.0"
title: Vote for block producers
summary: '{{nowrap voter}} updates producer votes{{#if proxy}} via proxy {{proxy}}{{/if}}'
---

{{#if proxy}}
{{voter}} delegates voting weight to proxy **{{proxy}}**. Staked SIKA (including REX stake) counts toward the proxy's producer slate.
{{else}}
{{voter}} votes for up to 30 block producer candidates:

{{#each producers}}
- {{this}}
{{/each}}

Staked SIKA (including REX stake) is cast toward each selected producer.
{{/if}}

<h1 class="contract">deposit</h1>

---
spec_version: "0.2.0"
title: Deposit SIKA for REX
summary: '{{nowrap owner}} moves {{nowrap amount}} into REX custody before staking'
---

{{owner}} transfers **{{amount}}** to `sika.rex` custody as a prerequisite for REX staking on legacy code paths. Prefer **buyrex** on SikaChain, which combines transfer and mint in one step.

<h1 class="contract">delegatebw</h1>

---
spec_version: "0.2.0"
title: Delegate CPU and NET
summary: '{{nowrap from}} delegates {{stake_net_quantity}} NET and {{stake_cpu_quantity}} CPU to {{receiver}}'
---

{{from}} stakes **{{stake_net_quantity}}** (network bandwidth) and **{{stake_cpu_quantity}}** (CPU) to **{{receiver}}**.

{{#if transfer}}
Tokens are moved from {{from}}'s liquid balance into staked resources.
{{else}}
Existing stake is reassigned without moving liquid tokens.
{{/if}}

Unstaking delegated resources requires **undelegatebw** and a cooldown before **refund**.

<h1 class="contract">undelegatebw</h1>

---
spec_version: "0.2.0"
title: Unstake CPU and NET
summary: '{{nowrap from}} unstakes {{unstake_net_quantity}} NET and {{unstake_cpu_quantity}} CPU from {{receiver}}'
---

{{from}} reduces staked **{{unstake_net_quantity}}** (NET) and **{{unstake_cpu_quantity}}** (CPU) delegated to **{{receiver}}**.

Unstaked tokens enter a cooldown queue. Call **refund** after the waiting period to return SIKA to the liquid balance.

<h1 class="contract">buyram</h1>

---
spec_version: "0.2.0"
title: Buy RAM
summary: '{{nowrap payer}} buys {{nowrap quant}} RAM for {{receiver}}'
---

{{payer}} pays **{{quant}}** to purchase RAM bytes credited to **{{receiver}}** via the network RAM market. A protocol fee may apply; part funds the REX yield pool on SikaChain.

<h1 class="contract">buyrambytes</h1>

---
spec_version: "0.2.0"
title: Buy RAM (bytes)
summary: '{{nowrap payer}} buys {{bytes}} bytes of RAM for {{receiver}}'
---

{{payer}} purchases **{{bytes}}** bytes of RAM for account **{{receiver}}** using the on-chain Bancor market.

<h1 class="contract">sellram</h1>

---
spec_version: "0.2.0"
title: Sell RAM
summary: '{{nowrap account}} sells {{bytes}} bytes of RAM from their balance'
---

{{account}} sells **{{bytes}}** bytes of RAM back to the network market and receives SIKA in return (minus protocol fee).

<h1 class="contract">regproxy</h1>

---
spec_version: "0.2.0"
title: Register voting proxy
summary: '{{nowrap proxy}} {{#if isproxy}}registers as{{else}}unregisters from{{/if}} a vote proxy'
---

{{#if isproxy}}
**{{proxy}}** registers as a voting proxy. Other accounts may delegate their vote weight to this proxy.
{{else}}
**{{proxy}}** removes its registration as a voting proxy. Existing delegations may need to be cleared separately via **voteproducer**.
{{/if}}

<h1 class="contract">updateauth</h1>

---
spec_version: "0.2.0"
title: Update account permission
summary: '{{account}} updates permission {{permission}} (parent {{parent}})'
---

**{{account}}** replaces the authority for permission **{{permission}}** under parent **{{parent}}**. This changes who can authorize actions linked to that permission — review keys, accounts, and wait delays carefully.

<h1 class="contract">linkauth</h1>

---
spec_version: "0.2.0"
title: Link action to permission
summary: '{{account}} requires {{requirement}} to call {{code}}::{{type}}'
---

**{{account}}** requires permission **{{requirement}}** to execute **{{type}}** on contract **{{code}}**.

<h1 class="contract">unlinkauth</h1>

---
spec_version: "0.2.0"
title: Unlink action permission
summary: '{{account}} removes special permission for {{code}}::{{type}}'
---

**{{account}}** removes the requirement that **{{type}}** on **{{code}}** must use a linked permission. The action falls back to the default parent permission.

<h1 class="contract">deleteauth</h1>

---
spec_version: "0.2.0"
title: Delete account permission
summary: '{{account}} deletes permission {{permission}}'
---

**{{account}}** permanently deletes permission **{{permission}}**. Any links or sub-permissions must be cleared first.

<h1 class="contract">regproducer</h1>

---
spec_version: "0.2.0"
title: Register block producer
summary: '{{producer}} registers as a block producer candidate'
---

**{{producer}}** registers (or updates) its block producer profile with signing key **{{producer_key}}**, endpoint **{{url}}**, and location code **{{location}}**.

Registration requires meeting SikaChain BP stake and compliance rules (`sika.rules`). Producers earn inflation and fee rewards when elected and active; unstaking below the minimum may disqualify the account.

<h1 class="contract">regproducer2</h1>

---
spec_version: "0.2.0"
title: Register block producer (multi-key)
summary: '{{producer}} registers with block signing authority {{producer_authority}}'
---

**{{producer}}** registers (or updates) its producer profile using a **multi-key block signing authority** instead of a single legacy key. Endpoint **{{url}}** and location **{{location}}** are published on-chain.

Same stake and compliance requirements apply as **regproducer**.

<h1 class="contract">claimprod</h1>

---
spec_version: "0.2.0"
title: Claim producer rewards
summary: '{{producer}} claims accrued block producer inflation'
---

**{{producer}}** withdraws unpaid block producer rewards from the network inflation buckets (per-block and per-vote components) into its liquid SIKA balance.

Only active, compliant producers with accrued rewards can claim. Amounts depend on producer schedule participation and vote weight.

<h1 class="contract">claimvest</h1>

---
spec_version: "0.2.0"
title: Claim vested BP rewards
summary: '{{producer}} claims released vesting tranche'
---

**{{producer}}** withdraws SIKA that has finished vesting from the BP vesting schedule (tier-2 / bonus vesting when enabled). Forfeited or not-yet-vested amounts are not included.

<h1 class="contract">withdraw</h1>

---
spec_version: "0.2.0"
title: Withdraw REX savings
summary: '{{owner}} withdraws {{amount}} from REX savings balance'
---

**{{owner}}** moves **{{amount}}** from their REX savings balance back to liquid SIKA. This is the legacy savings withdrawal path; most users should prefer **sellrex** + **refund** on SikaChain.

<h1 class="contract">unregprod</h1>

---
spec_version: "0.2.0"
title: Unregister block producer
summary: '{{producer}} withdraws from block producer candidacy'
---

**{{producer}}** voluntarily removes itself from the active block producer candidate set. Existing stake remains delegated; compliance and minimum-stake rules still apply if the account re-registers later.

<h1 class="contract">attestcompl</h1>

---
spec_version: "0.2.0"
title: Attest BP compliance
summary: '{{producer}} attests uptime {{uptime_bps}} bps and operational compliance'
---

**{{producer}}** submits an on-chain compliance attestation: uptime **{{uptime_bps}}** basis points, public RPC **{{has_public_rpc}}**, upgrade timeliness **{{upgrade_on_time}}**.

False attestations may trigger enforcement under `sika.rules` and affect reward eligibility.

<h1 class="contract">canceldelay</h1>

---
spec_version: "0.2.0"
title: Cancel deferred transaction
summary: '{{canceling_auth.actor}} cancels deferred transaction {{trx_id}}'
---

**{{canceling_auth.actor}}** cancels a previously scheduled deferred transaction identified by **{{trx_id}}**, using permission **{{canceling_auth.permission}}**.

Only the authorized canceling account can abort the deferred action before it executes.

<h1 class="contract">newaccount</h1>

---
spec_version: "0.2.0"
title: Create account
summary: '{{creator}} creates account {{name}} on SikaChain'
---

**{{creator}}** creates a new on-chain account **{{name}}** with the specified **owner** and **active** authorities.

Account creation consumes RAM and may include bundled **buyrambytes** and **delegatebw** steps in the same transaction. The creator pays resource costs unless stake is transferred to the new account.

<h1 class="contract">setcode</h1>

---
spec_version: "0.2.0"
title: Deploy contract WASM
summary: '{{account}} deploys smart contract bytecode'
---

**{{account}}** publishes new WASM bytecode to its on-chain account. This replaces executable logic and requires **`eosio.code`** permission on the deploying authority.

Only deploy code you trust. Malicious WASM can drain assets or alter account behavior.

<h1 class="contract">setabi</h1>

---
spec_version: "0.2.0"
title: Publish contract ABI
summary: '{{account}} updates its contract ABI'
---

**{{account}}** publishes an updated Application Binary Interface (ABI) describing how to interact with its contract.

Clients and wallets rely on the ABI for action encoding and Ricardian display. Keep ABI in sync with deployed WASM.

<h1 class="contract">setrexcfg</h1>

---
spec_version: "0.2.0"
title: Configure REX unstake window
summary: 'Governance sets REX refund cooldown to {{unstake_seconds}} seconds'
---

Privileged authority **{{authority}}** updates how long SIKA remains locked after **sellrex** before **refund** can claim it (`rexcfg.unstake_seconds`). Shorter windows improve UX; longer windows protect pool stability.

<h1 class="contract">setvesting</h1>

---
spec_version: "0.2.0"
title: Configure BP vesting parameters
summary: 'Governance updates Tier-2 vesting rules (enabled={{tier2_vesting_enabled}})'
---

Privileged authority **{{authority}}** sets Tier-2 producer vesting: enabled flag **{{tier2_vesting_enabled}}**, bonus vest duration **{{bonus_vesting_seconds}}** seconds, inflation gain cap **{{inflation_gain_bps}}** basis points.

<h1 class="contract">setparams</h1>

---
spec_version: "0.2.0"
title: Update blockchain parameters
summary: 'Governance updates core chain resource and transaction limits'
---

Privileged governance updates global **blockchain_parameters** (CPU/NET limits, block sizes, transaction lifetimes). Only authorized system accounts may call this action.

<h1 class="contract">setram</h1>

---
spec_version: "0.2.0"
title: Set maximum RAM supply
summary: 'Governance sets max RAM to {{max_ram_size}} bytes'
---

Privileged governance sets the network-wide maximum RAM byte supply to **{{max_ram_size}}**. Affects RAM market pricing and account creation costs.

<h1 class="contract">setpriv</h1>

---
spec_version: "0.2.0"
title: Set privileged account flag
summary: '{{account}} privileged flag set to {{is_priv}}'
---

Privileged governance marks account **{{account}}** as privileged (**{{is_priv}}**). Privileged accounts can invoke native system intrinsics.

<h1 class="contract">refillpay</h1>

---
spec_version: "0.2.0"
title: Refill inflation pay buckets
summary: 'Governance credits daily inflation tranche (authority {{authority}})'
---

Privileged authority **{{authority}}** credits one inflation tranche into producer pay buckets (dev/ops path). Distributes scheduled SIKA inflation per the halving schedule.

<h1 class="contract">credyield</h1>

---
spec_version: "0.2.0"
title: Credit REX yield pool
summary: 'Adds {{ref_amount}} to the reference-unit yield pool'
---

Credits **{{ref_amount}}** reference units to the REX yield pool (v0.2 §6). Called by settlement/treasury flows when fees are swept for staker rewards.

<h1 class="contract">crediboost</h1>

---
spec_version: "0.2.0"
title: Credit Rep boost payout
summary: 'Credits {{local_amount}} local yield to voter {{voter}}'
---

Credits **{{local_amount}}** local stablecoin yield to voter **{{voter}}** from the Rep boost program. Internal action invoked by **sika.boost**.

<h1 class="contract">openissue</h1>

---
spec_version: "0.2.0"
title: Open producer issue ticket
summary: '{{producer}} opens an on-chain issue: {{reason}}'
---

Block producer **{{producer}}** registers an operational issue on-chain with reason: **{{reason}}**. Used for transparency and compliance tracking.

<h1 class="contract">closeissue</h1>

---
spec_version: "0.2.0"
title: Close producer issue ticket
summary: '{{producer}} closes their open issue ticket'
---

Block producer **{{producer}}** marks its open issue ticket as resolved.

<h1 class="contract">enforce</h1>

---
spec_version: "0.2.0"
title: Enforce BP compliance rules
summary: 'Applies probation and vote-removal for rule violations'
---

System maintenance action that enforces **sika.rules** compliance: applies probation timers and removes vote weight from disqualified producers. No parameters — processes eligible accounts.

<h1 class="contract">init</h1>

---
spec_version: "0.2.0"
title: Initialize system contract
summary: 'Bootstraps system state (version {{version}}, core {{core}})'
---

One-time or upgrade initialization of **sika.system** global state: protocol version **{{version}}** and core symbol **{{core}}**. Privileged bootstrap only.

<h1 class="contract">onblock</h1>

---
spec_version: "0.2.0"
title: Block callback
summary: 'Producer {{header.producer}} block hook — updates pay state'
---

Internal **on_block** callback invoked each block. Updates producer unpaid buckets, inflation accrual, and schedule metadata. Not user-initiated.

<h1 class="contract">onerror</h1>

---
spec_version: "0.2.0"
title: Failed deferred transaction callback
summary: 'System handler for a failed deferred inner transaction'
---

Native error callback when a deferred transaction fails. Used for diagnostics; not typically signed by users.

