#include "PCH.h"
#include "PlayerVoice.h"
#include "GameClock.h"
#include "GameUtil.h"
#include "Session.h"
#include "IdleChatter.h"

#include <algorithm>
#include <span>

#include "RE/T/TESQuest.h"
#include "RE/T/TESTopicInfo.h"
#include "RE/B/BSAudioManager.h"
#include "RE/I/ID.h"
#include "RE/A/AIProcess.h"
#include "RE/H/HighProcessData.h"
#include "RE/B/BSFaceGenAnimationData.h"

namespace InTheirShoes {

    // Unvoiced lines are shown for roughly this long per character of text
    constexpr float kUnvoicedSecondsPerChar = 0.065f;
    constexpr float kUnvoicedMinSeconds = 1.5f;
    // Failsafe so a sound handle that never reports finishing can't lock up a scene
    constexpr float kMaxLineSeconds = 30.0f;
    // A line's sound handle may not report as playing on the frame it starts
    constexpr float kLineStartGraceSeconds = 0.2f;
    // On a voice file's first play it is still loading, so the engine can't voice the line yet. Keep
    // asking it for this long before falling back to playing the file ourselves, without lip sync.
    constexpr float kEngineVoiceRetrySeconds = 0.5f;
    // A self-dialogue line that starts before the Dialogue Menu is open (the greeting) waits this long
    // for it: the engine won't voice the player's line until the menu is up
    constexpr float kMenuWaitSeconds = 1.0f;
    constexpr float kVoiceRetryIntervalSeconds = 0.1f;
    // A voiced line is done this long after its sound's duration has played
    constexpr float kSoundTailSeconds = 0.25f;
    // A stand-in's own voice plays at this volume: the engine times the line by it, and nobody hears it
    constexpr float kStandInVolume = 0.0f;

    namespace {
        std::span<RE::BSSoundHandle> GetSoundHandles(RE::Actor* a_actor) {
            auto* process = a_actor ? a_actor->GetActorRuntimeData().currentProcess : nullptr;
            if (!process || !process->high) return {};
            return process->high->soundHandles;
        }

        // A stopped handle keeps reporting "playing" for a moment, so clear it as well
        void StopSounds(RE::Actor* a_actor) {
            for (auto& handle : GetSoundHandles(a_actor)) {
                if (handle.IsValid()) {
                    handle.Stop();
                }
                handle = RE::BSSoundHandle();
            }
        }

        bool IsSoundActive(RE::Actor* a_actor) {
            for (auto& handle : GetSoundHandles(a_actor)) {
                if (!handle.IsValid()) continue;
                auto state = handle.state.get();
                if (handle.IsPlaying() || state == RE::BSSoundHandle::AssumedState::kPlaying ||
                    state == RE::BSSoundHandle::AssumedState::kInitialized) {
                    return true;
                }
            }
            return false;
        }

        // Length of the actor's voice sound in seconds, or 0 while unknown
        float GetSoundSeconds(RE::Actor* a_actor) {
            for (auto& handle : GetSoundHandles(a_actor)) {
                if (handle.IsValid() && handle.GetDuration() > 0) {
                    return static_cast<float>(handle.GetDuration()) / 1000.0f;
                }
            }
            return 0.0f;
        }

        bool HasVoice(const RE::DialogueResponse* a_response) {
            return a_response->voiceSound || !a_response->voice.empty();
        }

        RE::NiAVObject* GetHeadNode(RE::Actor* a_actor) {
            auto* root = a_actor->Get3D();
            RE::NiAVObject* head = root ? root->GetObjectByName("NPC Head [Head]"sv) : nullptr;
            return head ? head : root;
        }

        // Makes the actor's playing sounds come from, and follow, its head
        void AttachSoundsToHead(RE::Actor* a_actor) {
            auto* head = GetHeadNode(a_actor);
            if (!head) return;
            for (auto& handle : GetSoundHandles(a_actor)) {
                if (handle.IsValid()) {
                    handle.SetPosition(head->world.translate);
                    handle.SetObjectToFollow(head);
                }
            }
        }

        void ResetFace(RE::Actor* a_actor) {
            if (auto* faceData = a_actor->GetFaceGenAnimationData()) {
                RE::BSSpinLockGuard locker(faceData->lock);
                faceData->dialogueData = nullptr;
                faceData->Reset(0.0f, true, true, true, false);
                faceData->ClearExpressionOverride();
            }
            a_actor->SetEmotionType(RE::EmotionType::kNeutral);
            a_actor->SetEmotionValue(0);
        }

