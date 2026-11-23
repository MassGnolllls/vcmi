# A/B test (16 configs, seeds 67)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 10 | 1 | 0.904 ± 0.163 | - | 0.893 ± 0.166 | - | 12 |
| nXYUF | `{"scouts": 2, "supplyRuns": 1, "surrender": 2, "fightForecast": 1, "forecastPlans": 1}` | 12 | 11 | 1 | 0.917 ± 0.163 | +0.012 ± 0.024 | 0.917 ± 0.163 | +0.024 ± 0.047 | 12 |
| nKYUD | `{"kitePlan": 1, "latePlan": 1, "supplyRuns": 1, "surrender": 2, "tour": 1}` | 12 | 10 | 2 | 0.833 ± 0.220 | -0.071 ± 0.167 | 0.833 ± 0.220 | -0.060 ± 0.174 | 12 |
| nKXDF | `{"kitePlan": 1, "latePlan": 1, "scouts": 2, "tour": 1, "fightForecast": 1, "forecastPlans": 1}` | 12 | 11 | 1 | 0.917 ± 0.163 | +0.012 ± 0.242 | 0.917 ± 0.163 | +0.024 ± 0.246 | 12 |
| nQYDF | `{"skillChoice": 1, "primaryByArmy": 1, "supplyRuns": 1, "tour": 1, "fightForecast": 1, "forecastPlans": 1}` | 11 | 10 | 0 | 0.983 ± 0.034 | +0.087 ± 0.179 | 0.948 ± 0.102 | +0.065 ± 0.190 | 11 |
| nQXUD | `{"skillChoice": 1, "primaryByArmy": 1, "scouts": 2, "surrender": 2, "tour": 1}` | 12 | 8 | 4 | 0.667 ± 0.279 | -0.238 ± 0.261 | 0.667 ± 0.279 | -0.226 ± 0.268 | 12 |
| nQKUF | `{"skillChoice": 1, "primaryByArmy": 1, "kitePlan": 1, "latePlan": 1, "surrender": 2, "fightForecast": 1, "forecastPlans": 1}` | 12 | 11 | 1 | 0.917 ± 0.163 | +0.012 ± 0.024 | 0.917 ± 0.163 | +0.024 ± 0.047 | 12 |
| nQKXY | `{"skillChoice": 1, "primaryByArmy": 1, "kitePlan": 1, "latePlan": 1, "scouts": 2, "supplyRuns": 1}` | 11 | 11 | 0 | 1.000 ± 0.000 | +0.091 ± 0.178 | 1.000 ± 0.000 | +0.091 ± 0.178 | 11 |
| nAUDF | `{"artifactCare": 1, "surrender": 2, "tour": 1, "fightForecast": 1, "forecastPlans": 1}` | 12 | 9 | 2 | 0.815 ± 0.218 | -0.089 ± 0.163 | 0.786 ± 0.227 | -0.107 ± 0.166 | 12 |
| nAXYD | `{"artifactCare": 1, "scouts": 2, "supplyRuns": 1, "tour": 1}` | 12 | 10 | 2 | 0.833 ± 0.220 | -0.071 ± 0.294 | 0.833 ± 0.220 | -0.060 ± 0.297 | 12 |
| nAKYF | `{"artifactCare": 1, "kitePlan": 1, "latePlan": 1, "supplyRuns": 1, "fightForecast": 1, "forecastPlans": 1}` | 12 | 10 | 2 | 0.833 ± 0.220 | -0.071 ± 0.167 | 0.833 ± 0.220 | -0.060 ± 0.174 | 12 |
| nAKXU | `{"artifactCare": 1, "kitePlan": 1, "latePlan": 1, "scouts": 2, "surrender": 2}` | 12 | 11 | 1 | 0.917 ± 0.163 | +0.012 ± 0.024 | 0.917 ± 0.163 | +0.024 ± 0.047 | 12 |
| nAQYU | `{"artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1, "supplyRuns": 1, "surrender": 2}` | 12 | 10 | 2 | 0.833 ± 0.220 | -0.071 ± 0.294 | 0.833 ± 0.220 | -0.060 ± 0.297 | 12 |
| nAQXF | `{"artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1, "scouts": 2, "fightForecast": 1, "forecastPlans": 1}` | 12 | 11 | 1 | 0.917 ± 0.163 | +0.012 ± 0.024 | 0.917 ± 0.163 | +0.024 ± 0.047 | 12 |
| nAQKD | `{"artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1, "kitePlan": 1, "latePlan": 1, "tour": 1}` | 12 | 8 | 4 | 0.667 ± 0.279 | -0.238 ± 0.356 | 0.667 ± 0.279 | -0.226 ± 0.361 | 12 |
| nAQKXYUDF | `{"artifactCare": 1, "skillChoice": 1, "primaryByArmy": 1, "kitePlan": 1, "latePlan": 1, "scouts": 2, "supplyRuns": 1, "surrender": 2, "tour": 1, "fightForecast": 1, "forecastPlans": 1}` | 12 | 11 | 1 | 0.917 ± 0.163 | +0.012 ± 0.242 | 0.917 ± 0.163 | +0.024 ± 0.246 | 12 |
