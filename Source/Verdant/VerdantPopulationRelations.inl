// Reviewed allowed type pairs; unimplemented relation types are not sampled.
static const FRelationRule RelationRules[] = {
    {TEXT("R01"), TEXT("旧友"), TEXT("*"), TEXT("*"), true, 6.0f, TEXT("R24"), 3, 3},
    {TEXT("R03"), TEXT("患难同伴"), TEXT("*"), TEXT("*"), true, 2.0f, TEXT("R24"), 2, 2},
    {TEXT("R07"), TEXT("无偿保护"), TEXT("T02,T12,T18,T20,T40,T46,T48,T49"), TEXT("*"), false, 6.0f, TEXT("R10,R23,R45"), 1, 2},
    {TEXT("R10"), TEXT("勒索支配"), TEXT("T04,T13,T28,T29"), TEXT("*"), false, 1.0f, TEXT("R07,R08,R09"), 1, 2},
    {TEXT("R18"), TEXT("同地采食竞争"), TEXT("*"), TEXT("*"), true, 6.0f, TEXT(""), 1, 2},
    {TEXT("R19"), TEXT("声望对手"), TEXT("T01,T10,T12,T17,T22,T23,T28,T33,T42,T43,T50"), TEXT("T01,T10,T12,T17,T22,T23,T28,T33,T42,T43,T50"), true, 2.0f, TEXT(""), 1, 2},
    {TEXT("R20"), TEXT("共同供奉"), TEXT("T03,T08,T20,T30,T38,T40,T45,T49,T51"), TEXT("T03,T08,T20,T30,T38,T40,T45,T49,T51"), true, 3.0f, TEXT("R17,R21"), 1, 2},
    {TEXT("R21"), TEXT("供奉立场分歧"), TEXT("T03,T08,T20,T30,T38,T40,T45,T49,T51"), TEXT("T07,T09,T15,T16,T24,T26,T27,T32,T36,T37,T47"), false, 2.0f, TEXT("R20"), 1, 2},
    {TEXT("R32"), TEXT("越权代言与真实供货"), TEXT("T17"), TEXT("T37,T27"), false, 1.0f, TEXT(""), 1, 2},
    {TEXT("R34"), TEXT("宣讲主张与事实核验"), TEXT("T30"), TEXT("T26"), false, 1.0f, TEXT(""), 1, 2},
    {TEXT("R37"), TEXT("讽刺监督与体面权威"), TEXT("T52"), TEXT("T33"), false, 1.0f, TEXT(""), 1, 2},
};
