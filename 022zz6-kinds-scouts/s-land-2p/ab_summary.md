# A/B test (2 configs, seeds 91, 92)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 23 | 9 | 10 | 0.487 ± 0.199 | - | 0.478 ± 0.196 | - | 23 |
| X | `{"scouts": 2}` | 24 | 9 | 10 | 0.480 ± 0.190 | +0.014 ± 0.229 | 0.479 ± 0.187 | +0.022 ± 0.226 | 23 |
