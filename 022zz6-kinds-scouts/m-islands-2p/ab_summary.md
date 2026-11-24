# A/B test (2 configs, seeds 91, 92)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 24 | 14 | 8 | 0.651 ± 0.190 | - | 0.639 ± 0.189 | - | 24 |
| X | `{"scouts": 2}` | 24 | 15 | 5 | 0.742 ± 0.163 | +0.091 ± 0.225 | 0.722 ± 0.166 | +0.083 ± 0.220 | 24 |
