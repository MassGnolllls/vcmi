# A/B test (2 configs, seeds 93, 94)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 21 | 6 | 14 | 0.332 ± 0.206 | - | 0.327 ± 0.203 | - | 21 |
| K | `{"kitePlan": 1, "latePlan": 1}` | 24 | 4 | 20 | 0.167 ± 0.152 | -0.141 ± 0.203 | 0.167 ± 0.152 | -0.136 ± 0.199 | 21 |