        // Plays the response's voice file on the actor, for when the engine didn't start it
        // (see SpeakLine). It has no lip sync.
        bool StartVoiceSound(RE::Actor* a_actor, const RE::DialogueResponse* a_response) {
            auto* process = a_actor->GetActorRuntimeData().currentProcess;
            auto* audioMgr = RE::BSAudioManager::GetSingleton();
            if (!process || !process->high || !audioMgr) return false;

            RE::BSSoundHandle handle;
            if (a_response->voiceSound) {
                audioMgr->GetSoundHandle(handle, a_response->voiceSound);
            } else if (!a_response->voice.empty()) {
                std::string path = a_response->voice.c_str();
                std::replace(path.begin(), path.end(), '/', '\\');
                if (path.size() >= 5 && _strnicmp(path.c_str(), "data\\", 5) == 0) {
                    path = path.substr(5);
                }
                auto dot = path.rfind('.');
                std::string base = dot != std::string::npos ? path.substr(0, dot) : path;
                // Voice files ship as .fuz in the archives even though the response names the .wav
                for (const auto& candidate : { base + ".fuz", base + ".wav" }) {
                    RE::BSResource::ID resID;
                    resID.GenerateFromPath(candidate.c_str());
                    audioMgr->GetSoundHandleByFile(handle, resID, 0x10, 64);
                    if (handle.IsValid()) break;
                }
            }

            if (!handle.IsValid()) {
                return false;
            }
            if (auto* head = GetHeadNode(a_actor)) {
                handle.SetPosition(head->world.translate);
                handle.SetObjectToFollow(head);
            }
            handle.SetVolume(1.0f);
            handle.Play();
            process->high->soundHandles[0] = handle;
            return true;
        }

        void CopyResponse(const RE::DialogueResponse* a_from, RE::DialogueResponse& a_to) {
            a_to.text = a_from->text;
            a_to.animFaceArchType = a_from->animFaceArchType;
            a_to.percent = a_from->percent;
            a_to.voice = a_from->voice;
            a_to.voiceSound = a_from->voiceSound;
            a_to.soundLip = a_from->soundLip;
            a_to.speakerIdle = a_from->speakerIdle;
            a_to.listenIdle = a_from->listenIdle;
            a_to.useEmotion = a_from->useEmotion;
        }

        // Leaves the response's text and voice (the engine times the line by the voice) but no emotion or idles.
        // Its lip sync comes with the voice file: see SilenceStandIn.
        void StripPerformance(RE::DialogueResponse* a_response) {
            a_response->useEmotion = false;
            a_response->animFaceArchType = RE::EmotionType::kNeutral;
            a_response->percent = 0;
            a_response->speakerIdle = nullptr;
            a_response->listenIdle = nullptr;
        }

        // Idle chatter skips the speech path that sends this event; sending it runs the info's fragments (#5).
        void SendTopicInfoEvent(const RE::DialogueItem& a_item, RE::TESTopicInfoEvent::TopicInfoEventType a_type) {
            auto* events = RE::ScriptEventSourceHolder::GetSingleton();
            if (!events || !a_item.info) return;
            RE::TESTopicInfoEvent event;
            event.speakerRef.reset(a_item.speaker);
            event.topicInfoFormID = a_item.info->GetFormID();
            event.type = a_type;
            event.stage = 0;
            events->SendEvent(&event);
        }
    }

    void PlayerVoice::Clear() {
        _line.reset();
    }

    // --- Speaking ---

    bool PlayerVoice::HasVoiceSound(RE::Actor* a_actor) {
        return GetSoundSeconds(a_actor) > 0.0f;
    }

    bool PlayerVoice::IsVoiceActive(RE::Actor* a_actor) {
        return IsSoundActive(a_actor);
    }

    void PlayerVoice::Speak(RE::DialogueResponse* a_response, bool a_unused) {
        SpeakLine(a_response, a_unused, false);
    }

    void PlayerVoice::SpeakIdleChatter(RE::BSTSmartPointer<RE::DialogueItem> a_item) {
        if (_line) Finish();
        SendTopicInfoEvent(*a_item, RE::TESTopicInfoEvent::TopicInfoEventType::kTopicBegin);
        StartLine(a_item->responses.front(), false, true);
        _line->item = std::move(a_item);  // the response must outlive the line
    }

    bool PlayerVoice::SpeakNextIdleResponse() {
        // An info's responses are spoken one after another, as the engine does (#34). The end event waits for
        // the last.
        auto item = _line->item;
        const auto next = _line->responseIndex + 1;
        std::size_t index = 0;
        for (auto* response : item->responses) {
            if (index++ == next && response) {
                StartLine(response, false, true);
                _line->item = std::move(item);
                _line->responseIndex = next;
                return true;
            }
        }
        return false;
    }

