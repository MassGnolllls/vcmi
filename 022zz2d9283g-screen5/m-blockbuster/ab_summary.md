# A/B test (16 configs, seeds 66)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 4 | 4 | 0.592 ± 0.261 | - | 0.514 ± 0.262 | - | 12 |
| nDKBQ | `{"tour": 1, "kitePlan": 1, "latePlan": 1, "breakout": 3, "skillChoice": 1, "primaryByArmy": 1}` | 12 | 9 | 2 | 0.783 ± 0.229 | +0.191 ± 0.298 | 0.764 ± 0.243 | +0.250 ± 0.318 | 12 |
| nSKBA | `{"threatSpend": 2, "kitePlan": 1, "latePlan": 1, "breakout": 3, "artifactCare": 1}` | 10 | 5 | 2 | 0.790 ± 0.258 | +0.212 ± 0.390 | 0.750 ± 0.249 | +0.233 ± 0.402 | 10 |
| nSDAQ | `{"threatSpend": 2, "tour": 1, "artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1}` | 11 | 5 | 2 | 0.765 ± 0.229 | +0.119 ± 0.241 | 0.712 ± 0.229 | +0.152 ± 0.218 | 11 |
| nRKAQ | `{"secondArmy": 1, "secondArmyEdge": 0.6, "kitePlan": 1, "latePlan": 1, "artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1}` | 12 | 7 | 3 | 0.750 ± 0.256 | +0.158 ± 0.240 | 0.708 ± 0.248 | +0.194 ± 0.234 | 12 |
| nRDBA | `{"secondArmy": 1, "secondArmyEdge": 0.6, "tour": 1, "breakout": 3, "artifactCare": 1}` | 12 | 5 | 6 | 0.498 ± 0.294 | -0.094 ± 0.442 | 0.486 ± 0.288 | -0.028 ± 0.412 | 12 |
| nRSBQ | `{"secondArmy": 1, "secondArmyEdge": 0.6, "threatSpend": 2, "breakout": 3, "skillChoice": 1, "primaryByArmy": 1}` | 11 | 4 | 5 | 0.466 ± 0.301 | -0.102 ± 0.415 | 0.455 ± 0.292 | -0.030 ± 0.415 | 11 |
| nRSDK | `{"secondArmy": 1, "secondArmyEdge": 0.6, "threatSpend": 2, "tour": 1, "kitePlan": 1, "latePlan": 1}` | 12 | 6 | 6 | 0.500 ± 0.295 | -0.092 ± 0.332 | 0.500 ± 0.295 | -0.014 ± 0.320 | 12 |
| nFBAQ | `{"fightForecast": 2, "forecastPlans": 1, "breakout": 3, "artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1}` | 10 | 3 | 4 | 0.505 ± 0.295 | -0.120 ± 0.360 | 0.417 ± 0.277 | -0.117 ± 0.351 | 10 |
| nFDKA | `{"fightForecast": 2, "forecastPlans": 1, "tour": 1, "kitePlan": 1, "latePlan": 1, "artifactCare": 1}` | 11 | 5 | 4 | 0.629 ± 0.295 | +0.070 ± 0.309 | 0.591 ± 0.283 | +0.106 ± 0.254 | 11 |
| nFSKQ | `{"fightForecast": 2, "forecastPlans": 1, "threatSpend": 2, "kitePlan": 1, "latePlan": 1, "skillChoice": 1, "primaryByArmy": 1}` | 11 | 3 | 5 | 0.522 ± 0.297 | -0.046 ± 0.434 | 0.467 ± 0.276 | -0.018 ± 0.381 | 11 |
| nFSDB | `{"fightForecast": 2, "forecastPlans": 1, "threatSpend": 2, "tour": 1, "breakout": 3}` | 12 | 7 | 1 | 0.895 ± 0.162 | +0.302 ± 0.294 | 0.847 ± 0.163 | +0.333 ± 0.254 | 12 |
| nFRKB | `{"fightForecast": 2, "forecastPlans": 1, "secondArmy": 1, "secondArmyEdge": 0.6, "kitePlan": 1, "latePlan": 1, "breakout": 3}` | 12 | 3 | 8 | 0.271 ± 0.252 | -0.321 ± 0.413 | 0.264 ± 0.253 | -0.250 ± 0.399 | 12 |
| nFRDQ | `{"fightForecast": 2, "forecastPlans": 1, "secondArmy": 1, "secondArmyEdge": 0.6, "tour": 1, "skillChoice": 1, "primaryByArmy": 1}` | 12 | 7 | 5 | 0.583 ± 0.291 | -0.009 ± 0.370 | 0.583 ± 0.291 | +0.069 ± 0.361 | 12 |
| nFRSA | `{"fightForecast": 2, "forecastPlans": 1, "secondArmy": 1, "secondArmyEdge": 0.6, "threatSpend": 2, "artifactCare": 1}` | 12 | 4 | 5 | 0.541 ± 0.279 | -0.051 ± 0.247 | 0.514 ± 0.268 | +0.000 ± 0.205 | 12 |
| nFRSDKBAQ | `{"fightForecast": 2, "forecastPlans": 1, "secondArmy": 1, "secondArmyEdge": 0.6, "threatSpend": 2, "tour": 1, "kitePlan": 1, "latePlan": 1, "breakout": 3, "artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1}` | 11 | 7 | 3 | 0.648 ± 0.290 | +0.093 ± 0.303 | 0.652 ± 0.287 | +0.182 ± 0.247 | 11 |
