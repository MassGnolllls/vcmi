# A/B test (2 configs, seeds 93, 94)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 44 | 14 | 24 | 0.368 ± 0.138 | - | 0.375 ± 0.137 | - | 44 |
| K | `{"kitePlan": 1, "latePlan": 1}` | 46 | 12 | 27 | 0.330 ± 0.130 | -0.044 ± 0.159 | 0.332 ± 0.129 | -0.047 ± 0.159 | 43 |
