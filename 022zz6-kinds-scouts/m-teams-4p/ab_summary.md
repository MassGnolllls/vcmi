# A/B test (2 configs, seeds 91, 92)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 12 | 0 | 1.000 ± 0.000 | - | 1.000 ± 0.000 | - | 12 |
| X | `{"scouts": 2}` | 12 | 9 | 3 | 0.750 ± 0.256 | -0.250 ± 0.256 | 0.750 ± 0.256 | -0.250 ± 0.256 | 12 |
