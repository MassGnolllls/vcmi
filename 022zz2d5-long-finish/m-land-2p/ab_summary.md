# A/B test (9 configs, seeds 42)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 24 | 0 | 7 | 0.543 ± 0.146 | - | 0.326 ± 0.093 | - | 24 |
| f0 | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 2}` | 24 | 5 | 3 | 0.523 ± 0.140 | -0.020 ± 0.153 | 0.464 ± 0.144 | +0.138 ± 0.139 | 24 |
| fSR | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 2, "scouts": 2, "garrisonRoom": 1, "collectorsTakeGarrison": 0}` | 24 | 12 | 5 | 0.633 ± 0.174 | +0.090 ± 0.236 | 0.655 ± 0.172 | +0.328 ± 0.202 | 24 |
| fDR | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 2, "townDefenceValue": 40000, "garrisonRoom": 1, "collectorsTakeGarrison": 0}` | 23 | 11 | 4 | 0.656 ± 0.157 | +0.119 ± 0.229 | 0.602 ± 0.172 | +0.281 ± 0.201 | 23 |
| fDS | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 2, "townDefenceValue": 40000, "scouts": 2}` | 24 | 10 | 9 | 0.484 ± 0.191 | -0.058 ± 0.236 | 0.470 ± 0.190 | +0.144 ± 0.209 | 24 |
| fNR | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 1, "garrisonRoom": 1, "collectorsTakeGarrison": 0}` | 24 | 9 | 5 | 0.533 ± 0.163 | -0.010 ± 0.156 | 0.488 ± 0.175 | +0.162 ± 0.155 | 24 |
| fNS | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 1, "scouts": 2}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| fND | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 1, "townDefenceValue": 40000}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| fNDSR | `{"leaderFocus": 1, "campaignEdge": 3.0, "heroModel": 1, "noChase": 1, "townDefenceValue": 40000, "scouts": 2, "garrisonRoom": 1, "collectorsTakeGarrison": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
