# A/B test (2 configs, seeds 93, 94)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 9 | 3 | 0.750 ± 0.256 | - | 0.750 ± 0.256 | - | 12 |
| K | `{"kitePlan": 1, "latePlan": 1}` | 11 | 8 | 3 | 0.727 ± 0.276 | -0.091 ± 0.414 | 0.727 ± 0.276 | -0.091 ± 0.414 | 11 |
