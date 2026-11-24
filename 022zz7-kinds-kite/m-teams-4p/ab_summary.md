# A/B test (2 configs, seeds 93, 94)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 23 | 20 | 3 | 0.870 ± 0.141 | - | 0.870 ± 0.141 | - | 23 |
| K | `{"kitePlan": 1, "latePlan": 1}` | 24 | 18 | 6 | 0.750 ± 0.177 | -0.087 ± 0.210 | 0.750 ± 0.177 | -0.087 ± 0.210 | 23 |
