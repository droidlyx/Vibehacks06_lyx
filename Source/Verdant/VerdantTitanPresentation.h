#pragma once
class AVerdantCharacter;

namespace VerdantTitanPresentation
{
	bool Load(AVerdantCharacter* Character);
	void Update(AVerdantCharacter* Character);
	void Paint(AVerdantCharacter* Character);
	float BaseHeight(const AVerdantCharacter* Character);
}
