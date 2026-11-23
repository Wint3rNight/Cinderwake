// The player's input buffer (spec section 4): one slot, the latest press wins, and a press
// lasts Window seconds on the player's own clock.
#pragma once

#include "CoreMinimal.h"

enum class EPlayerInput : uint8 { None, Light, Heavy, Dodge, Heal };

struct FInputBuffer
{
	float Window = 0.18f;

	void Press(EPlayerInput Input);   // the latest press replaces an older one
	void Tick(float Dt);              // a press older than Window expires
	EPlayerInput Peek() const { return Pending; }
	void Consume();
	void Clear() { Consume(); }

private:
	EPlayerInput Pending = EPlayerInput::None;
	float Age = 0.f;
};
