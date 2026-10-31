#include "VerdantWorld.h"
#include "VerdantFoodPickup.h"
#include "VerdantCharacter.h"
#include "VerdantEconomyRules.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

void AVerdantWorld::SpawnFoodField()
{
    FRandomStream Random(14663);
    const FVector Start(-1000,-5800,0);
    for (int32 I=0; I<160; ++I)
    {
        const bool Starter = I<24;
        const int32 Grove = !Starter && I%3==0 ? (I/3)%Groves.Num() : INDEX_NONE;
        const FVector Center = Starter ? Start : Grove!=INDEX_NONE ? Groves[Grove].Position : Start+FVector(0,5200,0);
        const int32 Roll = Random.RandRange(0,99);
        int32 Tier = Starter ? 1 : Roll<45 ? 1 : Roll<72 ? 2 : Roll<90 ? 3 : 4;
        if (Grove!=INDEX_NONE)
            if (auto* GroveOwner=FindFolk(Groves[Grove].ControllerId)) Tier=FMath::Min(Tier,GroveOwner->FoodTier());
        FVector At;
        for (int32 Attempt=0;Attempt<24;++Attempt)
        {
            const float Angle=Random.FRandRange(0,2*PI);
            const float Radius=Starter ? Random.FRandRange(110.f,1100.f) : Grove!=INDEX_NONE ? Random.FRandRange(150.f,580.f) : Random.FRandRange(1000.f,8400.f);
            At=Center+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0);
            const float H=HeightAt(At.X,At.Y);
            if (FMath::Abs(HeightAt(At.X+70,At.Y)-H)<55 && FMath::Abs(HeightAt(At.X,At.Y+70)-H)<55) break;
        }
        At=SurfacePoint(At, Tier==1?6.f:Tier==2?13.f:Tier==3?27.f:48.f);
        auto* Pickup=GetWorld()->SpawnActor<AVerdantFoodPickup>(At,FRotator::ZeroRotator);
        if (!Pickup) continue;
        Pickup->Configure(this,Tier,VerdantEconomy::Amount(Tier),true,Grove);
        FoodPickups.Add(Pickup);
    }
    TickFoodField();
}

void AVerdantWorld::TickFoodField()
{
    FoodPickups.RemoveAll([](const TObjectPtr<AVerdantFoodPickup>& P){return !IsValid(P);});
    for (auto& G:Groves) G.Food=0.f;
    for (AVerdantFoodPickup* P : FoodPickups)
    {
        if (!IsValid(P)) continue;
        if (P->bRenewable && P->Remaining<=.001f && P->RefillAt>=0 && Elapsed>=P->RefillAt)
        {
            // Exhausted objects replenish after a delay; partial piles never refill.
            P->Remaining=P->InitialAmount;
            P->RefillAt=-1.f;
            P->RefreshVisual();
        }
        if (Groves.IsValidIndex(P->GroveIndex)) Groves[P->GroveIndex].Food+=P->Remaining;
    }
}

AVerdantFoodPickup* AVerdantWorld::FindFood(const AVerdantCharacter* Who,float MaxDistance) const
{
    if (!IsValid(Who)||Who->bDead||Who->FreeFoodCapacity()<=.001f) return nullptr;
    AVerdantFoodPickup* Best=nullptr;float Distance=MaxDistance*MaxDistance;
    for (AVerdantFoodPickup* P : FoodPickups)
    {
        if (!IsValid(P)||P->Remaining<=.001f||Who->FoodTier()<P->FoodTier) continue;
        if (P->GroveIndex!=INDEX_NONE&&!CanHarvest(P->GroveIndex,Who)) continue;
        const float D=FVector::DistSquared(Who->GetActorLocation(),P->GetActorLocation());
        if (D<Distance){Distance=D;Best=P;}
    }
    return Best;
}

float AVerdantWorld::CollectFood(AVerdantCharacter* Who,float Radius)
{
    if (!IsValid(Who)||Who->bDead) return 0.f;
    const float SearchRadius=FMath::Max(Radius,Who->Reach()+130.f);
    if (auto* Eligible=FindFood(Who,SearchRadius))
    {
        const float Taken=Eligible->Take(Who);
        if(Taken>.001f)return Taken;
    }
    AVerdantFoodPickup* Best=nullptr;float Distance=FMath::Square(SearchRadius);
    for (AVerdantFoodPickup* P : FoodPickups)
    {
        if(!IsValid(P)||P->Remaining<=.001f)continue;
        const float D=FVector::DistSquared(Who->GetActorLocation(),P->GetActorLocation());
        if(D<Distance){Distance=D;Best=P;}
    }
    if (!Best){if(Who->AgentId==0)Notify(TEXT("靠近散落的彩色食物按住 E；体型越大可获取的食物档位越高。"));return 0.f;}
    if (Who->FoodTier()<Best->FoodTier)
    {
        if(Who->AgentId==0)Notify(FString::Printf(TEXT("第%d档食物需要身高 %.2f 米；当前 %.2f 米。"),Best->FoodTier,VerdantEconomy::MinimumHeight(Best->FoodTier)/100,Who->HeightCm()/100));
        return 0.f;
    }
    if(Who->FreeFoodCapacity()<=.001f){if(Who->AgentId==0)Notify(TEXT("食物已满，剩余仍留在原地；每10秒自动吸收成长。"));return 0.f;}
    if(Best->GroveIndex!=INDEX_NONE&&!CanHarvest(Best->GroveIndex,Who)){if(Who->AgentId==0)Notify(TEXT("这处果林尚未允许采集，可以与管理者商量。"));return 0.f;}
    return Best->Take(Who);
}

float AVerdantWorld::LootCorpse(AVerdantCharacter* Corpse,AVerdantCharacter* Receiver,float Amount)
{
    if(!IsValid(Corpse)||!Corpse->bDead||Corpse->AgentId==0||!IsValid(Receiver)||Receiver->bDead||Corpse==Receiver) return 0.f;
    if(FVector::DistSquared(Corpse->GetActorLocation(),Receiver->GetActorLocation())>FMath::Square(FMath::Max(220.f,Receiver->Reach()+130.f))) return 0.f;
    const float Paid=VerdantEconomy::TransferAmount(Corpse->Food,Receiver->FreeFoodCapacity(),Amount,Receiver->FoodTier(),Corpse->FoodTier());
    if(Paid<=.001f)
    {
        if(Receiver->AgentId==0)Notify(Receiver->FoodTier()<Corpse->FoodTier()?FString::Printf(TEXT("这是第%d档掉落，达到对应体型后才能获取。"),Corpse->FoodTier()):TEXT("食物已满，未取走的部分仍留在这里。"));
        return 0.f;
    }
    Corpse->Food-=Paid;Receiver->Food+=Paid;
    Record(FString::Printf(TEXT("%s 取走%s的第%d档食物 %.1f 份，剩余 %.1f。"),*Receiver->DisplayName,*Corpse->DisplayName,Corpse->FoodTier(),Paid,Corpse->Food));
    return Paid;
}

AVerdantFoodPickup* AVerdantWorld::SpawnFoodDrop(const FVector& Position,float Amount,int32 Tier)
{
    if(!FMath::IsFinite(Amount)||Amount<=.001f)return nullptr;
    auto* P=GetWorld()->SpawnActor<AVerdantFoodPickup>(SurfacePoint(Position,20.f),FRotator::ZeroRotator);
    if(P){P->Configure(this,Tier,Amount,false);FoodPickups.Add(P);}
    return P;
}
