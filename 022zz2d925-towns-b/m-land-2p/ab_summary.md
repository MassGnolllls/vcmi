# A/B test (16 configs, seeds 56)

Paired against `nETHK`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| nETHK | `{"campaignEdge": 2.0, "townWorth": 1, "townHop": 1, "freshHeroes": 1}` | 24 | 10 | 6 | 0.565 ± 0.178 | - | 0.595 ± 0.174 | - | 24 |
| nRE | `{"refillShare": 1.0, "campaignEdge": 2.0}` | 24 | 10 | 4 | 0.585 ± 0.163 | +0.019 ± 0.226 | 0.571 ± 0.175 | -0.024 ± 0.244 | 24 |
| nCK | `{"supplyRuns": 1, "freshHeroes": 1}` | 24 | 16 | 0 | 0.856 ± 0.090 | +0.291 ± 0.165 | 0.804 ± 0.120 | +0.208 ± 0.164 | 24 |
| nCRTH | `{"supplyRuns": 1, "refillShare": 1.0, "townWorth": 1, "townHop": 1}` | 24 | 13 | 6 | 0.683 ± 0.174 | +0.118 ± 0.230 | 0.654 ± 0.177 | +0.059 ± 0.242 | 24 |
| nYH | `{"safeParts": 1, "townHop": 1}` | 24 | 12 | 4 | 0.682 ± 0.157 | +0.117 ± 0.161 | 0.637 ± 0.173 | +0.042 ± 0.145 | 24 |
| nYRTK | `{"safeParts": 1, "refillShare": 1.0, "townWorth": 1, "freshHeroes": 1}` | 24 | 7 | 6 | 0.535 ± 0.170 | -0.030 ± 0.233 | 0.512 ± 0.166 | -0.083 ± 0.229 | 24 |
| nYCET | `{"safeParts": 1, "supplyRuns": 1, "campaignEdge": 2.0, "townWorth": 1}` | 23 | 5 | 2 | 0.619 ± 0.139 | +0.029 ± 0.212 | 0.596 ± 0.150 | -0.025 ± 0.211 | 23 |
| nYCREHK | `{"safeParts": 1, "supplyRuns": 1, "refillShare": 1.0, "campaignEdge": 2.0, "townHop": 1, "freshHeroes": 1}` | 24 | 10 | 3 | 0.712 ± 0.147 | +0.147 ± 0.194 | 0.649 ± 0.163 | +0.054 ± 0.216 | 24 |
| nST | `{"scouts": 2, "townWorth": 1}` | 22 | 11 | 5 | 0.592 ± 0.189 | +0.021 ± 0.243 | 0.617 ± 0.187 | +0.013 ± 0.229 | 22 |
| nSRHK | `{"scouts": 2, "refillShare": 1.0, "townHop": 1, "freshHeroes": 1}` | 24 | 10 | 3 | 0.724 ± 0.149 | +0.158 ± 0.188 | 0.696 ± 0.153 | +0.101 ± 0.185 | 24 |
| nSCEH | `{"scouts": 2, "supplyRuns": 1, "campaignEdge": 2.0, "townHop": 1}` | 24 | 12 | 1 | 0.697 ± 0.146 | +0.131 ± 0.220 | 0.702 ± 0.143 | +0.107 ± 0.219 | 24 |
| nSCRETK | `{"scouts": 2, "supplyRuns": 1, "refillShare": 1.0, "campaignEdge": 2.0, "townWorth": 1, "freshHeroes": 1}` | 24 | 15 | 6 | 0.677 ± 0.183 | +0.112 ± 0.196 | 0.679 ± 0.180 | +0.083 ± 0.196 | 24 |
| nSYEK | `{"scouts": 2, "safeParts": 1, "campaignEdge": 2.0, "freshHeroes": 1}` | 24 | 12 | 1 | 0.742 ± 0.134 | +0.177 ± 0.211 | 0.732 ± 0.150 | +0.137 ± 0.221 | 24 |
| nSYRETH | `{"scouts": 2, "safeParts": 1, "refillShare": 1.0, "campaignEdge": 2.0, "townWorth": 1, "townHop": 1}` | 24 | 11 | 1 | 0.729 ± 0.132 | +0.164 ± 0.156 | 0.726 ± 0.135 | +0.131 ± 0.171 | 24 |
| nSYCTHK | `{"scouts": 2, "safeParts": 1, "supplyRuns": 1, "townWorth": 1, "townHop": 1, "freshHeroes": 1}` | 23 | 10 | 6 | 0.512 ± 0.188 | -0.034 ± 0.205 | 0.534 ± 0.184 | -0.043 ± 0.203 | 23 |
| nSYCR | `{"scouts": 2, "safeParts": 1, "supplyRuns": 1, "refillShare": 1.0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
