# A/B test (2 configs, seeds 93, 94)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 10 | 3 | 7 | 0.300 ± 0.299 | - | 0.300 ± 0.299 | - | 10 |
| K | `{"kitePlan": 1, "latePlan": 1}` | 12 | 2 | 10 | 0.167 ± 0.220 | -0.100 ± 0.352 | 0.167 ± 0.220 | -0.100 ± 0.352 | 10 |
