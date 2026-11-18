# A/B test (5 configs, seeds 70)

Paired against `new`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| new | `{}` | 24 | 6 | 12 | 0.451 ± 0.198 | - | 0.417 ± 0.183 | - | 24 |
| nW | `{"magicWorth": 1}` | 24 | 8 | 11 | 0.497 ± 0.191 | +0.046 ± 0.268 | 0.458 ± 0.185 | +0.042 ± 0.244 | 24 |
| nWB | `{"magicWorth": 1, "boldBehind": 1}` | 24 | 8 | 9 | 0.566 ± 0.196 | +0.115 ± 0.206 | 0.542 ± 0.186 | +0.125 ± 0.188 | 24 |
| nWBK | `{"magicWorth": 1, "boldBehind": 1, "breakout": 3}` | 24 | 10 | 10 | 0.571 ± 0.198 | +0.121 ± 0.285 | 0.542 ± 0.192 | +0.125 ± 0.266 | 24 |
| nWBKG | `{"magicWorth": 1, "boldBehind": 1, "breakout": 3, "townHeroes": 1, "secondArmy": 1, "nightWatch": 1, "gather": 2}` | 24 | 14 | 8 | 0.665 ± 0.192 | +0.214 ± 0.231 | 0.639 ± 0.189 | +0.222 ± 0.219 | 24 |
