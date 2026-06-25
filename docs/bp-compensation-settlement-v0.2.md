# BP Compensation & Multi-Country Settlement — v0.2

> **Status:** proposed / in progress. Economics not final — see §10 parameter modelling before mainnet.

This document tracks implementation against the v0.2 design (two-tier BP pay, settlement layer, REX payout generalisation).

## Implementation map

| Spec § | Component | Status |
|--------|-----------|--------|
| 4 | Two-tier BP pay (`producer_pay.cpp`) | **Verified dev:** Tier-1 CUSD + Tier-2 vest → `claimvest` → REX |
| 5 | `sika.treas` settlement contract | **Partial:** full action set live; FX via `fxquotes` + `pushfx` oracle |
| 5 | `sika.cost`, `sika.usd` accounts | **Reserved** on dev chain (no WASM) |
| 6 | REX reference-unit yield + `payout_currency` | **Partial:** `claimrexyld` + `clearyield`; RAM fees **compound SIKA** into REX (v0.2 §6.2) |
| 8 | Usage-gated Tier-2 inflation | **Scaffold:** `accrueepochfee`, `usage_gated_tier2`, `setvestparams` (off by default) |
| 9.5 | BP vesting + auto-REX | **Scaffold:** vest table + `claimvest`; **forfeit** on probation/unreg/vote-removal |
| 9.6 | Backend settlement worker | **Dev:** `settlement-worker.mjs`; **API:** `POST /api/v0/settlement` |

## On-chain artifacts

```
sika.treas   — settlement WASM (scaffold)
sika.cost    — Tier-1 payout custody (account only)
sika.usd     — cUSD issuer (account only; future sika.issue child)
```

Build: `contracts/build.sh` (requires Antelope CDT 4.1+)

Deploy: `sikachaindev/scripts/deploy-sika-system.sh` (or `upgrade-contracts.sh`) creates CUSD, deploys `sika.treas`, runs `init` / `setparams` / `creditreserve`, and sets `--add-code` on the contract account.

**Verified (SikaChainDev):** `sikabpa` `claimprod` paid **250.0000 CUSD** from reserve (50k bootstrap → 49.75k after one claim).

### Scaffold actions (`sika.treas`)

| Action | Status |
|--------|--------|
| `init` | Implemented |
| `setparams` | Implemented (governance: `eosio` / `sika.rules`) |
| `accruefee` | Implemented — appends to `marketpnl` ledger |
| `paycost` | Implemented — Tier-1 cUSD from reserve (skips if unfunded) |
| `creditreserve` | Implemented — sync reserve after CUSD issue |
| `sweep` | FX via `fxquotes`; credits reserve + `accruepoch` |
| `setfx` | Governance FX ppm + TTL override |
| `pushfx` | Oracle feed (`sika.oracle`) — licensed-rail FX updates |
| `clearyield` | Implemented — REX payout settlement (any 4-dec local via FX table) |
| `subsidize` | Implemented — capped cross-market ledger (CUSD reference) |
| `rebalance` | Implemented — reserve cUSD/gGOLD split vs `reserve_gold_bps` (accounting MVP) |

## Design invariants (summary)

1. SIKA is global and singular.
2. Network actors (BPs) paid in **cUSD** (Tier-1), not consumer stables.
3. Consumer REX yield paid in **local stable at claim**, pool accounted in cUSD-equivalent internally.
4. Tier-2 SIKA bonus is vested, usage-gated, auto-staked to REX.
5. Cross-market subsidy is explicit ledger entries, capped by governance.

## Dev chain

- 21 BPs for schedule/vote testing: `sikachaindev/scripts/bootstrap-21bp.sh`
- Oracle FX push: `node sikachaindev/scripts/oracle-push-fx.mjs`
- Settlement worker: `settlement-sweep.sh` (one-shot) or `settlement-worker.mjs` (FX + sweep interval)
- App cron endpoint: `POST /api/v0/settlement` (set `SETTLEMENT_CRON_SECRET`)
- Smoke test: `sikachaindev/scripts/verify-settlement-v0.2.sh`
- Tier-2 vest test: `sikachaindev/scripts/verify-tier2-vesting.sh` (uses `refillpay` + `SIKA_VEST_SECONDS=60` for fast release)
- Contract build (Docker): `sikachaindev/scripts/build-sika-contracts-docker.sh`
- Tier-2 vesting on deploy: `TIER2_VESTING_ENABLE=1 bash upgrade-contracts.sh`
- Genesis cohort (7–9) is a **governance/ops** choice, not enforced on-chain yet.

## Next build steps

1. Model §10 parameters (reserve size, `inflation_gain`, `sweep_slice_bps`, `fee_to_yield_bps`).
2. Licensed-rail FX oracle feed — **`pushfx` live**; production: signed attestations + HSM for `sika.oracle`
3. BullMQ worker in Sika app backend — interim: cron → `/api/v0/settlement` or `settlement-worker.mjs`
4. Two-leg yield (SIKA auto-compound leg) + per-market payout prefs.
5. Generalise `crediboost` to reference-unit deposits + FX oracle. **Done:** reads `sika.treas::fxquotes`
