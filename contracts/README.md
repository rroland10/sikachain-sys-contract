# SikaChain System Contracts

C++ smart contracts for the SikaChain protocol. Account names are centralized in `include/sika.accounts.hpp` (keep in sync with `spring/sikachaindev/accounts.json`).

## What's in here

| Contract | Deployed to | Responsibility |
|---|---|---|
| `sika.system` | `eosio` (Spring privileged) | BPs, voting, REX, RAM, halving inflation, vote-removal enforcement |
| `sika.token` | `sika.token` | Multi-symbol fungible tokens — SIKA and cGHS |
| `sika.rep` | `sika.rep` | Representative registry with **protocol-enforced +5% APY boost cap** |
| `sika.guard` | `sika.guard` | 9-seat Network Guardian multisig + quarterly knockout elections |
| `sika.rules` | `sika.rules` | Protocol amendments: 17-of-21 BP supermajority + 7-day Guardian veto window |
| `sika.issue` | `sika.issue` | Stablecoin / RWA issuer registry with KYC + reserve attestation + Guardian approval |

## Protocol design

The core Antelope DPoS pattern (registered BPs with weighted votes, REX, RAM Bancor, CPU/NET staking) is preserved for wallet and explorer compatibility. SikaChain-specific pieces:

**Halving inflation (Article V)** — Bitcoin-style. Year 1 is 1.00% APR, halves every 4 years. Asymptotic supply ceiling: 8.64B SIKA (8B initial + 0.64B lifetime inflation). Implemented in `sika.system/src/producer_pay.cpp::inflation_bps_for_year`.

**75/25 BP distribution** — 75% of yearly inflation goes to the Top 21 (split 25% per-block + 75% per-vote-share). 25% goes to Standby BPs (ranks 22-50). Forfeited shares from probationary BPs return to the pool, not destroyed.

**Vote-removal-only enforcement (Articles IV & X)** — When a BP fails any of the 5 compliance rules (uptime > 95%, public RPC, ≥ 1M SIKA staked, network upgrades on time, issues resolved within 7 days), they enter probation. After 7 days unresolved, they lose their seat — but **no SIKA is ever destroyed**. Voters keep every coin. Implemented in `sika.system/src/enforcement.cpp`.

**Representatives with +5% APY boost cap (Articles III & V)** — Anyone can register as a Representative via `sika.rep::regrep` and offer delegators a yield boost on top of base REX. The cap is mechanically enforced at the source: `setboost` rejects any value > 500 basis points. There is no admin override. Implemented in `sika.rep/src/sika.rep.cpp`.

**REX yield paid in cGHS, not SIKA** — Users stake SIKA but earn yield in cGHS (the GHS-pegged stablecoin). This isolates yield from SIKA price volatility for Ghanaian users who think in cedis. Implemented in `sika.system/src/rex.cpp::claimrewards`.

**RAM Bancor with 0.5% fee split (Article VI)** — Every buyram and sellram extracts a 0.5% fee. Half goes to the REX cGHS yield pool, half is permanently burned. Implemented in `sika.system/src/ram_bancor.cpp::route_ram_fee`.

**Network Guardians (Article VII)** — 9 seats, 6-of-9 multisig over a 720M SIKA reserve. Elected quarterly via a 3-round knockout playoff (27 → 18 → 9). Powers: open/close BP issues, approve issuers, veto Rules amendments. Implemented in `sika.guard`.

**Rules amendments (Article VIII)** — Anyone may propose; requires 17-of-21 BP supermajority; then a 7-day Guardian veto window. Only after the window expires unvetoed does the amendment execute. Implemented in `sika.rules`.

**Open-platform issuer registry (Article IX)** — Stablecoins (cNGN, cKES) and RWAs (cGOLD, cCOCOA) onboard via `sika.issue`. Flow: applicant submits → auditor attests → Guardians approve (6-of-9). Live issuers must re-attest reserves monthly and audit annually or they auto-pause. Implemented in `sika.issue`.

## Directory layout

