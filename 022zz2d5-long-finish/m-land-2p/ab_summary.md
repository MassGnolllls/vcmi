# A/B test (9 configs, seeds 42)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 24 | 0 | 7 | 0.543 ± 0.146 | - | 0.326 ± 0.093 | - | 24 |
| f0 | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 2}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| fSR | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 2, "scouts": 2, "garrisonRoom": 1, "collectorsTakeGarrison": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| fDR | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 2, "townDefenceValue": 40000, "garrisonRoom": 1, "collectorsTakeGarrison": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| fDS | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 2, "townDefenceValue": 40000, "scouts": 2}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| fNR | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 1, "garrisonRoom": 1, "collectorsTakeGarrison": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| fNS | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 1, "scouts": 2}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| fND | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 1, "townDefenceValue": 40000}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| fNDSR | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 1, "townDefenceValue": 40000, "scouts": 2, "garrisonRoom": 1, "collectorsTakeGarrison": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
