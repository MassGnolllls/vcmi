# A/B test (5 configs, seeds 70)

Paired against `new`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| new | `{}` | 24 | 16 | 8 | 0.667 ± 0.193 | - | 0.667 ± 0.193 | - | 24 |
| nW | `{"magicWorth": 1}` | 24 | 19 | 5 | 0.792 ± 0.166 | +0.125 ± 0.135 | 0.792 ± 0.166 | +0.125 ± 0.135 | 24 |
| nWB | `{"magicWorth": 1, "boldBehind": 1}` | 23 | 18 | 4 | 0.819 ± 0.158 | +0.124 ± 0.182 | 0.801 ± 0.161 | +0.106 ± 0.173 | 23 |
| nWBK | `{"magicWorth": 1, "boldBehind": 1, "breakout": 3}` | 24 | 18 | 6 | 0.750 ± 0.177 | +0.083 ± 0.163 | 0.750 ± 0.177 | +0.083 ± 0.163 | 24 |
| nWBKG | `{"magicWorth": 1, "boldBehind": 1, "breakout": 3, "townHeroes": 1, "secondArmy": 1, "nightWatch": 1, "gather": 2}` | 24 | 16 | 7 | 0.699 ± 0.184 | +0.032 ± 0.145 | 0.690 ± 0.184 | +0.024 ± 0.149 | 24 |
