# A/B test (3 configs, seeds 42)

Paired against `old`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| old | `{"leaderFocus": 0, "campaignEdge": 2.0, "heroModel": 0, "noChase": 0, "garrisonRoom": 0, "collectorsTakeGarrison": 1}` | 24 | 1 | 6 | 0.450 ± 0.142 | - | 0.280 ± 0.100 | - | 24 |
| new | `{}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| newS | `{"scouts": 2}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
