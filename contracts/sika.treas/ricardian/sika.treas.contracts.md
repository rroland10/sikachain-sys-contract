<h1 class="contract">setpayoutpref</h1>

---
spec_version: "0.2.0"
title: Set REX yield payout preference
summary: '{{nowrap owner}} prefers REX yield paid as {{payout_currency}} in market {{market}}'
---

{{owner}} records a default payout currency (**{{payout_currency}}**) for market **{{market}}**. Future yield claims may use this preference when permitted by the market's compliance menu (`marketpref`).

This does not move funds — it only updates the on-chain preference table used by settlement and wallet UX.

<h1 class="contract">clearyield</h1>

---
spec_version: "0.2.0"
title: Settle REX yield payout
summary: 'Treasury pays {{nowrap owner}} {{cusd_amount}} reference units as {{payout_currency}} ({{market}})'
---

The treasury settles a REX yield payout for **{{owner}}**: **{{cusd_amount}}** in reference units, converted and delivered as **{{payout_currency}}** for market **{{market}}**.

Users normally invoke this indirectly via **claimrexyld** on the system contract; {{owner}} must authorize when called directly.

<h1 class="contract">sweep</h1>

---
spec_version: "0.2.0"
title: Sweep market fees to treasury
summary: 'Settlement sweeps accrued fees for market {{market}} into reserve and REX yield'
---

Governance action that allocates **{{market}}** fee ledger balances according to `setparams`: a slice to bootstrap reserve, a slice to the REX yield pool, and BP cost recovery. Typically invoked by the settlement worker after **accruefee**.

<h1 class="contract">accruefee</h1>

---
spec_version: "0.2.0"
title: Accrue network fees
summary: 'Record {{local_quantity}} fees for market {{market}}'
---

Credits **{{local_quantity}}** to the **{{market}}** fee ledger for later **sweep**. Authorized by the system account or settlement automation.

<h1 class="contract">paycost</h1>

---
spec_version: "0.2.0"
title: BP Tier-1 cost recovery
summary: 'Pay Tier-1 BP cost to producer {{producer}} from treasury reserve'
---

Transfers reference-unit cost recovery (CUSD) from the treasury reserve to block producer **{{producer}}** for Tier-1 compensation. Usually called via **claimprod** on the system contract.

<h1 class="contract">subsidize</h1>

---
spec_version: "0.2.0"
title: Cross-market subsidy
summary: 'Move {{amount}} from market {{from_market}} to {{to_market}}'
---

Governance transfer of **{{amount}}** reference value from donor market **{{from_market}}** to recipient **{{to_market}}**, capped by `max_subsidy_per_market_bps`.

<h1 class="contract">rebalance</h1>

---
spec_version: "0.2.0"
title: Rebalance reserve composition
summary: 'Rebalance cUSD / gGOLD mix toward reserve_gold_bps target'
---

Adjusts treasury **reserve** between cUSD and gGOLD holdings to match the configured gold allocation. No user wallets are debited.

<h1 class="contract">setparams</h1>

---
spec_version: "0.2.0"
title: Set settlement parameters
summary: '{{authority}} updates sweep, yield, and cost-recovery parameters'
---

**{{authority}}** sets global settlement knobs: sweep slice ({{sweep_slice_bps}} bps), cost recovery ({{cost_recovery_cusd}}), max subsidy ({{max_subsidy_per_market_bps}} bps), yield share ({{fee_to_yield_bps}} bps), reserve gold target ({{reserve_gold_bps}} bps).

<h1 class="contract">setmarketpref</h1>

---
spec_version: "0.2.0"
title: Set market payout menu
summary: '{{authority}} configures payout options for market {{market}}'
---

**{{authority}}** defines compliance-gated payout menu for **{{market}}** (local symbol {{local_symbol}}): cUSD={{allow_cusd}}, gGOLD={{allow_ggold}}, ready={{compliance_ready}}.

<h1 class="contract">setfx</h1>

---
spec_version: "0.2.0"
title: Set FX peg (governance)
summary: '{{authority}} sets {{local_symbol}} → CUSD rate to {{cusd_ppm}} ppm'
---

Governance override of the **{{local_symbol}}** FX quote at **{{cusd_ppm}}** parts-per-million (TTL {{ttl_seconds}}s). Dev fallback when oracle push is unavailable.

<h1 class="contract">pushfx</h1>

---
spec_version: "0.2.0"
title: Push FX quote (oracle)
summary: 'Oracle updates {{local_symbol}} at {{cusd_ppm}} ppm (TTL {{ttl_seconds}}s)'
---

**sika.oracle** publishes an FX quote for **{{local_symbol}}** used by settlement and **clearyield**. Blocked when `require_signed_push` is enabled without **pushfxsig**.

<h1 class="contract">pushfxsig</h1>

---
spec_version: "0.2.0"
title: Push signed FX attestation
summary: 'Signed FX update for {{local_symbol}} at {{cusd_ppm}} ppm'
---

Updates **{{local_symbol}}** FX quote at **{{cusd_ppm}}** ppm with ECDSA attestation (published {{published_at}}, TTL {{ttl_seconds}}s). Required when signed-only oracle mode is active.

<h1 class="contract">setoraclekey</h1>

---
spec_version: "0.2.0"
title: Register oracle attestation key
summary: '{{authority}} sets oracle key; signed-only={{require_signed_push}}'
---

**{{authority}}** stores the oracle attestation public key and toggles **require_signed_push** for FX updates.

<h1 class="contract">init</h1>

---
spec_version: "0.2.0"
title: Initialize treasury
summary: 'Bootstrap sika.treas settlement singletons (one-time)'
---

One-time initialization of treasury tables and default parameters. Idempotent on already-initialized deployments.

<h1 class="contract">creditreserve</h1>

---
spec_version: "0.2.0"
title: Credit bootstrap reserve
summary: 'Credit {{quantity}} to treasury reserve'
---

Governance action that increases treasury **reserve** by **{{quantity}}** (typically CUSD bootstrap).

