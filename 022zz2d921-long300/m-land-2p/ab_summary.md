# A/B test (2 configs, seeds 59)

Paired against `new`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| new | `{}` | 24 | 13 | 11 | 0.542 ± 0.204 | - | 0.542 ± 0.204 | - | 24 |
| nTE | `{"townWorth": 1, "campaignEdge": 2.0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
