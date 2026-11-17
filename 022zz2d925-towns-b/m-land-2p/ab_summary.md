# A/B test (16 configs, seeds 56)

Paired against `nETHK`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| nETHK | `{"campaignEdge": 2.0, "townWorth": 1, "townHop": 1, "freshHeroes": 1}` | 24 | 10 | 6 | 0.565 ± 0.178 | - | 0.595 ± 0.174 | - | 24 |
| nRE | `{"refillShare": 1.0, "campaignEdge": 2.0}` | 24 | 10 | 4 | 0.585 ± 0.163 | +0.019 ± 0.226 | 0.571 ± 0.175 | -0.024 ± 0.244 | 24 |
| nCK | `{"supplyRuns": 1, "freshHeroes": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nCRTH | `{"supplyRuns": 1, "refillShare": 1.0, "townWorth": 1, "townHop": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nYH | `{"safeParts": 1, "townHop": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nYRTK | `{"safeParts": 1, "refillShare": 1.0, "townWorth": 1, "freshHeroes": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nYCET | `{"safeParts": 1, "supplyRuns": 1, "campaignEdge": 2.0, "townWorth": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nYCREHK | `{"safeParts": 1, "supplyRuns": 1, "refillShare": 1.0, "campaignEdge": 2.0, "townHop": 1, "freshHeroes": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nST | `{"scouts": 2, "townWorth": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSRHK | `{"scouts": 2, "refillShare": 1.0, "townHop": 1, "freshHeroes": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSCEH | `{"scouts": 2, "supplyRuns": 1, "campaignEdge": 2.0, "townHop": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSCRETK | `{"scouts": 2, "supplyRuns": 1, "refillShare": 1.0, "campaignEdge": 2.0, "townWorth": 1, "freshHeroes": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSYEK | `{"scouts": 2, "safeParts": 1, "campaignEdge": 2.0, "freshHeroes": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSYRETH | `{"scouts": 2, "safeParts": 1, "refillShare": 1.0, "campaignEdge": 2.0, "townWorth": 1, "townHop": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSYCTHK | `{"scouts": 2, "safeParts": 1, "supplyRuns": 1, "townWorth": 1, "townHop": 1, "freshHeroes": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSYCR | `{"scouts": 2, "safeParts": 1, "supplyRuns": 1, "refillShare": 1.0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
