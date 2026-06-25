<h1 class="contract">transfer</h1>

---
spec_version: "0.2.0"
title: Send tokens
summary: '{{nowrap from}} sends {{nowrap quantity}} to {{nowrap to}}'
---

{{from}} authorizes a transfer of **{{quantity}}** to **{{to}}**.

{{#if memo}}
Memo: {{memo}}
{{/if}}

This action moves tokens from {{from}}'s balance to {{to}}. It cannot be reversed except by a new transfer from the recipient.

<h1 class="contract">open</h1>

---
spec_version: "0.2.0"
title: Open token balance
summary: '{{nowrap owner}} opens a {{symbol}} balance (RAM paid by {{ram_payer}})'
---

Creates a zero balance row for **{{owner}}** for symbol **{{symbol}}**. Account **{{ram_payer}}** pays the RAM cost for the new table row.

<h1 class="contract">issue</h1>

---
spec_version: "0.2.0"
title: Issue tokens
summary: 'Issuer mints {{nowrap quantity}} to {{nowrap to}}'
---

The token issuer mints **{{quantity}}** and credits **{{to}}**.

{{#if memo}}
Memo: {{memo}}
{{/if}}

Only the symbol issuer may authorize this action.

<h1 class="contract">create</h1>

---
spec_version: "0.2.0"
title: Create token symbol
summary: 'Issuer defines {{maximum_supply}} with issuer {{issuer}}'
---

The token issuer registers a new **{{maximum_supply}}** symbol on **sika.token**. This is a one-time setup per symbol; only privileged issuers may call **create**.

<h1 class="contract">close</h1>

---
spec_version: "0.2.0"
title: Close token balance
summary: '{{owner}} closes their {{symbol}} balance row'
---

**{{owner}}** removes their zero (or emptied) **{{symbol}}** balance row and recovers the RAM it occupied.

<h1 class="contract">retire</h1>

---
spec_version: "0.2.0"
title: Retire (burn) tokens
summary: 'Issuer burns {{quantity}} from circulation'
---

The token issuer permanently removes **{{quantity}}** from the symbol supply (burn).

{{#if memo}}
Memo: {{memo}}
{{/if}}

Only the symbol issuer may authorize this action.
