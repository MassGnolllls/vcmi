# A/B test (2 configs, seeds 91, 92)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 1 | 11 | 0.083 ± 0.163 | - | 0.083 ± 0.163 | - | 12 |
| X | `{"scouts": 2}` | 11 | 0 | 11 | 0.000 ± 0.000 | -0.091 ± 0.178 | 0.000 ± 0.000 | -0.091 ± 0.178 | 11 |
