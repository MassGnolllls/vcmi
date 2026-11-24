# A/B test (2 configs, seeds 93, 94)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 11 | 5 | 5 | 0.545 ± 0.309 | - | 0.530 ± 0.301 | - | 11 |
| K | `{"kitePlan": 1, "latePlan": 1}` | 12 | 9 | 2 | 0.827 ± 0.219 | +0.266 ± 0.460 | 0.819 ± 0.218 | +0.273 ± 0.441 | 11 |
