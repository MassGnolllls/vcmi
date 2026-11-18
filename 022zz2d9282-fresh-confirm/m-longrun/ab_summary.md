# A/B test (5 configs, seeds 70)

Paired against `new`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| new | `{}` | 24 | 8 | 16 | 0.333 ± 0.193 | - | 0.333 ± 0.193 | - | 24 |
| nW | `{"magicWorth": 1}` | 22 | 11 | 11 | 0.500 ± 0.214 | +0.136 ± 0.234 | 0.500 ± 0.214 | +0.136 ± 0.234 | 22 |
| nWB | `{"magicWorth": 1, "boldBehind": 1}` | 23 | 10 | 9 | 0.578 ± 0.197 | +0.274 ± 0.230 | 0.572 ± 0.197 | +0.268 ± 0.222 | 23 |
| nWBK | `{"magicWorth": 1, "boldBehind": 1, "breakout": 3}` | 24 | 10 | 13 | 0.453 ± 0.201 | +0.119 ± 0.211 | 0.436 ± 0.198 | +0.103 ± 0.204 | 24 |
| nWBKG | `{"magicWorth": 1, "boldBehind": 1, "breakout": 3, "townHeroes": 1, "secondArmy": 1, "nightWatch": 1, "gather": 2}` | 23 | 11 | 10 | 0.520 ± 0.200 | +0.172 ± 0.276 | 0.513 ± 0.200 | +0.165 ± 0.274 | 23 |
