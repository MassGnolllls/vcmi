# A/B test (16 configs, seeds 62)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{"magicWorth": 2}` | 24 | 16 | 8 | 0.667 ± 0.193 | - | 0.667 ± 0.193 | - | 24 |
| nWURP | `{"magicWorth": 2, "fightForecast": 1, "muster": 1, "supplyDays": 8, "secondArmy": 1, "secondArmyEdge": 0.6, "passOwn": 1}` | 24 | 21 | 3 | 0.875 ± 0.135 | +0.208 ± 0.235 | 0.875 ± 0.135 | +0.208 ± 0.235 | 24 |
| nLURI | `{"magicWorth": 2, "battlePlan": 3, "muster": 1, "supplyDays": 8, "secondArmy": 1, "secondArmyEdge": 0.6, "idleBuild": 1, "spareStock": 1, "magicFocus": 2}` | 24 | 19 | 5 | 0.792 ± 0.166 | +0.125 ± 0.215 | 0.792 ± 0.166 | +0.125 ± 0.215 | 24 |
| nLWIP | `{"magicWorth": 2, "battlePlan": 3, "fightForecast": 1, "idleBuild": 1, "spareStock": 1, "magicFocus": 2, "passOwn": 1}` | 24 | 20 | 4 | 0.833 ± 0.152 | +0.167 ± 0.226 | 0.833 ± 0.152 | +0.167 ± 0.226 | 24 |
| nDUIP | `{"magicWorth": 2, "towerGuard": 1, "threatSpend": 1, "muster": 1, "supplyDays": 8, "idleBuild": 1, "spareStock": 1, "magicFocus": 2, "passOwn": 1}` | 24 | 20 | 4 | 0.833 ± 0.152 | +0.167 ± 0.255 | 0.833 ± 0.152 | +0.167 ± 0.255 | 24 |
| nDWRI | `{"magicWorth": 2, "towerGuard": 1, "threatSpend": 1, "fightForecast": 1, "secondArmy": 1, "secondArmyEdge": 0.6, "idleBuild": 1, "spareStock": 1, "magicFocus": 2}` | 24 | 17 | 7 | 0.708 ± 0.186 | +0.042 ± 0.250 | 0.708 ± 0.186 | +0.042 ± 0.250 | 24 |
| nDLRP | `{"magicWorth": 2, "towerGuard": 1, "threatSpend": 1, "battlePlan": 3, "secondArmy": 1, "secondArmyEdge": 0.6, "passOwn": 1}` | 24 | 19 | 5 | 0.792 ± 0.166 | +0.125 ± 0.215 | 0.792 ± 0.166 | +0.125 ± 0.215 | 24 |
| nDLWU | `{"magicWorth": 2, "towerGuard": 1, "threatSpend": 1, "battlePlan": 3, "fightForecast": 1, "muster": 1, "supplyDays": 8}` | 24 | 17 | 7 | 0.708 ± 0.186 | +0.042 ± 0.276 | 0.708 ± 0.186 | +0.042 ± 0.276 | 24 |
| nCRIP | `{"magicWorth": 2, "saveFor": 1, "secondArmy": 1, "secondArmyEdge": 0.6, "idleBuild": 1, "spareStock": 1, "magicFocus": 2, "passOwn": 1}` | 24 | 18 | 6 | 0.750 ± 0.177 | +0.083 ± 0.201 | 0.750 ± 0.177 | +0.083 ± 0.201 | 24 |
| nCWUI | `{"magicWorth": 2, "saveFor": 1, "fightForecast": 1, "muster": 1, "supplyDays": 8, "idleBuild": 1, "spareStock": 1, "magicFocus": 2}` | 24 | 17 | 7 | 0.708 ± 0.186 | +0.042 ± 0.276 | 0.708 ± 0.186 | +0.042 ± 0.276 | 24 |
| nCLUP | `{"magicWorth": 2, "saveFor": 1, "battlePlan": 3, "muster": 1, "supplyDays": 8, "passOwn": 1}` | 24 | 15 | 9 | 0.625 ± 0.198 | -0.042 ± 0.300 | 0.625 ± 0.198 | -0.042 ± 0.300 | 24 |
| nCLWR | `{"magicWorth": 2, "saveFor": 1, "battlePlan": 3, "fightForecast": 1, "secondArmy": 1, "secondArmyEdge": 0.6}` | 24 | 19 | 5 | 0.792 ± 0.166 | +0.125 ± 0.215 | 0.792 ± 0.166 | +0.125 ± 0.215 | 24 |
| nCDUR | `{"magicWorth": 2, "saveFor": 1, "towerGuard": 1, "threatSpend": 1, "muster": 1, "supplyDays": 8, "secondArmy": 1, "secondArmyEdge": 0.6}` | 24 | 16 | 8 | 0.667 ± 0.193 | +0.000 ± 0.236 | 0.667 ± 0.193 | +0.000 ± 0.236 | 24 |
| nCDWP | `{"magicWorth": 2, "saveFor": 1, "towerGuard": 1, "threatSpend": 1, "fightForecast": 1, "passOwn": 1}` | 24 | 17 | 7 | 0.708 ± 0.186 | +0.042 ± 0.220 | 0.708 ± 0.186 | +0.042 ± 0.220 | 24 |
| nCDLI | `{"magicWorth": 2, "saveFor": 1, "towerGuard": 1, "threatSpend": 1, "battlePlan": 3, "idleBuild": 1, "spareStock": 1, "magicFocus": 2}` | 24 | 17 | 7 | 0.708 ± 0.186 | +0.042 ± 0.186 | 0.708 ± 0.186 | +0.042 ± 0.186 | 24 |
| nCDLWURIP | `{"magicWorth": 2, "saveFor": 1, "towerGuard": 1, "threatSpend": 1, "battlePlan": 3, "fightForecast": 1, "muster": 1, "supplyDays": 8, "secondArmy": 1, "secondArmyEdge": 0.6, "idleBuild": 1, "spareStock": 1, "magicFocus": 2, "passOwn": 1}` | 23 | 15 | 8 | 0.652 ± 0.199 | -0.043 ± 0.288 | 0.652 ± 0.199 | -0.043 ± 0.288 | 23 |
