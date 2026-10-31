#pragma once

class UCanvas;
class UFont;
class APlayerController;
class AVerdantWorld;
class AVerdantCharacter;

namespace VerdantSpeechBubbles
{
    void Draw(UCanvas* Canvas, APlayerController* PC, UFont* Font,
              const AVerdantWorld* World, const AVerdantCharacter* Player);
}
