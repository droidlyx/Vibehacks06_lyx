// Season records describe observed outcomes, never inferred blame or dialogue promises.
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "Engine/World.h"

namespace
{
	FString Change(bool HasOpening, int32 Before, int32 After)
	{
		return HasOpening
			       ? FString::Printf(TEXT("%d → %d"), Before, After)
			       : FString::Printf(TEXT("%d（无开局记录）"), After);
	}

	FString DeliveredByPerson(const FVerdantSeasonSnapshot& Snapshot)
	{
		TArray<int32> Ids;
		Snapshot.Donations.GetKeys(Ids);
		Ids.Sort();
		TArray<FString> DeliveryParts;
		for (int32 Id : Ids)
		{
			const float Paid = Snapshot.Donations.FindRef(Id);
			if (Paid <= 0.f) continue;
			const FString Name = Id == 0
				                     ? TEXT("你")
				                     : Snapshot.DonorNames.Contains(Id)
				                     ? Snapshot.DonorNames.FindRef(Id)
				                     : FString::Printf(TEXT("角色%d"), Id);
			DeliveryParts.Add(FString::Printf(TEXT("%s %.1f"), *Name, Paid));
		}
		return DeliveryParts.IsEmpty() ? TEXT("无已结算供奉") : FString::Join(DeliveryParts, TEXT(" · "));
	}
}

FVerdantSeasonSnapshot AVerdantWorld::CaptureSeasonSnapshot() const
{
	FVerdantSeasonSnapshot Snapshot;
	Snapshot.bCaptured = true;
	Snapshot.AtSeconds = Elapsed;
	Snapshot.TreeNutrition = TreeNutrition;
	Snapshot.Donations = DonationLedger;
	Snapshot.Tending = TendLedger;
	Snapshot.DonorNames = DonationNames;
	for (AVerdantCharacter* Who : Folk)
	{
		if (!IsValid(Who)) continue;
		FVerdantSeasonPerson Person;
		Person.AgentId = Who->AgentId;
		Person.Name = Who->DisplayName;
		Person.bWild = Who->bWild;
		Person.bAlive = !Who->bDead;
		Snapshot.People.Add(Person);
		if (Who->AgentId == 0) continue;
		if (Who->bWild)
		{
			++Snapshot.WildTotal;
			Snapshot.WildAlive += Person.bAlive ? 1 : 0;
		}
		else
		{
			++Snapshot.ResidentsTotal;
			Snapshot.ResidentsAlive += Person.bAlive ? 1 : 0;
		}
	}
	bool HasTax = false;
	for (const FVerdantGrove& Grove : Groves)
	{
		FVerdantSeasonGrove Entry;
		Entry.Name = Grove.Name;
		const AVerdantCharacter* Controller = FindFolk(Grove.ControllerId);
		const bool ControllerAlive = IsValid(Controller) && !Controller->bDead;
		// Matches actual harvest access even between death and the next economy tick.
		Entry.ControllerId = ControllerAlive ? Controller->AgentId : -1;
		Entry.ControllerName = ControllerAlive ? Controller->DisplayName : TEXT("无人管理");
		Entry.bOpen = Grove.bOpen || !ControllerAlive;
		Entry.Share = ControllerAlive ? FMath::Clamp(Grove.Share, 0.f, .9f) : 0.f;
		Entry.Food = Grove.Food;
		Entry.Fertility = Grove.Fertility;
		Snapshot.Groves.Add(Entry);
		Snapshot.OpenGroves += Entry.bOpen ? 1 : 0;
		Snapshot.FreeGroves += Entry.bOpen && Entry.Share <= .001f ? 1 : 0;
		Snapshot.PlayerGroves += Entry.ControllerId == 0 ? 1 : 0;
		Snapshot.AverageFertility += Entry.Fertility;
		HasTax |= Entry.Share > .001f;
	}
	if (!Snapshot.Groves.IsEmpty()) Snapshot.AverageFertility /= Snapshot.Groves.Num();
	if (Snapshot.ResidentsAlive == 0)
		Snapshot.StateLabel = TEXT("寂静山谷");
	else if (Snapshot.PlayerGroves >= 2)
		Snapshot.StateLabel = TEXT("旅人领地");
	else if (!Snapshot.Groves.IsEmpty() && Snapshot.FreeGroves == Snapshot.Groves.Num() && Snapshot.ResidentsAlive >= 2)
		Snapshot.StateLabel = TEXT("共生山谷");
	else
		Snapshot.StateLabel = HasTax ? TEXT("分立有税") : TEXT("分立山谷");
	return Snapshot;
}

