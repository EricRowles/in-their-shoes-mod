# Dialogue

While impersonating, the **player** voices every line the donor would say, and talking to the **puppet** shows the donor's dialogue. This is `DialogueManager` (routing, self-dialogue) and `PlayerVoice` (playing the lines), with idle lines picked by `IdleChatter`.

## Who counts as whom

| Situation | Player tests as | Puppet tests as |
|---|---|---|
| Normal play | Donor | Dragonborn |
| Self-dialogue (talking to the puppet) | Dragonborn (the listener) | Donor (the speaker) |

This includes race and sex: in self-dialogue the donor's lines see the Dragonborn's race and sex as the listener's. The parked donor always counts as being where the player is. The player and puppet both use the donor's voice type, so lines resolve to the donor's voice files. With NPC voice off in the MCM, the player keeps the Dragonborn's voice type. Conditions never count the player as being in a scene, because it only joins scenes in the donor's place.

## Self-dialogue

Opens when the player activates the puppet, or when a donor package that would greet the Dragonborn reaches the puppet during idle AI. The menu shows the donor's topics.

## Speech

- **Self-dialogue lines:** the puppet runs the line unheard, without moving its mouth or gesturing, so the game paces the conversation as it does for any NPC, while the player voices it.
- **Lines scripts give the donor** (e.g. a follower's reply to another follower) are spoken by the player when the MCM's script redirect is on.
- **Scene lines** the player is given through the donor's aliases are voiced by the player. Idle AI keeps running during scenes and runs the scene's package actions for the player. While idle AI moves the player, the player's scene lines wait until it stops.
- **Skipping and ending a line:** the game handles these as it does for an NPC's line, and the player's voice stops when the puppet's line does.
- Outside self-dialogue the puppet says nothing.

## Idle chatter

When the cooldown (MCM) has passed and the player is free and not in a scene, an idle line the donor could say is picked from running quests as the engine picks one: the highest priority quest with a line whose conditions pass, the first such line in its topic (or one at random from a random group), skipping say-once lines already said and lines whose reset hours haven't passed. The player voices each of its responses. The line's fragments run with the donor as the speaker, so it can start a scene (e.g. another follower's reply).

## Follower recruitment

Dialogue fragments act on the speaker, which in self-dialogue is the puppet. So with the MCM's script redirect on, during self-dialogue and just after it closes, what a fragment does to the puppet goes to the player (items, AI, factions, killing it), except teammate status and relationships, which go to the donor. With it off, those changes stay on the puppet. The player holds the donor's factions for the session, so the donor gets them on revert. Any quest alias the puppet is put into (e.g. a follower alias) moves to the player and counts for the donor's conditions. If a quest later clears the alias (e.g. dismissal), the donor loses it; otherwise the donor gets it on revert like its other aliases.
