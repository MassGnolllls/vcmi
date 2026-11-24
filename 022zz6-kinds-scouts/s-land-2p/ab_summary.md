# A/B test (2 configs, seeds 91, 92)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 47 | 15 | 24 | 0.391 ± 0.133 | - | 0.398 ± 0.132 | - | 47 |
| X | `{"scouts": 2}` | 48 | 18 | 21 | 0.451 ± 0.132 | +0.070 ± 0.144 | 0.463 ± 0.132 | +0.074 ± 0.143 | 47 |