```
contracts/
├── CMakeLists.txt              # Top-level build
├── build.sh                    # One-command build
├── README.md                   # This file
│
├── sika.system/
│   ├── CMakeLists.txt
│   ├── include/sika.system/
│   │   ├── sika.system.hpp     # Main header: tables, actions, constants
│   │   ├── exchange_state.hpp  # RAM Bancor curve interface
│   │   └── native.hpp          # Native intrinsic action wrappers
│   └── src/
│       ├── sika.system.cpp     # Constructor, init, voting, regproducer, delegatebw
│       ├── producer_pay.cpp    # Halving inflation, 75/25 distribution
│       ├── enforcement.cpp     # 5-rule Article IV with vote-removal-only
│       ├── rex.cpp             # REX (stake SIKA, earn cGHS)
│       ├── ram_bancor.cpp      # buyram/sellram with 0.5% fee
│       └── exchange_state.cpp  # Bancor curve math
│
├── sika.token/   include/ + src/
├── sika.rep/     include/ + src/
├── sika.guard/   include/ + src/
├── sika.rules/   include/ + src/
└── sika.issue/   include/ + src/
```

## Building

**Prerequisites:** Antelope CDT 4.1.0 or later. Install from the [CDT releases page](https://github.com/AntelopeIO/cdt/releases) (Ubuntu .deb is easiest), or build from source and set `CDT_BUILD_PATH`.

```bash
cd contracts
./build.sh                  # Release build, no tests, all 6 contracts
./build.sh --with-tests     # Also build the Spring-based test suite
                            # (requires SPRING_BUILD_PATH)
./build.sh clean            # Wipe build/
```

Produces, in `build/contracts/<name>/`:

```
sika.system.wasm  + sika.system.abi
...
```

### Ricardian (human-readable signing)

Earn / settlement user actions include Ricardian templates under `<contract>/ricardian/*.contracts.md`. CMake wires them via `target_ricardian_directory`. After build:

```bash
node ../spring/sikachaindev/scripts/verify-ricardian.mjs
```

## Deployment order

At chain genesis, contracts must be deployed in a specific order because they reference each other:

1. **Create system accounts** (in order: `sika.token`, `sika.rex`, `sika.rep`, `sika.guard`, `sika.rules`, `sika.issue`, `sika.boost`, `sika.bppay`, `sika.burn`, `sika.gold`, `sika.cocoa`, `sika.cngn`, `sika.ckes`).
2. **Deploy `sika.token`** to `sika.token` and create the SIKA symbol with 8.64B max supply and `eosio` as issuer.
3. **Deploy `sika.system`** to `eosio` (Spring privileged account).
4. **Activate protocol features** required by the system contract.
5. **Call `eosio::init(0, "4,SIKA")`** to seed the genesis timestamp and RAM market.
6. **Issue the initial 8B SIKA** to the genesis vesting accounts (per the allocation table in the marketing page).
7. **Create the cGHS symbol** on `sika.token`, issuer = `sika.issue`.
8. **Deploy `sika.guard`** and seed the initial 9 Guardians (founding council; rotates out after first quarterly election).
9. **Deploy `sika.rep`**, **`sika.rules`**, **`sika.issue`**.
10. **Configure `sika.guard::active` permission** to require 6-of-9 from the founding Guardians.
11. **Configure `sika.rules::active` permission** to be granted only by 17-of-21 BP signatures via `sika.msig`.

## System accounts

All ≤ 12 characters (AntelopeOS constraint). Canonical names in `include/sika.accounts.hpp`.

| Account | Length | Purpose |
|---|---|---|
| `eosio` | 5 | Spring privileged account — hosts `sika.system` WASM |
| `sika.token` | 10 | Token contract (SIKA + cGHS) |
| `sika.rex` | 8 | REX pool custody |
| `sika.rep` | 8 | Representative registry |
| `sika.guard` | 10 | Network Guardian multisig |
| `sika.rules` | 10 | Rules amendments |
| `sika.issue` | 10 | Issuer registry |
| `sika.boost` | 10 | Rep boost cGHS pool |
| `sika.bppay` | 10 | BP inflation rewards |
| `sika.burn` | 9 | Burn destination |
| `sika.gold` | 9 | cGOLD issuer (RWA) |
| `sika.cocoa` | 10 | cCOCOA issuer (RWA) |
| `sika.cngn` | 9 | cNGN issuer (stablecoin) |
| `sika.ckes` | 9 | cKES issuer (stablecoin) |

A previous draft used `sika.repboost` (13 chars) for the Rep boost pool — caught by the integration test suite, renamed to `sika.boost`.

## Constants — single source of truth

The same constants appear in **four places** and **must stay in sync**:

| Constant | TypeScript (`domain.ts`) | C++ (`sika.system.hpp` + `sika.accounts.hpp`) | `accounts.json` | Prisma (`schema.prisma`) |
|---|---|---|---|---|
| Token contract | `sika.token` in config | `sikaaccounts::TOKEN` | `"token": "sika.token"` | — |
| 1M SIKA stake floor | `STAKE_FLOOR_RAW = 1_000_000n × 10_000n` | `bp_stake_floor = 10'000'000'000ll` | — | (BpComplianceState checks) |
| 95% uptime | `UPTIME_FLOOR = 95.0` | `uptime_floor_bps = 9500` | — | (Producer model) |
| 7-day SLA | `ISSUE_SLA_DAYS = 7` | `sla_seconds = 7*24*60*60` | — | (Producer.openIssueAt) |
| +5% Rep boost cap | `REP_BOOST_CAP = 5.0` | `rep_boost_cap_bps = 500` | — | (Representative.boostApy) |
| 30 BPs per vote | (voteproducer schema) | `max_bps_per_vote = 30` | — | (Vote.bpSelection max length) |
| 0.5% RAM fee | (not modeled off-chain) | `ram_fee_bps = 50` | — | — |
| Halving every 4 years | `halving_epoch_years = 4` | `halving_epoch_years = 4` | — | — |
| 8B initial supply | `8_000_000_000` in `maxSikaSupply` | `initial_sika_supply = 80'000'000'000'000ll` | — | — |
| 8.64B ceiling | `initialSupply + initialSupply × 0.08` | `max_sika_supply = 86'400'000'000'000ll` | — | — |
| 75/25 BP split | (landing page) | `top21_share_bps = 7500`, `standby_share_bps = 2500` | — | — |

If you change a constant in one place, **change it in all synced locations** and update the integration tests.

## Testing

The contract tests in `tests/` (built with `--with-tests`) use Spring's BOOST framework to exercise full transaction flows. They cover:

- Halving math at the year 1/4/8/12/16 boundaries against `domain.test.ts` expectations
- 5-rule probation transitions (apply / clear / vote removal at 7 days)
- Rep boost cap rejection (501 bps must fail)
- REX cGHS payout math vs the off-chain Zod yield calculator
- RAM Bancor 0.5% fee routing (50% boost, 50% burn)
- 17-of-21 amendment approval + Guardian veto path
- Issuer lifecycle (apply → audit → approve → mint → pause)

Off-chain, the TypeScript test suite (`backend/src/types/domain.test.ts`) already has 101 tests covering the same invariants from the JavaScript side. The two suites are kept in parity by sharing the constants table above.

## Production deployment notes

- **Privilege:** Only `sika.system` should be privileged. Other contracts must be unprivileged so their actions still pay RAM/CPU.
- **Auth wiring:** After elections seat new Guardians, `sika.guard::active` must be reconfigured to require 6-of-9 of the new seats. This is done via a Guardian-approved proposal that calls `eosio::updateauth`.
- **Cross-contract reads:** `sika.rules::current_top21` and `sika.rep::validate_bp_slate` currently stub the cross-contract table reads. Before mainnet, replace with either a shared header that declares `sika.system::producer_info` in full, or add view actions on `sika.system` that the other contracts call.
- **Genesis vesting:** The 8B SIKA initial mint must be split across the buckets in the marketing page allocation table. Use multi-sig escrow accounts (`sika.vest1`, `sika.vest2`, etc.) with cliff/linear unlock schedules enforced by a separate `sika.vesting` contract (not in this initial set).
- **Auditors:** Before any issuer applies, Guardians must whitelist at least one auditor via `sika.issue::addauditor`.
