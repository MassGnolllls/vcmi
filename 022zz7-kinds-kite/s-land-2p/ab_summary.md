# A/B test (2 configs, seeds 93, 94)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 21 | 5 | 13 | 0.293 ± 0.188 | - | 0.298 ± 0.188 | - | 21 |
| K | `{"kitePlan": 1, "latePlan": 1}` | 24 | 6 | 14 | 0.336 ± 0.182 | +0.048 ± 0.252 | 0.333 ± 0.180 | +0.048 ± 0.252 | 21 |
