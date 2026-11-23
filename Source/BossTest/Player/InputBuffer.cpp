#include "Player/InputBuffer.h"

void FInputBuffer::Press(EPlayerInput Input)
{
	Pending = Input;
	Age = 0.f;
}

void FInputBuffer::Tick(float Dt)
{
	if (Pending == EPlayerInput::None)
	{
		return;
	}
	Age += Dt;
	if (Age > Window)
	{
		Consume();
	}
}

void FInputBuffer::Consume()
{
	Pending = EPlayerInput::None;
	Age = 0.f;
}