    void PlayerVoice::TakeStandInLine(RE::DialogueResponse* a_engineResponse) {
        // The player voices its own copy; the stand-in runs the engine's response unheard, without emotion or idles,
        // so the engine paces the conversation.
        CopyResponse(a_engineResponse, _standInLine);
        StripPerformance(a_engineResponse);
    }

    void PlayerVoice::SpeakForStandIn(RE::Actor* a_standIn, bool a_unused) {
        SpeakLine(&_standInLine, a_unused, false);
        _line->standIn = a_standIn->GetHandle();
        _line->followsStandIn = true;
    }

    void PlayerVoice::SilenceStandIn(RE::Actor* a_actor) {
        for (auto& handle : GetSoundHandles(a_actor)) {
            if (handle.IsValid()) {
                handle.SetVolume(kStandInVolume);
            }
        }
        // The engine loads the voice file's lip sync whatever the response says. Dropping it keeps the
        // stand-in's mouth shut. (Not released, as in ResetFace.)
        if (auto* faceData = a_actor->GetFaceGenAnimationData()) {
            RE::BSSpinLockGuard locker(faceData->lock);
            if (faceData->dialogueData) {
                faceData->dialogueData = nullptr;
                faceData->Reset(0.0f, true, true, true, false);
            }
        }
    }

    void PlayerVoice::SpeakLine(RE::DialogueResponse* a_response, bool a_unused, bool a_isIdleChatter) {
        // End the playing line first, so it still sends its end event and reports to IdleChatter.
        if (_line) Finish();
        StartLine(a_response, a_unused, a_isIdleChatter);
    }

    void PlayerVoice::StartLine(RE::DialogueResponse* a_response, bool a_unused, bool a_isIdleChatter) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        StopSounds(player);

        Line line;

        if (a_response->useEmotion) {
            player->SetEmotionType(a_response->animFaceArchType.get());
            player->SetEmotionValue(a_response->percent);
        }

        auto& runtime = player->GetActorRuntimeData();
        runtime.boolBits.reset(RE::Actor::BOOL_BITS::kSpeakingDone);
        runtime.boolBits.set(RE::Actor::BOOL_BITS::kSoundFileDone);
        runtime.boolBits.set(RE::Actor::BOOL_BITS::kVoiceFileDone);
        runtime.voiceTimer = 0.0f;
        _engine.setSpeakingDone(player, false);

