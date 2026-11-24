# A/B test (2 configs, seeds 93, 94)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 23 | 13 | 7 | 0.695 ± 0.192 | - | 0.674 ± 0.188 | - | 23 |
| K | `{"kitePlan": 1, "latePlan": 1}` | 23 | 16 | 5 | 0.778 ± 0.172 | +0.087 ± 0.284 | 0.761 ± 0.170 | +0.091 ± 0.279 | 22 |
