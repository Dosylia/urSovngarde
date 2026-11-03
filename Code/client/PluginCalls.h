#pragma once

// Runs the calls other plugins made since the last frame, on the main thread.
void RunPluginCalls() noexcept;

// The server's answer on a plugin: the name of a party member without it, or empty when everyone has it.
void StorePartyPluginAnswer(const String& acPluginFile, const String& acPlayerWithout) noexcept;

// The party changed, so what the server said about it no longer holds.
void ForgetPartyPluginAnswers() noexcept;

// A number the party's leader set for the party.
void StorePartyValue(const String& acKey, float aValue) noexcept;

// This game left its party, or joined another, so the leader's numbers no longer hold.
void ForgetPartyValues() noexcept;
