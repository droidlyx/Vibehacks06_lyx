#pragma once

class AVerdantCharacter;

// A deliberately simple seed creature. The actor retains ownership of collision,
// growth, movement and all combat decisions; these functions only present them.
namespace VerdantCreaturePresentation
{
    void BuildHero(AVerdantCharacter* Character);
    void PaintHero(AVerdantCharacter* Character);
    void UpdateHero(AVerdantCharacter* Character);
}
