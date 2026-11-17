# A/B test (9 configs, seeds 43)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 24 | 0 | 4 | 0.516 ± 0.125 | - | 0.351 ± 0.089 | - | 24 |
| fU | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 2, "surrender": 1, "veterans": 1}` | 24 | 7 | 7 | 0.454 ± 0.162 | -0.062 ± 0.209 | 0.381 ± 0.168 | +0.030 ± 0.187 | 24 |
| fSR | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 2, "scouts": 2, "garrisonRoom": 1, "collectorsTakeGarrison": 0}` | 24 | 10 | 8 | 0.550 ± 0.182 | +0.034 ± 0.201 | 0.542 ± 0.187 | +0.190 ± 0.185 | 24 |
| fDRU | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 2, "townDefenceValue": 40000, "garrisonRoom": 1, "collectorsTakeGarrison": 0, "surrender": 1, "veterans": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| fDS | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 2, "townDefenceValue": 40000, "scouts": 2}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| fNR | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 1, "garrisonRoom": 1, "collectorsTakeGarrison": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| fNSU | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 1, "scouts": 2, "surrender": 1, "veterans": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| fND | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 1, "townDefenceValue": 40000}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| fNDSRU | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 1, "townDefenceValue": 40000, "scouts": 2, "garrisonRoom": 1, "collectorsTakeGarrison": 0, "surrender": 1, "veterans": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