void AVerdantWorld::BeginSeasonRecord()
{
	if (OpeningSeason.bCaptured) return;
	// Called after residents exist but before their first autonomous simulation tick.
	OpeningSeason = CaptureSeasonSnapshot();
}

void AVerdantWorld::FinalizeSeasonRecord()
{
	if (!bRestored || RestoredSeason.bCaptured) return;
	// Capture once. Later policy changes or deaths remain live-world changes and
	// cannot retroactively rewrite the season that restored the Mother Tree.
	RestoredSeason = CaptureSeasonSnapshot();
	const bool HasOpening = OpeningSeason.bCaptured;
	const int32 Duration = FMath::Max(
		0, FMath::FloorToInt(RestoredSeason.AtSeconds - (HasOpening ? OpeningSeason.AtSeconds : 0.f)));
	const FString ResidentChange = Change(HasOpening, OpeningSeason.ResidentsAlive, RestoredSeason.ResidentsAlive);
	const FString WildChange = Change(HasOpening, OpeningSeason.WildAlive, RestoredSeason.WildAlive);
	const FString FreeChange = Change(HasOpening, OpeningSeason.FreeGroves, RestoredSeason.FreeGroves);
	const FString TerritoryChange = Change(HasOpening, OpeningSeason.PlayerGroves, RestoredSeason.PlayerGroves);
	float DeliveredTotal = 0.f;
	for (const auto& Entry : RestoredSeason.Donations) DeliveredTotal += Entry.Value;
	const float PlayerDelivered = RestoredSeason.Donations.FindRef(0);
	SeasonBannerTitle = FString::Printf(TEXT("本季记录 · %02d:%02d"), Duration / 60, Duration % 60);
	const FString StateChange = HasOpening
		                            ? OpeningSeason.StateLabel + TEXT(" → ") + RestoredSeason.StateLabel
		                            : TEXT("复苏时：") + RestoredSeason.StateLabel;
	SeasonBannerBody = FString::Printf(TEXT("%s\n居民 %s · 野生 %s\n免税开放 %s · 你管理 %s\n实际送达：你 %.1f · 其他 %.1f"),
	                                   *StateChange, *ResidentChange, *WildChange, *FreeChange, *TerritoryChange,
	                                   PlayerDelivered, FMath::Max(0.f, DeliveredTotal - PlayerDelivered));
	SeasonCoreSummary = FString::Printf(TEXT("复苏时：%s · %02d:%02d\n居民 %s · 野生 %s\n免税开放 %s · 你管理 %s\n亲自送达：%s"),
	                                    *RestoredSeason.StateLabel, Duration / 60, Duration % 60, *ResidentChange,
	                                    *WildChange,
	                                    *FreeChange, *TerritoryChange, *DeliveredByPerson(RestoredSeason));
	const FString Ecology = FString::Printf(TEXT("\n土地生机 %.0f%% → %.0f%% · 你投入养护 %.1f"),
	                                        OpeningSeason.AverageFertility * 100.f,
	                                        RestoredSeason.AverageFertility * 100.f,
	                                        RestoredSeason.Tending.FindRef(0));
	SeasonBannerBody += Ecology;
	SeasonCoreSummary += Ecology;
	SeasonNoticeUntil = GetWorld()->GetTimeSeconds() + 15.f;
	UE_LOG(LogTemp, Display,
	       TEXT("VERDANT_SEASON_CAPTURED residents=%d wild=%d free=%d/%d player_groves=%d actual_delivered=%.3f"),
	       RestoredSeason.ResidentsAlive, RestoredSeason.WildAlive, RestoredSeason.FreeGroves,
	       RestoredSeason.Groves.Num(), RestoredSeason.PlayerGroves, DeliveredTotal);
}

bool AVerdantWorld::IsSeasonRecordVisible() const
{
	return RestoredSeason.bCaptured && GetWorld() && GetWorld()->GetTimeSeconds() < SeasonNoticeUntil;
}
