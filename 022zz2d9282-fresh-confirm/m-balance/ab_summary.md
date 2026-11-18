# A/B test (5 configs, seeds 70)

Paired against `new`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| new | `{}` | 24 | 11 | 12 | 0.497 ± 0.203 | - | 0.494 ± 0.202 | - | 24 |
| nW | `{"magicWorth": 1}` | 24 | 8 | 14 | 0.414 ± 0.200 | -0.083 ± 0.232 | 0.404 ± 0.196 | -0.090 ± 0.225 | 24 |
| nWB | `{"magicWorth": 1, "boldBehind": 1}` | 22 | 4 | 15 | 0.256 ± 0.173 | -0.242 ± 0.188 | 0.214 ± 0.167 | -0.279 ± 0.185 | 22 |
| nWBK | `{"magicWorth": 1, "boldBehind": 1, "breakout": 3}` | 24 | 10 | 12 | 0.450 ± 0.195 | -0.047 ± 0.305 | 0.440 ± 0.196 | -0.054 ± 0.304 | 24 |
| nWBKG | `{"magicWorth": 1, "boldBehind": 1, "breakout": 3, "townHeroes": 1, "secondArmy": 1, "nightWatch": 1, "gather": 2}` | 24 | 11 | 12 | 0.480 ± 0.200 | -0.017 ± 0.239 | 0.470 ± 0.200 | -0.024 ± 0.243 | 24 |
