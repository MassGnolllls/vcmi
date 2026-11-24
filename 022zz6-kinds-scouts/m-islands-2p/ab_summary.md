# A/B test (2 configs, seeds 91, 92)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 7 | 4 | 0.643 ± 0.272 | - | 0.625 ± 0.273 | - | 12 |
| X | `{"scouts": 2}` | 12 | 6 | 4 | 0.628 ± 0.268 | -0.015 ± 0.405 | 0.611 ± 0.268 | -0.014 ± 0.401 | 12 |
