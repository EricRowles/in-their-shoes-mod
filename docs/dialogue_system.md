# Dialogue

While impersonating, the **player** voices every line the donor would say, and talking to the **puppet** shows the donor's dialogue. This is `DialogueManager`, with idle lines picked by `IdleChatter`.

## Who counts as whom

| Situation | Player tests as | Puppet tests as |
|---|---|---|
| Normal play | Donor | Dragonborn |
| Self-dialogue (talking to the puppet) | Dragonborn (the listener) | Donor (the speaker) |

The parked donor always counts as being where the player is. The player and puppet both use the donor's voice type, so lines resolve to the donor's voice files. With NPC voice off in the MCM, the player keeps the Dragonborn's voice type.

## Self-dialogue

Opens when the player activates the puppet, or when a donor package that would greet the Dragonborn reaches the puppet during idle AI. The menu shows the donor's topics.

## Speech

- **Donor lines** (self-dialogue, or lines the hidden donor is given): the engine's speaker runs the line silently while the player voices it, so the conversation waits for the player. Lines from the hidden donor get a subtitle.
- **Scene lines** the player is given through the donor's aliases are voiced by the player.
- **Skipping:** the usual skip input during a self-dialogue line ends the player's line.
- Outside self-dialogue the puppet says nothing.

## Idle chatter

When the cooldown (MCM) has passed and the player is free, a random idle line the donor could say is picked from running quests and voiced by the player.

## Follower recruitment

Dialogue fragments act on the speaker, which in self-dialogue is the puppet. So while self-dialogue is open, faction changes on the puppet go to the donor, and any quest alias the puppet is put into (e.g. a follower alias) moves to the player. On revert, the donor gets it like its other aliases. Other changes a fragment makes to the speaker (items, AI) still land on the puppet ([#6](https://github.com/EricRowles/in-their-shoes-mod/issues/6)).
