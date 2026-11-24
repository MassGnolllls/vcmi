# A/B test (2 configs, seeds 93, 94)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 11 | 9 | 2 | 0.818 ± 0.239 | - | 0.818 ± 0.239 | - | 11 |
| K | `{"kitePlan": 1, "latePlan": 1}` | 12 | 9 | 3 | 0.750 ± 0.256 | +0.000 ± 0.374 | 0.750 ± 0.256 | +0.000 ± 0.374 | 11 |
