# A/B test (16 configs, seeds 57)

Paired against `nB`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| nB | `{"secondArmy": 1}` | 24 | 10 | 5 | 0.596 ± 0.169 | - | 0.560 ± 0.178 | - | 24 |
| nL | `{"garrisonShare": 0.12}` | 24 | 4 | 5 | 0.409 ± 0.140 | -0.187 ± 0.230 | 0.381 ± 0.146 | -0.179 ± 0.231 | 24 |
| nQ | `{"townHeroes": 1}` | 24 | 5 | 5 | 0.493 ± 0.158 | -0.103 ± 0.199 | 0.440 ± 0.161 | -0.119 ± 0.223 | 24 |
| nQLB | `{"townHeroes": 1, "garrisonShare": 0.12, "secondArmy": 1}` | 24 | 10 | 4 | 0.651 ± 0.163 | +0.055 ± 0.256 | 0.565 ± 0.179 | +0.006 ± 0.268 | 24 |
| nC | `{"supplyRuns": 1}` | 24 | 4 | 6 | 0.463 ± 0.158 | -0.133 ± 0.194 | 0.440 ± 0.157 | -0.119 ± 0.198 | 24 |
| nCLB | `{"supplyRuns": 1, "garrisonShare": 0.12, "secondArmy": 1}` | 24 | 6 | 8 | 0.387 ± 0.167 | -0.209 ± 0.242 | 0.333 ± 0.167 | -0.226 ± 0.241 | 24 |
| nCQB | `{"supplyRuns": 1, "townHeroes": 1, "secondArmy": 1}` | 24 | 7 | 5 | 0.520 ± 0.155 | -0.076 ± 0.206 | 0.429 ± 0.169 | -0.131 ± 0.227 | 24 |
| nCQL | `{"supplyRuns": 1, "townHeroes": 1, "garrisonShare": 0.12}` | 24 | 7 | 8 | 0.470 ± 0.169 | -0.127 ± 0.232 | 0.417 ± 0.172 | -0.143 ± 0.241 | 24 |
| nT | `{"townWorth": 1}` | 24 | 8 | 7 | 0.491 ± 0.180 | -0.106 ± 0.266 | 0.464 ± 0.174 | -0.095 ± 0.272 | 24 |
| nTLB | `{"townWorth": 1, "garrisonShare": 0.12, "secondArmy": 1}` | 24 | 7 | 5 | 0.436 ± 0.159 | -0.161 ± 0.239 | 0.435 ± 0.162 | -0.125 ± 0.251 | 24 |
| nTQB | `{"townWorth": 1, "townHeroes": 1, "secondArmy": 1}` | 24 | 9 | 6 | 0.568 ± 0.168 | -0.028 ± 0.194 | 0.536 ± 0.169 | -0.024 ± 0.190 | 24 |
| nTQL | `{"townWorth": 1, "townHeroes": 1, "garrisonShare": 0.12}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nTCB | `{"townWorth": 1, "supplyRuns": 1, "secondArmy": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nTCL | `{"townWorth": 1, "supplyRuns": 1, "garrisonShare": 0.12}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nTCQ | `{"townWorth": 1, "supplyRuns": 1, "townHeroes": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nTCQLB | `{"townWorth": 1, "supplyRuns": 1, "townHeroes": 1, "garrisonShare": 0.12, "secondArmy": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
