# A/B test (16 configs, seeds 67)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 4 | 7 | 0.366 ± 0.272 | - | 0.345 ± 0.275 | - | 12 |
| nXYUF | `{"scouts": 2, "supplyRuns": 1, "surrender": 2, "fightForecast": 1, "forecastPlans": 1}` | 11 | 5 | 4 | 0.490 ± 0.292 | +0.181 ± 0.453 | 0.481 ± 0.296 | +0.195 ± 0.444 | 11 |
| nKYUD | `{"kitePlan": 1, "latePlan": 1, "supplyRuns": 1, "surrender": 2, "tour": 1}` | 12 | 5 | 5 | 0.513 ± 0.283 | +0.147 ± 0.339 | 0.500 ± 0.283 | +0.155 ± 0.331 | 12 |
| nKXDF | `{"kitePlan": 1, "latePlan": 1, "scouts": 2, "tour": 1, "fightForecast": 1, "forecastPlans": 1}` | 11 | 9 | 1 | 0.907 ± 0.178 | +0.544 ± 0.310 | 0.896 ± 0.177 | +0.532 ± 0.318 | 11 |
| nQYDF | `{"skillChoice": 1, "primaryByArmy": 1, "supplyRuns": 1, "tour": 1, "fightForecast": 1, "forecastPlans": 1}` | 12 | 6 | 5 | 0.516 ± 0.288 | +0.150 ± 0.387 | 0.512 ± 0.289 | +0.167 ± 0.397 | 12 |
| nQXUD | `{"skillChoice": 1, "primaryByArmy": 1, "scouts": 2, "surrender": 2, "tour": 1}` | 11 | 8 | 2 | 0.772 ± 0.243 | +0.372 ± 0.378 | 0.740 ± 0.264 | +0.364 ± 0.388 | 11 |
| nQKUF | `{"skillChoice": 1, "primaryByArmy": 1, "kitePlan": 1, "latePlan": 1, "surrender": 2, "fightForecast": 1, "forecastPlans": 1}` | 9 | 6 | 3 | 0.667 ± 0.327 | +0.222 ± 0.436 | 0.667 ± 0.327 | +0.222 ± 0.436 | 9 |
| nQKXY | `{"skillChoice": 1, "primaryByArmy": 1, "kitePlan": 1, "latePlan": 1, "scouts": 2, "supplyRuns": 1}` | 12 | 7 | 4 | 0.602 ± 0.281 | +0.235 ± 0.409 | 0.595 ± 0.284 | +0.250 ± 0.418 | 12 |
| nAUDF | `{"artifactCare": 1, "surrender": 2, "tour": 1, "fightForecast": 1, "forecastPlans": 1}` | 10 | 3 | 6 | 0.347 ± 0.293 | -0.092 ± 0.317 | 0.314 ± 0.295 | -0.100 ± 0.337 | 10 |
| nAXYD | `{"artifactCare": 1, "scouts": 2, "supplyRuns": 1, "tour": 1}` | 12 | 8 | 3 | 0.750 ± 0.256 | +0.384 ± 0.275 | 0.738 ± 0.253 | +0.393 ± 0.291 | 12 |
| nAKYF | `{"artifactCare": 1, "kitePlan": 1, "latePlan": 1, "supplyRuns": 1, "fightForecast": 1, "forecastPlans": 1}` | 12 | 8 | 4 | 0.667 ± 0.279 | +0.300 ± 0.299 | 0.667 ± 0.279 | +0.321 ± 0.284 | 12 |
| nAKXU | `{"artifactCare": 1, "kitePlan": 1, "latePlan": 1, "scouts": 2, "surrender": 2}` | 12 | 11 | 1 | 0.917 ± 0.163 | +0.550 ± 0.371 | 0.917 ± 0.163 | +0.571 ± 0.374 | 12 |
| nAQYU | `{"artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1, "supplyRuns": 1, "surrender": 2}` | 10 | 6 | 4 | 0.600 ± 0.320 | +0.260 ± 0.325 | 0.600 ± 0.320 | +0.286 ± 0.307 | 10 |
| nAQXF | `{"artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1, "scouts": 2, "fightForecast": 1, "forecastPlans": 1}` | 12 | 9 | 2 | 0.787 ± 0.227 | +0.420 ± 0.266 | 0.774 ± 0.235 | +0.429 ± 0.276 | 12 |
| nAQKD | `{"artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1, "kitePlan": 1, "latePlan": 1, "tour": 1}` | 12 | 7 | 5 | 0.583 ± 0.291 | +0.217 ± 0.411 | 0.583 ± 0.291 | +0.238 ± 0.420 | 12 |
| nAQKXYUDF | `{"artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1, "kitePlan": 1, "latePlan": 1, "scouts": 2, "supplyRuns": 1, "surrender": 2, "tour": 1, "fightForecast": 1, "forecastPlans": 1}` | 12 | 7 | 4 | 0.601 ± 0.281 | +0.235 ± 0.330 | 0.595 ± 0.284 | +0.250 ± 0.341 | 12 |
