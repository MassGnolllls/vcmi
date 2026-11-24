# A/B test (2 configs, seeds 93, 94)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 24 | 20 | 4 | 0.833 ± 0.152 | - | 0.833 ± 0.152 | - | 24 |
| K | `{"kitePlan": 1, "latePlan": 1}` | 23 | 18 | 5 | 0.783 ± 0.172 | -0.087 ± 0.210 | 0.783 ± 0.172 | -0.087 ± 0.210 | 23 |