        // PlayerCharacter's own UpdateInDialogue doesn't run lines; the Character version does, and voices
        // the line itself (with lip sync) when the voice file is loaded. Update retries it while
        // the file loads, and falls back to playing the file without lip sync.
        auto* ui = RE::UI::GetSingleton();
        line.waitsForMenu = Session::GetSingleton().IsInSelfDialogue() && !(ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME));
        if (!line.waitsForMenu) {
            // Voiced from the frame when called elsewhere: voices started from the Dialogue Menu's job threads
            // never played (#3)
            if (IsMainThread()) {
                _engine.updateInDialogue(player, a_response, a_unused);
            }
            runtime.soundCallBackSet = 1;
        }

        line.start = GameClock::Now();
        line.created = line.start;
        line.hadSound = IsSoundActive(player);
        line.soundStart = line.start;
        line.lastSoundTick = line.start;
        line.pendingVoice = (!line.hadSound && HasVoice(a_response)) ? a_response : nullptr;
        if (line.hadSound) {
            AttachSoundsToHead(player);
        }
        SKSE::log::info("Player line: '{}' (engine voiced={}, idleChatter={})",
            a_response->text.c_str(), line.hadSound, a_isIdleChatter);

        float textLen = static_cast<float>(a_response->text.length());
        line.unvoicedDuration = std::max(kUnvoicedMinSeconds, textLen * kUnvoicedSecondsPerChar);
        line.isIdleChatter = a_isIdleChatter;
        _line = std::move(line);
    }

    void PlayerVoice::StartVoiceLate() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto& line = *_line;
        if (line.waitsForMenu) {
            auto* ui = RE::UI::GetSingleton();
            bool menuOpen = ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
            if (!menuOpen && GameClock::SecondsSince(line.start) < kMenuWaitSeconds) return;
            line.waitsForMenu = false;
            line.start = GameClock::Now();  // the line starts now
            player->GetActorRuntimeData().soundCallBackSet = 1;
        }

        float elapsed = GameClock::SecondsSince(line.start);

        if (elapsed < kEngineVoiceRetrySeconds) {
            if (GameClock::SecondsSince(line.lastVoiceRetry) < kVoiceRetryIntervalSeconds) return;
            line.lastVoiceRetry = GameClock::Now();
            _engine.updateInDialogue(player, line.pendingVoice, false);
            if (!IsSoundActive(player)) return;
            SKSE::log::info("Engine voiced the player line on retry after {:.2f}s", elapsed);
        } else if (StartVoiceSound(player, line.pendingVoice)) {
            SKSE::log::info("Engine didn't voice the player line; playing the voice file without lip sync");
        } else {
            SKSE::log::warn("No voice sound could be started for '{}'", line.pendingVoice->voice.c_str());
            line.pendingVoice = nullptr;
            return;
        }

        AttachSoundsToHead(player);
        line.pendingVoice = nullptr;
        line.hadSound = true;
        line.soundStart = GameClock::Now();
        line.lastSoundTick = line.soundStart;
    }

    // A stand-in's line ends when the engine says the stand-in has finished speaking. The player's voice may
    // have started later than the stand-in's (waiting for the menu or for the voice file), so it gets that
    // long to finish; a new line or the menu closing ends it sooner.
    bool PlayerVoice::StandInLineDone() {
        auto& line = *_line;
        auto standIn = line.standIn.get();
        if (!standIn) return true;
        if (GameClock::SecondsSince(line.created) < kLineStartGraceSeconds) return false;
        if (!standIn->QSpeakingDone()) {
            line.standInDoneAt.reset();
            return false;
        }
        if (!line.hadSound) return true;
        if (!line.standInDoneAt) {
            line.standInDoneAt = GameClock::Now();
        }
        const auto lag = static_cast<float>(line.soundStart - line.created);
        return GameClock::SecondsSince(*line.standInDoneAt) >= lag || !IsSoundActive(RE::PlayerCharacter::GetSingleton());
    }

    // --- Playback ---

    void PlayerVoice::Update() {
        if (!_line) return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto& line = *_line;

        // A scene the player is in takes over from idle chatter: its lines would talk over this one, and the
        // scene runs them on the player without going through here
        if (auto* scene = GetPlayingScene(player); line.isIdleChatter && scene) {
            SKSE::log::info("Scene {:08X} started; ending the idle line", scene->GetFormID());
            Finish();
            return;
        }

        // The player's line isn't ticked while the Dialogue Menu is open; doing so stalls its lip sync
        auto* ui = RE::UI::GetSingleton();
        if (!(ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME))) {
            _engine.updateInDialogue(player, nullptr, false);
        }

        if (line.pendingVoice) {
            StartVoiceLate();
        }

        if (line.followsStandIn) {
            if (!StandInLineDone()) return;
        } else if (line.hadSound) {
            // Timed from the sound's duration, once its file has loaded. GameClock leaves out pauses, loading
            // screens and hitches, which the voice doesn't play through either.
            const auto now = GameClock::Now();
            const float heard = static_cast<float>(now - line.lastSoundTick);
            line.lastSoundTick = now;
            line.soundActive += heard;
            if (line.soundSeconds <= 0.0f) {
                line.soundSeconds = GetSoundSeconds(player);
            } else {
                line.soundPlayed += heard;
            }
            bool done = line.soundSeconds > 0.0f ? line.soundPlayed >= line.soundSeconds + kSoundTailSeconds
                                                 : line.soundActive >= kLineStartGraceSeconds && !IsSoundActive(player);
            if (!done) {
                if (line.soundActive < kMaxLineSeconds) return;
                SKSE::log::warn("Player line still playing after {:.0f}s; finishing it", line.soundActive);
            }
        } else if (line.pendingVoice || GameClock::SecondsSince(line.start) < line.unvoicedDuration) {
            return;
        }

        SKSE::log::info("Player line finished after {:.2f}s (voiced={}, sound length {:.2f}s, played {:.2f}s)",
            GameClock::SecondsSince(line.start), line.hadSound, line.soundSeconds, line.soundPlayed);
        if (line.isIdleChatter && line.item && SpeakNextIdleResponse()) return;
        Finish();
    }

    void PlayerVoice::Finish() {
        if (!_line) return;
        // Dropped first: the speaking hooks must see that no line is playing
        const auto line = std::move(*_line);
        _line.reset();

        auto* player = RE::PlayerCharacter::GetSingleton();
        StopSounds(player);
        player->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSpeakingDone);
        player->GetActorRuntimeData().voiceTimer = 0.0f;
        _engine.setSpeakingDone(player, true);
        ResetFace(player);

        if (line.isIdleChatter) {
            if (line.item) {
                SendTopicInfoEvent(*line.item, RE::TESTopicInfoEvent::TopicInfoEventType::kTopicEnd);
            }
            IdleChatter::GetSingleton().OnLineFinished();
        }
    }

}
