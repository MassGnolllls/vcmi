# A/B test (16 configs, seeds 66)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 4 | 6 | 0.429 ± 0.283 | - | 0.404 ± 0.277 | - | 12 |
| nDKBQ | `{"tour": 1, "kitePlan": 1, "latePlan": 1, "breakout": 3, "skillChoice": 1, "primaryByArmy": 1}` | 12 | 6 | 5 | 0.542 ± 0.282 | +0.113 ± 0.407 | 0.519 ± 0.286 | +0.115 ± 0.414 | 12 |
| nSKBA | `{"threatSpend": 2, "kitePlan": 1, "latePlan": 1, "breakout": 3, "artifactCare": 1}` | 12 | 7 | 5 | 0.583 ± 0.291 | +0.154 ± 0.314 | 0.583 ± 0.291 | +0.179 ± 0.320 | 12 |
| nSDAQ | `{"threatSpend": 2, "tour": 1, "artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1}` | 11 | 6 | 5 | 0.545 ± 0.309 | +0.094 ± 0.414 | 0.545 ± 0.309 | +0.112 ± 0.414 | 11 |
| nRKAQ | `{"secondArmy": 1, "secondArmyEdge": 0.6, "kitePlan": 1, "latePlan": 1, "artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1}` | 12 | 11 | 1 | 0.917 ± 0.163 | +0.488 ± 0.304 | 0.917 ± 0.163 | +0.513 ± 0.291 | 12 |
| nRDBA | `{"secondArmy": 1, "secondArmyEdge": 0.6, "tour": 1, "breakout": 3, "artifactCare": 1}` | 12 | 7 | 5 | 0.583 ± 0.291 | +0.154 ± 0.330 | 0.583 ± 0.291 | +0.179 ± 0.327 | 12 |
| nRSBQ | `{"secondArmy": 1, "secondArmyEdge": 0.6, "threatSpend": 2, "breakout": 3, "skillChoice": 1, "primaryByArmy": 1}` | 12 | 6 | 5 | 0.558 ± 0.283 | +0.129 ± 0.293 | 0.538 ± 0.282 | +0.135 ± 0.291 | 12 |
| nRSDK | `{"secondArmy": 1, "secondArmyEdge": 0.6, "threatSpend": 2, "tour": 1, "kitePlan": 1, "latePlan": 1}` | 12 | 6 | 6 | 0.500 ± 0.295 | +0.071 ± 0.436 | 0.500 ± 0.295 | +0.096 ± 0.429 | 12 |
| nFBAQ | `{"fightForecast": 2, "forecastPlans": 1, "breakout": 3, "artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1}` | 12 | 6 | 6 | 0.500 ± 0.295 | +0.071 ± 0.366 | 0.500 ± 0.295 | +0.096 ± 0.374 | 12 |
| nFDKA | `{"fightForecast": 2, "forecastPlans": 1, "tour": 1, "kitePlan": 1, "latePlan": 1, "artifactCare": 1}` | 12 | 7 | 5 | 0.583 ± 0.291 | +0.154 ± 0.314 | 0.583 ± 0.291 | +0.179 ± 0.320 | 12 |
| nFSKQ | `{"fightForecast": 2, "forecastPlans": 1, "threatSpend": 2, "kitePlan": 1, "latePlan": 1, "skillChoice": 1, "primaryByArmy": 1}` | 12 | 7 | 5 | 0.583 ± 0.291 | +0.154 ± 0.461 | 0.583 ± 0.291 | +0.179 ± 0.453 | 12 |
| nFSDB | `{"fightForecast": 2, "forecastPlans": 1, "threatSpend": 2, "tour": 1, "breakout": 3}` | 11 | 4 | 6 | 0.447 ± 0.304 | +0.070 ± 0.395 | 0.434 ± 0.297 | +0.084 ± 0.393 | 11 |
| nFRKB | `{"fightForecast": 2, "forecastPlans": 1, "secondArmy": 1, "secondArmyEdge": 0.6, "kitePlan": 1, "latePlan": 1, "breakout": 3}` | 12 | 4 | 8 | 0.333 ± 0.279 | -0.096 ± 0.361 | 0.333 ± 0.279 | -0.071 ± 0.357 | 12 |
| nFRDQ | `{"fightForecast": 2, "forecastPlans": 1, "secondArmy": 1, "secondArmyEdge": 0.6, "tour": 1, "skillChoice": 1, "primaryByArmy": 1}` | 12 | 3 | 9 | 0.250 ± 0.256 | -0.179 ± 0.403 | 0.250 ± 0.256 | -0.154 ± 0.393 | 12 |
| nFRSA | `{"fightForecast": 2, "forecastPlans": 1, "secondArmy": 1, "secondArmyEdge": 0.6, "threatSpend": 2, "artifactCare": 1}` | 11 | 3 | 8 | 0.273 ± 0.276 | -0.178 ± 0.353 | 0.273 ± 0.276 | -0.161 ± 0.340 | 11 |
| nFRSDKBAQ | `{"fightForecast": 2, "forecastPlans": 1, "secondArmy": 1, "secondArmyEdge": 0.6, "threatSpend": 2, "tour": 1, "kitePlan": 1, "latePlan": 1, "breakout": 3, "artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1}` | 12 | 5 | 6 | 0.425 ± 0.288 | -0.004 ± 0.244 | 0.423 ± 0.288 | +0.019 ± 0.245 | 12 |
